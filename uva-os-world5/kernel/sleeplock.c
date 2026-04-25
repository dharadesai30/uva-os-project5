// Sleeping locks
// fxl: derived from xv6-riscv

#include "utils.h"
#include "spinlock.h"
#include "sched.h"
#include "sleeplock.h"

void initsleeplock(struct sleeplock *lk, char *name) {
    initlock(&lk->lk, "sleep lock");
    lk->name = name;
    lk->locked = 0;
    lk->pid = 0;
}

void acquiresleep(struct sleeplock *lk) {
    acquire(&lk->lk);
    // fxl: @lk->locked: if it's already locked by others. a flag must be protected with spinlock
    while (lk->locked) {
        sleep(lk, &lk->lk);
    }
    // fxl: w spinlock held, now claim the sleeplock
    lk->locked = 1;
    lk->pid = myproc()->pid;
    release(&lk->lk);
}

void releasesleep(struct sleeplock *lk) {
    acquire(&lk->lk);
    lk->locked = 0;
    lk->pid = 0;
    wakeup(lk);
    release(&lk->lk);
}

int holdingsleep(struct sleeplock *lk) {
    int r;

    acquire(&lk->lk);
    r = lk->locked && (lk->pid == myproc()->pid);
    release(&lk->lk);
    return r;
}
