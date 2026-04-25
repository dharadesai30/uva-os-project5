#ifndef _SLEEP_LOCK_H
#define _SLEEP_LOCK_H
// Long-term locks for processes
struct sleeplock {
  unsigned int locked;       // Is the lock held?
  struct spinlock lk; // spinlock protecting this sleep lock
  
  // For debugging:
  char *name;        // Name of lock.
  int pid;           // Process holding lock
};
#endif
