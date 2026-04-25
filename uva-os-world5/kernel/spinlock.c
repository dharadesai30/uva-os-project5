// Mutual exclusion spin locks.
// derived from xv6

// if we do UP, the spinlock really boils down to interrupt off/on
// I left most spinning code here just in case. 
// besides, the push_off/pop_off code also coudl be useful. 

// cpu cache and memory attributes must be configured (cacheable, shareable)
// otherwise exclusive load/str instructions (e.g. ldxr) will throw memory 
// exception. cf: https://forums.raspberrypi.com/viewtopic.php?t=207173

#include "utils.h"
#include "spinlock.h"
#include "sched.h"

// #define SPINLOCK_DEBUG 1  

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void
acquire(struct spinlock *lk)
{
#if SPINLOCK_DEBUG
  volatile long cnt=0; 
#endif

  // ex code for debugging deadlock
  // if (lk->name[0]=='s' && lk->name[1]=='c' && current->pid==3)
  //   W("pid %d acquire %lx %s", current->pid, (unsigned long)lk, lk->name);
  
  push_off(); // disable interrupts to avoid deadlock.
  if(!lk || holding(lk))
    {printf("spinlock %s ", lk->name); panic("acquire");}

  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
  {
#if SPINLOCK_DEBUG
    if (cnt++>1000*602409 /* about 10 secs on real hw */) {
      E("acquire takes too long. deadlock? %s", lk->name); 
      show_stack(myproc(),"");
      procdump();
      BUG();
    }
#endif
    ;
  }

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen strictly after the lock is acquired.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();

  // Record info about lock acquisition for holding() and debugging.
  lk->cpu = mycpu();
}

// Release the lock.
void
release(struct spinlock *lk)
{
  // ex code for debugging deadlock
  // if (lk->name[0]=='s' && lk->name[1]=='c' && current->pid==3)
  //   W("pid %d rls %lx %s", current->pid, (unsigned long)lk, lk->name);

  if(!lk || !holding(lk))
    {printf("%s ", lk->name); panic("release");}

  lk->cpu = 0;

  // Tell the C compiler and the CPU to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released,
  // and that loads in the critical section occur strictly before
  // the lock is released.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked);

  pop_off();
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
int
holding(struct spinlock *lk)
{
  int r;
  // W("%lx %s %d", (unsigned long)lk, lk->name, lk->locked);
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.
// 
// fxl: "intena" is the irq status (on/off) when noff (i.e. the "balance") is 0. 
// hence, the irq status must be restored when noff reaches 0 again
void
push_off(void)
{
  int old = intr_get();

  // intr_off();
  disable_irq(); 
  if(mycpu()->noff == 0)
    mycpu()->intena = old;
  mycpu()->noff += 1;
}

// fxl: pop_off must be done with a positive counter (noff)
//  i.e. it's a bug if irq is already enabled and then pop_off
void
pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    panic("pop_off - interruptible");
  if(c->noff < 1)
    panic("pop_off");
  c->noff -= 1;
  if(c->noff == 0 && c->intena)
    // intr_on();
    enable_irq(); 
}

///////////////// Simple semaphore. as syscall ////////////////////////////
// Purpose: for user threads (created via clone()) to synchronize. vital for
// demonstrating SMP in userspace. Interface idea from Xinu OS which implements
// sem as syscalls The Linux futex() syscall seems too complicated. 
//
// Limitation: unsafe. each sem is a kernel-wide object. The userspace handle is
// the idx of this object. Thus, nothing prevents from unrelated (malicous) task
// to P()/V() the sem. A better design would be to bind a sem with the virt
// address space shared by the related tasks. Thus the kernel can check if a
// task is eligible to access a given sem.  (can be a project idea)

#define NSEM 32
static struct {
  // coarse grained lock (TBD: besides this, each sem has own spinlock)
  struct spinlock lock; 
  struct sem sem[NSEM];
} semtable; 

// maybe not needed, if we are confident bss is cleared..
void seminit(void) { 
  memzero(&semtable, sizeof(semtable));
}

// count: desired sem count 
// return: id of sem (sys-wide); -1 on err 
int sys_semcreate(int count) {
  int i; 
  acquire(&semtable.lock); 
  for (i = 0; i < NSEM; i++) {
    if (!semtable.sem[i].used) {
      semtable.sem[i].used = 1;
      semtable.sem[i].count = count; 
      break; 
    }
  }
  if (i == NSEM)
    i = -1;   
  release(&semtable.lock); 
  return i; 
}

// return: 0 on success; -1 on err 
//  if already freed, return 0
int sys_semfree(int sem) {
  if (sem<0 || sem>=NSEM)
    return -1; 
  acquire(&semtable.lock); 
  semtable.sem[sem].used = 0; 
  semtable.sem[sem].count = 0; 
  release(&semtable.lock); 
  return 0; 
}

// block calling task if sem.count==0, then sem.count--
// return: <0 on err; 0 on success
int sys_semp(int sem) {
  int ret; 
  if (sem<0 || sem>=NSEM)
    return -1; 

  acquire(&semtable.lock); 
  if (!semtable.sem[sem].used) {
    ret = -2; 
    goto out; 
  }

  while (semtable.sem[sem].count == 0) {
    sleep(&semtable.sem[sem], &semtable.lock); 
  }
  semtable.sem[sem].count --; BUG_ON(semtable.sem[sem].count < 0);
  ret = 0; 
out: 
  release(&semtable.lock); 
  return ret; 
}

// sem.count++ and wakeup all waiting tasks
// <0 on err; 0 on success
int sys_semv(int sem) {
  int ret; 
  if (sem<0 || sem>=NSEM)
    return -1; 

  acquire(&semtable.lock); 
  if (!semtable.sem[sem].used) {
    ret = -2; 
    goto out; 
  }

  BUG_ON(semtable.sem[sem].count < 0);

  semtable.sem[sem].count++; 
  wakeup(&semtable.sem[sem]);   
  ret = 0; 
out: 
  release(&semtable.lock); 
  return ret; 
}
