// #define K2_DEBUG_VERBOSE
// #define K2_DEBUG_INFO
#define K2_DEBUG_WARN

#include "plat.h"
#include "utils.h"
#include "sched.h"
#include "printf.h"
#include "spinlock.h"
#include "mmu.h"
#include "entry.h"

/*      Locking protocol 

    sched_lock is our 'scheduler lock'. protects adding/deleting/iterating of
    task[] entries. task_struct::state indicates whehter a slot is free,
    sched_lock must be held to access it. 

    task::cpu_context is not protected with lock, as it is only touched in task
    creation and cpu_switch_to(), when no other code would access cpu_context

    all other fields in a task::struct is protected by task_struct::lock

    a single mm_struct is protected by mm_struct::lock; the allocation of
    mm_struct (in mm_table) is done via __atomic intrinsics on (mm_struct:ref)
    hence lockfree. 

    ----- 
    exec():  mm::lock --> sched_lock (b/c readi())     
        a bad design??

    fork() : sched_lock -> task::lock -> (mm_alloc no longer grabs mm::lock, just use
        intrinics) 

    freeproc():  sched_lock -> task::lock -> mm::lock

    THEREFORE: deadlock still possible between exec() and freeproc() 
        but how will this occur? a task cannot call exec() and freeproc() same time..

    more locking consideration: cf README-next-fxl: 1530
 */

static struct mm_struct mm_table[NR_MMS];

static char *states[] = {
    [TASK_UNUSED]   "UNUSED  ",
    [TASK_RUNNING]  "RUNNING ",
    [TASK_SLEEPING] "SLEEP   ",
    [TASK_RUNNABLE] "RUNNABLE",
    [TASK_ZOMBIE]   "ZOMBIE  "};

// kernel_stacks[i]: kernel stack for task with pid=i. 
// WARNING: various kernel code assumes each kernel stack is page-aligned. 
// cf. ret_from_syscall (entry.S). if you modify the d/s below, keep that in mind. 
static __attribute__ ((aligned (PAGE_SIZE)))
char kernel_stacks[NR_TASKS][THREAD_SIZE]; 

// used during boot, then used as the kern stacks for idle tasks
__attribute__ ((aligned (PAGE_SIZE))) 
char boot_stacks[NCPU][THREAD_SIZE];

struct task_struct *init_task; 
struct task_struct *task[NR_TASKS]; // normal tasks 
struct task_struct *idle_tasks[NCPU];  // per cpu, only scheduled when no normal tasks runnable

struct spinlock sched_lock = {.locked=0, .cpu=0, .name="sched"};

struct cpu cpus[NCPU]; 

// For MP.
struct task_struct *myproc(void) {      
    struct task_struct *p;
    // need disable irq b/c: if right after mycpu(), the cur task moves to 
    // a diff cpu, then cpu still points to a previous cpu and ->proc 
    // is not this task but a diff one
	push_off(); 
    p=mycpu()->proc; 
    pop_off(); 
	return p; 
};
// NB: after myproc(), it's ok if the cur task is moved to a diff cpu; b/c local
// var is on the task's kern stack, the task_struct * is still valid.

// get a task's saved registers ("trapframe"), at the top of the task's kernel page. 
// these regs are saved/restored by kernel_entry()/kernel_exit(). 
struct trapframe * task_pt_regs(struct task_struct *tsk) {
	unsigned long p = (unsigned long)tsk + THREAD_SIZE - sizeof(struct trapframe);
	return (struct trapframe *)p;
}

extern void init(int arg); // kernel.c

// must be called before any schedule() or timertick() occurs
void sched_init(void) {
    for (int i = 0; i < NR_TASKS; i++) {
        task[i] = (struct task_struct *)(&kernel_stacks[i][0]); 
        BUG_ON((unsigned long)task[i] & ~PAGE_MASK);  // must be page aligned. see above
        memset(task[i], 0, sizeof(struct task_struct)); // zero everything
        initlock(&(task[i]->lock), "task");
        task[i]->state = TASK_UNUSED;
    }

    for (int i = 0; i < NCPU; i++) {
        idle_tasks[i] = (struct task_struct *)(&boot_stacks[i][0]); 
        cpus[i].proc = idle_tasks[i]; 
        initlock(&(idle_tasks[i]->lock), "idle"); // some code will try to grab
        snprintf(idle_tasks[i]->name, 10, "idle-%d", i); 
        idle_tasks[i]->pid = -1; // not meaningful. a placeholder
        // when each cpu calls schedule() for the first time, they will 
        // jump off the idle task to "normal" ones, saving cpu_context 
        // (inc sp/pc) to idle_tasks[i]
    }

    memset(mm_table, 0, sizeof(mm_table)); 
    for (int i = 0; i < NR_MMS; i++)
        initlock(&mm_table[i].lock, "mmlock");
    
    // init task, will be picked up once cpu0 calls schedule() for the 1st time
    // current = init_task = task[0]; // UP
    init_task = task[0]; 
    init_task->state = TASK_RUNNABLE;
    init_task->cpu_context.x19 = (unsigned long)init; 
    init_task->cpu_context.pc = (unsigned long)ret_from_fork; // entry.S
    init_task->cpu_context.sp = (unsigned long)init_task + THREAD_SIZE; 

    init_task->credits = 0;
    init_task->priority = 2;
    init_task->flags = PF_KTHREAD;
    init_task->mm = 0;  // nothing (kernel task) 
    init_task->chan = 0;
    init_task->killed = 0;
    init_task->pid = 0;
    init_task->cwd = 0;
    safestrcpy(init_task->name, "init", 5);
}

// return cpuid for the task currently on; 
// -1 on no found or error
// caller must hold sched_lock
static int task_on_cpu(struct task_struct *p) {
    if (!p) {BUG(); return -1;}
    for (int i = 0; i < NCPU; i++)
        if (cpus[i].proc == p)
            return i; 
    return -1; 
}

// the scheduler, called by tasks or irq
// caller must NOT hold sched_lock
void schedule() {
    V("cpu%d schedule", cpuid());

	int next, max_cr;
    int cpu, oncpu;
    
    // this cpu run on the kernel stack of task "cur"; our design 
    // ensures that "cur" CANNOT be picked by other cpus
	struct task_struct *p, *cur=myproc();
    int has_runnable; 

    acquire(&sched_lock); 
    cpu = cpuid();  // holding sched_lock, the cur process wont mirgrate across cpus

	while (1) {
		max_cr = -1; 
		next = 0;
        has_runnable = 0; 

		// Among all RUNNABLE tasks (+ the cur task, if it's RUNNING), 
        //    find a task w/ maximum credits.
		for (int i = 0; i < NR_TASKS; i++){
			p = task[i]; BUG_ON(!p);
            // if task is active on other cpu, dont touch
            oncpu = task_on_cpu(p); 
            if (oncpu != -1 && oncpu != cpu) 
                continue;
			if ((p == cur && p->state == TASK_RUNNING)
                || p->state == TASK_RUNNABLE) {
                has_runnable = 1; 
                // NB: p->credits protected by sched_lock
                V("cpu%d pid %d credits %ld", cpu, i, p->credits);
				if (p->credits > max_cr) { max_cr = p->credits; next = i; }
			}
		}        
		if (max_cr > 0) {
            I("cpu%d picked pid %d state %d", cpu, next, task[next]->state);
	        switch_to(task[next]);
			break;
        }

		// No task can run ...
        if (has_runnable) { 
            // reason1: insufficient credits. recharge for all & retry scheduling
            for (int i = 0; i < NR_TASKS; i++) {
                p = task[i]; BUG_ON(!p);
                if (p->state != TASK_UNUSED) {
                    // NB: p->credits/priority protected by sched_lock
                    p->credits = (p->credits >> 1) + p->priority;  // per priority
                }                
            }
        } else { // reason2: no normal tasks RUNNABLE (inc. cur task)
            V("cpu%d nothing to run. switch to idle", cpu); 
            #ifdef K2_DEBUG_VERBOSE
            procdump(); 
            #endif
            switch_to(idle_tasks[cpu]); // if cpu already on idle task, this will do nothing
            break;
        }
	}
    release(&sched_lock);
    // leave the scheduler 
}

// Another path to leave the scheduler
// this function exists b/c when a task is first time switch_to()'d (see above), 
// its pc points to ret_from_fork instead of the instruction right after 
// switch_to(). to make the preempt_disable/enable balance, ret_from_fork calls
// leave_scheduler() below
void leave_scheduler(void) {
    release(&sched_lock);
}

// voluntarily reschedule; gives up all remaining schedule credits
// only called from tasks
void yield(void) {    
    struct task_struct *p = myproc(); 
    acquire(&sched_lock); p->credits = 0; release(&sched_lock);
    schedule();
}

// caller must hold sched_lock, and not holding next->lock
// called when preemption is disabled, so the cur task wont lose cpu
void switch_to(struct task_struct * next) {
	struct task_struct * prev; 
    struct task_struct *cur; 

    cur = myproc(); BUG_ON(!cur); 
	if (cur == next) 
		return; 

	prev = cur;
	mycpu()->proc = next;

	if (prev->state == TASK_RUNNING) // preempted 
		prev->state = TASK_RUNNABLE; 
	next->state = TASK_RUNNING;

    // acquire(&next->lock); 
    if (next->mm) { // user task
        acquire(&next->mm->lock); 
	    set_pgd(next->mm->pgd);
        release(&next->mm->lock); 
        // now, next->mm should be effective. 
        // can use gdb to inspect user mapping here
    }
    // release(&next->lock);

    /*
        below is where context switch happens.

        after cpu_switch_to(), the @prev's cpu_context.pc points to the instruction right after
        cpu_switch_to(). this is where the @prev task will resume in the future.
        for example, shown as the arrow below:

            cpu_switch_to(prev, next);
            80d50:       f9400fe1        ldr     x1, [sp, #24]
            80d54:       f94017e0        ldr     x0, [sp, #40]
            80d58:       9400083b        bl      82e44 <cpu_switch_to>
        ==> 80d5c:       14000002        b       80d64 <switch_to+0x58>

        cpu_switch_to() does not need task::lock, cf "locking protocol" on the top
    */
    cpu_switch_to(prev, next);  // sched.S will branch to @next->cpu_context.pc
}

#define CPU_UTIL_INTERVAL 10  // cal cpu measurement every X ticks

// extern int buggycpu;
// caller by timer irq handler, with irq automatically turned off by hardware,
//      which can be verified by is_irq_masked()
//quest: multicore
void timer_tick() {
    struct task_struct *cur = myproc();
    struct cpu* cp = mycpu(); 

    if (cur) {
        V("enter timer_tick cpu%d task %s pid %d", cpuid(), cur->name, cur->pid);
        if (cur->pid>=0 && cur->state == TASK_RUNNING) // not "idle" (pid -1), and running
            cp->busy++; 

        // calculate cpu util %, update cp->last_util
        if ((cp->total++ % CPU_UTIL_INTERVAL) == CPU_UTIL_INTERVAL - 1) {
             
            /* STUDENT_TODO: your code here */
        }

        acquire(&sched_lock); 
        if (cur->pid>=0 && --cur->credits > 0) { // cur task continues to exec
            V("leave timer_tick. no resche");
            // if (cpuid()==buggycpu)
            //     E("leave timer_tick. no resche");
            release(&sched_lock); return;
        }
        cur->credits=0;
        release(&sched_lock);
    }

	enable_irq();
        /* what if a timer irq happens here? schedule() will be called twice
            back-to-back, no interleaving so we're fine. HOWEVER, if timer irqs
            occur more often than we can handle, the earlier schedule() calls
            may never have a chance to finish, the kernel stack will exhaust,
            corrupting the kernel memory. (the kernel will throw strange bugs) 
        */
	schedule();

    V("leave timer_tick cpu%d task %s pid %d", cpuid(), cur->name, cur->pid);
	/* disable irq until kernel_exit, in which eret will resort the interrupt 
        flag from spsr, which sets it on. */
	disable_irq(); 
}


// Design patterns for sleep() & wakeup() 
// 
// sleep() always needs to hold a lock (lk). inside sleep(), once the calling
// task grabs sched_lock (i.e. no other tasks can change their p->state), lk is
// released
//
// ONLY USE sched_lock to serialize task A/B is not enough wakeup() does NOT
// need to hold lk. if that's the case, it's possible: task B: sleep(on chan) in
// a loop; after it wakes up (no schedlock; only lk), before it calls sleep()
// again, task A calls wakeup(chan), taking schelock and wakes up no task -->
// wakeup is lost So our kernel cannot help on this case
//
// to avoid the above, task A calling wakeup() must hold lk beforehand. b/c of
// this, only after task B inside sleep() rls lk, task A can proceed to
// wakeup(). inside wakeup(), task A is further serialized on schedlock, which
// must wait until that task B has completely changed its p->state and is moved
// off the cpu

// Wake up all processes sleeping on chan. Only change p->state; wont call
// schedule() return # of tasks woken up.
// Caller must hold sched_lock 
static int wakeup_nolock(void *chan) {
    struct task_struct *p;
    int cnt = 0; 
    // V("chan=%lx", (unsigned long)chan);
	for (int i = 0; i < NR_TASKS; i ++) {
		p = task[i]; 
        // NB: it's possible that p == cur and should be woken up
        if (p->state == TASK_UNUSED) continue; 
        // acquire(&p->lock);     // for p->chan. also cf sleep() below
        if (p->state == TASK_SLEEPING && p->chan == chan) {
            p->state = TASK_RUNNABLE;
            cnt ++; 
            I("wakeup cpu%d chan=%lx pid %d", cpuid(),
                (unsigned long)p->chan, p->pid);
        }
        // release(&p->lock);
    }
    return cnt; 
}

// Must be called WITHOUT sched_lock 
// Called from irq (many drivers) or task
// return # of tasks woken up
int wakeup(void *chan) {
    int cnt; 
    acquire(&sched_lock);     
    cnt = wakeup_nolock(chan); 
    release(&sched_lock);
    return cnt; 
}

// Atomically release "lk" and sleep on chan.
// Reacquires lk when awakened.
// Called by tasks with @lk held
void sleep(void *chan, struct spinlock *lk) {
    struct task_struct *p = myproc();

    // Must acquire sched_lock in order to
    // change p->state and then call schedule().
    // 
    // this is useful for many drivers where caller acquire
    // the same "lk" before calling wakeup() 
    // (e.g. lk protects the same buffer, cf pl011.c)

    // Once we hold sched_lock, we can be
    // guaranteed that we won't miss any wakeup (meaning that another task 
    // calling wakeup() w/ holding lk)
    // b/c wakeup() can only 
    // start to wake up tasks after it locks sched_lock.
    // so it's okay to release lk.
    
    // corner case: lk==sched_lock, which is already held by cur task.
    // the right behavior of sleep(): keep sched_lock and switch to idle task, which 
    // later will release the lock
    if (lk != &sched_lock) {
        acquire(&sched_lock);
        release(lk);
    }

    I("sleep chan=%lx pid %d", (unsigned long)chan, p->pid);

    // Go to sleep.
    p->chan = chan;
    p->state = TASK_SLEEPING;

    // although the task has not used up the current tick, bill it regardless.
    // thus this task will be disadvantaged in future scheduling 
    p->credits --; 

    // switch the cpu away from the current kern stack to the idle task, which we
    // know exists for sure. the idle task will return from the schedule() and 
    // rls sched_lock. the next timertick will call schedule() and switch 
    // to a normal task (if any) 
    struct task_struct *idle = idle_tasks[cpuid()];
    mycpu()->proc = idle; 
    cpu_switch_to(p, idle);  
    
    // cpu_switch_to() back here when the cur task is woken up. 
    // it now has sched_lock. 

    // Tidy up.
    p->chan = 0;

    if (lk != &sched_lock) {
        release(&sched_lock);        
        acquire(lk); // Reacquire original lock.
    } // else keep holding sched_lock
}

// Pass p's abandoned children to init. (ie direct reparent to initprocess)
// return # of children reparanted
// Caller must hold sched_lock.   
static int reparent(struct task_struct *p) {
    struct task_struct **child;
    int cnt = 0; 
    for (child = task; child < &task[NR_TASKS]; child++) {
        BUG_ON(!(*child));
        if ((*child)->state == TASK_UNUSED) continue;
        if ((*child)->parent == p) {
            (*child)->parent = init_task;
            cnt ++; 
        }
    }
    return cnt; 
}

static void freeproc(struct task_struct *p); 

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children. 
// addr=0 a special case, dont care about status
int wait(uint64 addr /*dst user va to copy status to*/) {
    struct task_struct **pp;
    int havekids, pid;
    struct task_struct *p = myproc();

    I("pid %d (%s) entering wait()", p->pid, p->name);

    // make sure the (zombie) child is done with exit() and has been 
    // switched away from (so that no cpu uses the zombie's kern stack) 
    // cf exit_process() below
    acquire(&sched_lock); 

    for (;;) {
        // Scan through table looking for exited children.  pp:child
        havekids = 0;
        for (pp = task; pp < &task[NR_TASKS]; pp++) {
            struct task_struct *p0 = *pp; BUG_ON(!p0); 
            if (p0->state == TASK_UNUSED) continue; 
            if (p0->parent == p) {
                havekids = 1;
                if (p0->state == TASK_ZOMBIE) {
                    // Found one.
                    pid = p0->pid;
                    I("found zombie pid=%d", pid); BUG_ON(addr!=0 && !p->mm);//addr!=0 implies user task; mm must exist
                    if (addr != 0 && copyout(p->mm, addr, (char *)&(p0->xstate),
                                             sizeof(p0->xstate)) < 0) {
                        release(&sched_lock); 
                        return -1;
                    }
                    freeproc(p0);       // will mark the task slot as unused                    
                    release(&sched_lock); 
                    // the task slot now may be reused
                    return pid;
                }
            }
        }
        
        // No point waiting if we don't have any children.
        if (!havekids || killed(p)) {
            release(&sched_lock);
            return -1;
        }

        I("pid %d sleep on %lx", p->pid, (unsigned long)&sched_lock);
        sleep(p, &sched_lock); // sleep on own task_struct
        I("pid %d wake up from sleep. p->chan %lx state %d", p->pid, 
            (unsigned long)p->chan, p->state);
    }
}

// Becomes a zombie task and switch the cpu away from it 
// only when parent calls wait() this zombie task successfully, the zombie's 
// kernel stack (and task_struct on it) will be recycled.
void exit_process(int status) {
    struct task_struct *p = myproc();

    I("pid %d (%s): exit_process status %d", p->pid, p->name, status);

    if (p == init_task)
        panic("init exiting");

    // Close all open files.
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            struct file *f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = 0;
        }
    }

    if (p->cwd) {
        begin_op();
        iput(p->cwd);
        end_op();
    }
    p->cwd = 0;

    // Free any IO resources ... only for user tasks
    // this avoids kb_task exit_process() from calling sf_free() which tries to take
    // sflock 
    extern int sf_free(int pid); // free surface, sf.c
    if (!(p->flags & PF_KTHREAD))
        sf_free(p->pid); // ok if failed

    // This prevents to parent from checking & recycling this zombie until 
    // the cpu moves away from the zombie's stack (see below)
    acquire(&sched_lock); 

    // Give any children to init.
    if (reparent(p)) 
        wakeup_nolock(init_task);

    // Parent might be sleeping in wait().
    wakeup_nolock(p->parent); 
    p->xstate = status;
    p->state = TASK_ZOMBIE;
    
    V("exit done. will switch away...");
    // now the woken parent still CANNOT recycle this zombie b/c we hold
    // sched_lock 

    // switch the cpu away from zombie's kern stack to the idle task, which we
    // know exists for sure. the next timertick will call schedule() and switch 
    // to a normal task (if any)
    struct task_struct *idle = idle_tasks[cpuid()];
    mycpu()->proc = idle; 
    cpu_switch_to(p, idle);  // never return

    // the "switch-to" task will resume from the schedule()'s exit path, 
    // which will release sched_lock
    // after sched_lock is released, the parent can proceed to recycle
    // the zombie's kern stack (& task_struct), which is no longer used by 
    // any cpu    
    panic("zombie exit");
}

/* Destroys a task: task_struct, kernel stack, etc. free a proc structure and
    the data hanging from it, including user & kernel pages. 

    sched_lock must be held.  p->lock must be held */
static void freeproc(struct task_struct *p) {
    BUG_ON(!p); V("%s entered. pid %d", __func__, p->pid);

    p->state = TASK_UNUSED; // mark the slot as unused
    // no need to zero task_struct, which is among the task's kernel page
    // FIX: since we cannot recycle task slot now, so we dont dec nr_tasks ...

    if (p->mm) {    // kernel task has mm==0
        acquire(&p->mm->lock); 
        // must hold mm::lock: need the lock immediately in case newref==0
        int newref = __atomic_sub_fetch(&p->mm->ref, 1, __ATOMIC_SEQ_CST); 
        BUG_ON(newref<0);
        if (newref == 0) {
            V("<<<< free mm %lu", p->mm-mm_table); 
            free_task_pages(p->mm, 0 /* free all user and kernel pages*/);        
        }
        release(&p->mm->lock);
        p->mm = 0; 
    }

    p->flags = 0; 
    p->killed = 0; 
    p->credits = 0; 
    p->chan = 0; 
    p->pid = 0; 
    p->xstate = 0; 
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see ret_from_syscall).
int kill(int pid) {
    int i;
    struct task_struct *p;

    acquire(&sched_lock); 
    for (i = 0; i < NR_TASKS; i++) {
        p = task[i];
        BUG_ON(!p); BUG_ON(i==pid && p->state == TASK_UNUSED);
        if (p->state == TASK_UNUSED) continue;
        if (i == pid) { // index is pid
            acquire(&p->lock); p->killed = 1; release(&p->lock);
            if (p->state == TASK_SLEEPING) {
                // Wake the task from sleep(), so it can run & quit
                p->state = TASK_RUNNABLE;
            }            
            release(&sched_lock); 
            I("kill succeeds, pid =%d", pid);
            return 0;
        }
    }
    release(&sched_lock); 
    W("kill failed, pid =%d", pid);
    return -1;
}

// mark a task as "killed" (e.g. a faulty one)
// Set a flag, so exit_process() is called in ret_from_syscall (entry.S)
// useful only if p is known RUNNING (e.g. p is cur). otherwise, use kill()
void setkilled(struct task_struct *p) {
    acquire(&p->lock);
    p->killed = 1;
    release(&p->lock);
}

int killed(struct task_struct *p) {
    int k;
    acquire(&p->lock);
    k = p->killed;
    release(&p->lock);
    return k;
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void) {
    struct task_struct *p;
    char *state;

    printf("\t %5s %10s %10s %20s\n", "pid", "state", "name", "sleep-on");

    for (int i = 0; i < NR_TASKS; i++) {
        p = task[i];
        if (p->state == TASK_UNUSED)
            continue;
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";
        printf("\t %5d %10s %10s %20lx\n", p->pid, state, p->name, 
               (unsigned long)p->chan);
    }
    
    // TBD: move it to /proc/meminfo, also count malloc() allocations
extern unsigned paging_pages_used, paging_pages_total; // alloc.c
	printf("paging mem: used %u total %u (%u/100)\n", 
		paging_pages_used, paging_pages_total, 
        paging_pages_used*100/(paging_pages_total));

    // debug
    // printf("cpu total ticks: %lu %lu %lu %lu\n", 
    //     cpus[0].total, cpus[1].total, cpus[2].total, cpus[3].total);
    // printf("cpu busy ticks: %d %d %d %d\n", 
    //     cpus[0].busy, cpus[1].busy, cpus[2].busy, cpus[3].busy);
}

/* -------------  fork related  -------------------- */

/* alloc a blank mm. 
    on success, return mm, AND hold mm->lock  on failure, return 0 */
static struct mm_struct *alloc_mm(void) {
    struct mm_struct *mm; 
    for (mm = mm_table; mm < mm_table + NR_MMS; mm++) {
        int expected = 0; 
	    if (__atomic_compare_exchange_n(&mm->ref /*ptr*/, 
            &expected /* expected: ref=0 */, 
            1 /*desired ref*/, 0 /*weak*/, 
            __ATOMIC_SEQ_CST /*success_memorder*/,
            __ATOMIC_SEQ_CST /*failure_memorder*/)) {
            // have successfully set mm->ref from 0 to 1
            acquire(&mm->lock);
            break; 
        }
    }
    if (mm == mm_table + NR_MMS) 
        {E("no free mm"); BUG(); return 0;}  
    // now we hold mm->lock
    mm->pgd = 0; 
    mm->kernel_pages_count = 0;
    mm->user_pages_count = 0;
    V(">>>> alloc_mm %lu", mm-mm_table); 
    return mm;
}

/*
   	Create 1st user task by elevating a kernel task to EL0

	Populate trapframe for returning to user space (via kernel_exit) for the 1st time.
	Note that the actual switch will not happen until kernel_exit.

	@start: beginning of the user code (to be copied to the new task). kernel va
	@size: size of the initial code area
	@pc: offset of the startup function inside the area

    return 0 on ok, -1 on failure
*/

int move_to_user_mode(unsigned long start, unsigned long size, unsigned long pc) {
    BUG_ON(size > PAGE_SIZE); // initially, user va only covers [0,PAGE_SIZE)
    struct task_struct *cur = myproc();

	struct trapframe *regs = task_pt_regs(cur);
	V("pc %lx", pc);

    BUG_ON(cur->mm); // kernel task has no mm 

    if (!(cur->mm = alloc_mm())) return -1; 
    // now we have current->mm->lock

	regs->pstate = PSR_MODE_EL0t;
	regs->pc = pc;
	regs->sp = USER_VA_END; // 2 *  PAGE_SIZE;  <--too small

	/* only allocate 1 code page here b/c the stack page is to be mapped on demand. 
	   this will trigger allocating the task's pgtable tree (mm.pgd)
	*/	
	void *code_page = allocate_user_page_mm(cur->mm, 0 /*va*/, 
        MMU_PTE_FLAGS | MM_AP_RW);
	if (code_page == 0)	{
        release(&cur->mm->lock);
		return -1;
	}	
    // at this time, user vm only covers [0,PAGE_SIZE). large enough for the init user
    // code, which later invokes exec() and enlarges mm->sz
	cur->mm->sz = cur->mm->codesz = PAGE_SIZE; 
	memmove(code_page, (void *)start, size); 	
	set_pgd(cur->mm->pgd);

	safestrcpy(cur->name, "initusr", sizeof(cur->name));
	cur->cwd = namei("/");
	BUG_ON(!cur->cwd); 
    release(&cur->mm->lock);

    // (from xv6) File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);
#ifdef CONFIG_FAT        
	/* STUDENT_TODO: your code here */
#endif    
	
	return 0;
}

static int lastpid=0; // a hint for the next free tcb slot. slowdown pid reuse for dbg ease

/* For creating both user and kernel tasks

    return pid on success, <0 on err

    clone_flags: PF_KTHREAD for kernel thread, PF_UTHREAD for user thread
    fn: task func entry. only matters for PF_KTHREAD. 
    arg: arg to kernel thread; or stack (userva) for user thread
    name: to be copied to task->name[]. if null, copy parent's name 
*/
//quest: music player
int copy_process(unsigned long clone_flags, unsigned long fn, unsigned long arg,
    const char *name) {
	struct task_struct *p = 0, *cur=myproc(); 
    int i, pid; 

	acquire(&sched_lock);	
	// find an empty tcb slot
	for (i = 0; i < NR_TASKS; i++) {
        pid = (lastpid+1+i) % NR_TASKS; 
		p = task[pid]; BUG_ON(!p); 
		if (p->state == TASK_UNUSED)
			{V("alloc pid %d", pid); lastpid=pid; break;}
	}
	if (i == NR_TASKS) 
		{release(&sched_lock); return -1;}

	memset(p, 0, sizeof(struct task_struct));
	initlock(&p->lock, "proc");

	acquire(&p->lock);	
    acquire(&cur->lock);	

	struct trapframe *childregs = task_pt_regs(p);

	if (clone_flags & PF_KTHREAD) { // to create a kernel task...
		p->cpu_context.x19 = fn;
		p->cpu_context.x20 = arg;
    } else { // to create a user task...
        struct trapframe *cur_regs = task_pt_regs(cur);
        *childregs = *cur_regs; // copy over the entire trapframe
        childregs->regs[0] = 0; // fork()'s return value for child 
        if (clone_flags & PF_UTHREAD) {	// fork a "thread", i.e. sharing an existing mm
            p->mm = 0; /* STUDENT_TODO: replace this */
            __atomic_add_fetch(&p->mm->ref, 1, __ATOMIC_SEQ_CST);
            childregs->sp = 0; /* STUDENT_TODO: replace this */
            W("childregs->sp %lx", childregs->sp);
            // same pc 
        } else {	// fork a "process", having a mm of its own
            struct mm_struct *mm = alloc_mm();
            if (!mm) {BUG(); return -1;}  // XXX reverse task allocation
			// now we hold mm->lock
            p->mm = mm; V("new mm %lx", (unsigned long)mm); 
            dup_current_virt_memory(mm); // duplicate virt memory (inc contents)
            release(&mm->lock);
            // same pc, same sp
        }

		/* dup fds, so that parent/child share same set of fds. increment
		reference counts on opened file descriptors. (user task only; kernel
		tasks won't need them) */
		for (int i = 0; i < NOFILE; i++)
			if (cur->ofile[i])
				p->ofile[i] = filedup(cur->ofile[i]);
		p->cwd = idup(cur->cwd);		
    }

    // also inherit task name
    if (name)
        safestrcpy(p->name, name, sizeof(p->name));
    else 
	    safestrcpy(p->name, cur->name, sizeof(cur->name));

	p->flags = clone_flags;
	p->credits = p->priority = cur->priority;
	p->pid = pid; 

	// TODO: init more field here
	// @page is 0-filled, many fields (e.g. mm.pgd) are implicitly init'd

    // prep new task's scheduler context 
	p->cpu_context.pc = (unsigned long)ret_from_fork; // entry.S
	p->cpu_context.sp = (unsigned long)childregs; // XXX in fact a kernel task (PF_KTHREAD) can use all the way from the top of the stack page...

    /* For the benefit of the unwinder, set up childregs->stackframe
	   as the final frame for the new task. (Per aarch64 ABI, fp points to
       the current "stackframe" (frame record)). Unwinding will stop here. */
	p->cpu_context.fp = (unsigned long)childregs->stackframe;
	
    release(&cur->lock);
	release(&p->lock);

 	p->parent = cur;
	// the last thing ... hence scheduler can pick up
	p->state = TASK_RUNNABLE;

	release(&sched_lock);

	return pid;
}
