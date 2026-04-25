// #define K2_DEBUG_VERBOSE
#define K2_DEBUG_WARN

#include "plat.h"
#include "utils.h"
#include "sched.h"
#include "fcntl.h"
#include "syscall.h"
#include "mmu.h"

// in-kernel syscall impl
// we have a simplified mechanism getting syscall arguments & return values.

/*
	assumption 1: from kernel entry to sys_XXX, x0-x8 are untouched. 
	this allows syscalls to read args direcly 

	assumption 2: the user's va is mapped during the exec of the syscall.
	(and the user pointers are not used in irq context, in which the user
	va may not be mapped)	

	in general we shall not trust the user pointers passed to syscalls.  
	hence, the kernel must check user pointers, ie walking the backing pagetables 
	and copying contents to kernel va). 
	This allows the kernel to catch invalid user buffers and reject it as needed. 
	(^^ This does not avoid TOCTOU problems, which are a different topic)

*/ 

// copy to/from userspace: although the code is there (mm.c) and is in use, 
// it is not needed. 

// invoked from el0_svc (entry.S)
// x0..x9 were intact since kernel entry (svc). hence they can be directly used as arguments
// upon return, ret_from_syscall (entry.S) will copy x0 to trapframe, as return value to user

// Fetch the uint64 at addr from the current process.
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct task_struct *p = myproc();
  // fxl: maybe check against brk and stack bottom? our task memlayout differs TODO:
//   if(addr >= p->sz || addr+sizeof(uint64) > p->sz) // both tests needed, in case of overflow
//     return -1;
  if(copyin(p->mm, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int
fetchstr(uint64 addr, char *buf, int max)
{  
  if(copyinstr(myproc()->mm, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

int argstr(uint64 addr, char *buf, int max) {return fetchstr(addr, buf, max);};

int sys_fork(void) {
	return copy_process(0 /*clone_flags*/, 0 /*fn*/, 0 /*arg*/, 0/*inherit name*/);
}

// https://man7.org/linux/man-pages/man2/clone.2.html
// ignore parent_tid/tls/child_tid
// return -1 on failure, otherwise child pid
//quest: music player
long sys_clone(unsigned long flags, unsigned long userstack,
                     unsigned long parent_tid, unsigned long usertls,
                     unsigned long child_tid) {
	if (flags != CLONE_VM) return -1; 
	 
	/* STUDENT_TODO: your code here */

	return copy_process(PF_UTHREAD, 0, 0, 0); /* STUDENT_TODO: replace this */
}

int sys_exit(int c){
	I("exit called. pid %d code %d", myproc()->pid, c);
	exit_process(c);
	return 0; 
}

int sys_wait(unsigned long p /* user va*/) {	
	return wait(p); 
}

unsigned long sys_sbrk(int incr) {
	unsigned long sz, sz0; 
	struct trapframe *regs = task_pt_regs(myproc());
	
	acquire(&myproc()->mm->lock); 
	sz = myproc()->mm->sz; sz0=sz;  
	
	V("cal sys_sbrk, requestsed brk %lu", sz + incr);
	// all kinds of checks. careful: sz is unsigned; incr is signed
	if (incr < 0 && sz + incr < myproc()->mm->codesz) {
		W("sys_sbrk failed: requested brk too small. into code/data region"); 
		goto bad; 
	}
	/* NB: for multithreading case, sp can be anywhere, e.g. a global array. 
	 so we react iff old sz is below sp and new sz approches or exceeds sp */
	if (incr > 0 && sz < regs->sp && sz + incr > regs->sp - PAGE_SIZE) {
		W("sys_sbrk failed: requested brk %lx too large. too close to sp %lx", 
			sz+incr, regs->sp); 
		goto bad; 
	} 
	if (incr == 0) { // shortcut 
		V("pid %d user_pages_count %d", myproc()->pid, myproc()->mm->user_pages_count);
		release(&myproc()->mm->lock); return sz;
	}

	sz = growproc(myproc()->mm, incr);
	release(&myproc()->mm->lock);
	if (sz == (unsigned long)-1) 
		W("sys_sbrk failed. requestsed brk %lx", sz0 + incr); 
	return sz; 
bad:
	release(&myproc()->mm->lock);
	return (unsigned long)(void *)-1; 	
}

int sys_kill(int pid) {
	return kill(pid); 
}

int sys_getpid(void) {
	return myproc()->pid; 
}

// UP sys_sleep(), based on sched ticks (simple but inefficient)
// cf: sys_sleep in timer.c for a ktimer based design
// n: # of sched ticks to sleep (cf timer.c)
// NB: 
// 1. this is NOT sleep() in sched.c
// 2. in current design, each timer irq will wakeup() "ticks" (i.e. signal it); 
// this however, does not necessarily result in schedule() at each timer irq. 
// this is b/c wakeup() only change p->state. it is up to the next schedule()
// invocation to pick up the woken up task for running. 
#if 0
int sys_sleep(int n) {
	uint ticks0;
    acquire(&tickslock);
    ticks0 = ticks;
    while (ticks - ticks0 < n) {
        if (killed(myproc())) {
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}
#endif

// implementation based on sched ticks. 
// return: # of sched ticks
#if 0
int sys_uptime(void) {
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
    return (int)xticks;		// fxl: loss of precision?
}
#endif

// return: # of ms
int sys_uptime(void) {
    uint sec, msec; 
	current_time(&sec, &msec); 
    return (int)(sec * 1000 + msec);  // fxl: loss of precision ...
}

// sysfile.c
extern int sys_dup(int fd);
extern int sys_read(int fd, unsigned long p /*user va*/, int n);
extern int sys_write(int fd, unsigned long p /*user va*/, int n);
extern int sys_lseek(int fd, int offset, int whence); 
extern int sys_close(int fd); 
extern int sys_fstat(int fd, unsigned long st /*user va*/);
extern int sys_link(unsigned long userold, unsigned long usernew);
extern int sys_unlink(unsigned long upath /*user va*/);
extern int sys_open(unsigned long upath, int omode);
extern int sys_mkdir(unsigned long upath);
extern int sys_mknod(unsigned long upath, short major, short minor);
extern int sys_chdir(unsigned long upath);
extern int sys_exec(unsigned long upath, unsigned long uargv);
extern int sys_pipe(unsigned long fdarray);

extern int sys_fork();		// sys.c
extern long sys_clone(unsigned long flags, unsigned long userstack,
                     unsigned long parent_tid, unsigned long usertls,
                     unsigned long child_tid); // sys.c
// spinlock.c					 
extern int sys_semcreate(int count); 
extern int sys_semfree(int sem); 
extern int sys_semp(int sem); 
extern int sys_semv(int sem); 

extern unsigned long sys_sbrk(int incr); // mm.c
extern int sys_sleep(int ms); // timer.c

/* An array of pointers to all syscall handlers. 
	Each syscall has a "syscall number" (sys.h) — which is just an index in this array 		
*/
//quest: music player
void * const sys_call_table[] = {
	[SYS_fork]    sys_fork,
	[SYS_exit]    sys_exit,
	[SYS_wait]    sys_wait,
	[SYS_pipe]    sys_pipe,
	[SYS_read]    sys_read,
	[SYS_kill]    sys_kill,
	[SYS_exec]    sys_exec,
	[SYS_fstat]   sys_fstat,
	[SYS_chdir]   sys_chdir,
	[SYS_dup]     sys_dup,
	[SYS_getpid]  sys_getpid,
	[SYS_sbrk]    sys_sbrk,
	[SYS_sleep]   sys_sleep,
	[SYS_uptime]  sys_uptime,
	[SYS_open]    sys_open,
	[SYS_write]   sys_write,
	[SYS_mknod]   sys_mknod,
	[SYS_unlink]  sys_unlink,
	[SYS_link]    sys_link,
	[SYS_mkdir]   sys_mkdir,
	[SYS_close]   sys_close,	
	[SYS_lseek]   sys_lseek,	
	[SYS_clone]   0, /* STUDENT_TODO: replace this */
	[SYS_semcreate]   sys_semcreate,
	[SYS_semfree]   sys_semfree,
	[SYS_semp]   sys_semp,
	[SYS_semv]   sys_semv,
};
