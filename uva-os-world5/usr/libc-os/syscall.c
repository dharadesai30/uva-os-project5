// newlib glue.
// syscall expected by newlib
// b/c of newlib dependency, the build rules, path, lib differ from "bare" 
// xv6 user programs...

// cf: https://sourceware.org/newlib/libc.html#Syscalls
// https://github.com/NJU-ProjectN/navy-apps/blob/master/libs/libos/src/syscall.c
// (newlib) _syslist.h

#include <_ansi.h>
#include <sys/types.h>
#include <sys/stat.h>
// #include <sys/fcntl.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>
#include <errno.h>
#include <reent.h>
#include <assert.h>
// #include <signal.h>    // TBD
// #include <unistd.h>
#include <sys/wait.h>

#include "kernel/fcntl.h"

// typedef int pid_t;

// struct stat {
//   int dev;     // File system's disk device
//   unsigned int ino;    // Inode number
//   short type;  // Type of file
//   short nlink; // Number of links to file
//   unsigned long size; // Size of file in bytes
// };

// usys.S
int syscall_fork(void);
int syscall_exit(int) __attribute__((noreturn));
int syscall_wait(int*);
int syscall_pipe(int*);
int syscall_write(int, const void*, int);
int syscall_read(int, void*, int);
int syscall_close(int);
int syscall_kill(int);
int syscall_exec(const char*, char**);
int syscall_open(const char*, int);
int syscall_mknod(const char*, short, short);
int syscall_unlink(const char*);
int syscall_fstat(int fd, struct stat*);
int syscall_link(const char*, const char*);
int syscall_mkdir(const char*);    // defined in stat.h with 2nd para mode
int syscall_chdir(const char*);
int syscall_dup(int);
int syscall_getpid(void);
char* syscall_sbrk(int);
int syscall_sleep(int);
int syscall_uptime(void);
int syscall_lseek(int, int, int whence); // return -1 on failure. otherwise offset

void _exit(int status) {
  syscall_exit(status); 
}

// fxl: mode is ignored... relatd to permissions
// cf: https://man7.org/linux/man-pages/man2/open.2.html e.g. S_IRWXU  S_IRUSR  
int _open(const char *path, int flags, mode_t mode) {
  return syscall_open(path, flags); 
}

int _write(int fd, void *buf, size_t count) {
  return syscall_write(fd, buf, count); 
}

void *_sbrk(intptr_t increment) {
  return syscall_sbrk(increment); 
}

int _read(int fd, void *buf, size_t count) {
  return syscall_read(fd, buf, count); 
}

int _close(int fd) {
  return syscall_close(fd); 
}

off_t _lseek(int fd, off_t offset, int whence) {
  return syscall_lseek(fd, offset, whence); 
}

int _gettimeofday(struct timeval *tv, struct timezone *tz) {
  // TBD
  return 0;
}

// fxl: envp ignored 
int _execve(const char *fname, char * const argv[], char *const envp[]) {
  return syscall_exec(fname, (char **)argv); 
}

// our stat (kernel/stat.h) different from libc's stat
// fxl: XXX conversion
int _fstat(int fd, struct stat *buf) {
  return syscall_fstat(fd, buf); 
}

// Status of a file (by name). we can do it. TBD XXX note the different def of
// "stat" cf _fstat() above
int _stat(const char *fname, struct stat *st) {
  int fd, ret = -1;
  if ((fd = syscall_open(fname, O_RDONLY))) {
    if (syscall_fstat(fd, st) == 0)
      ret = 0;     
    syscall_close(fd); 
  }   
  return ret;
}

int _kill(int pid, int sig) {
  // XXX sig must == 9?
  return syscall_kill(pid); 
}

pid_t _getpid() {
  return syscall_getpid(); 
}

pid_t _fork() {
  return syscall_fork(); 
}

// needed for thread?? unsupported
// pid_t vfork() {
//   assert(0);
//   return -1;
// }

int _link(const char *d, const char *n) {
  return syscall_link(d, n); 
}

int _unlink(const char *n) {
  return syscall_unlink(n); 
}

pid_t _wait(int *status) {
  return syscall_wait(status); 
}

// Timing information for current process. TBD???
clock_t _times(void *buf) {
  assert(0);
  return 0;
}

// Query whether output stream is a terminal
int _isatty(int file) {
  return 1;     // TBD 
}

// newlib no pipe() def??
int pipe(int pipefd[2]) {
  return syscall_pipe(pipefd); 
}

// newlib lacks this. only _mkdir_r which calls _mkdir()
int mkdir(const char *path, mode_t mode) {
  // ignore "mode"
  return syscall_mkdir(path); 
}

// not a standard c func or syscall
int uptime(void) {
  return syscall_uptime();
}

unsigned int uptime_ms(void) {
  return syscall_uptime();
}

// int dup(int oldfd) {
//   assert(0);
//   return -1;
// }

// int dup2(int oldfd, int newfd) {
//   return -1;
// }

// only in libc/posix
// unsigned int sleep(unsigned int seconds) {
//   assert(0);
//   return -1;
// }

// needed for SDL/doom
// return 0 on success
unsigned int msleep(unsigned int ms) {
  syscall_sleep(ms); 
  return 0; 
}

int mknod(const char* path, short major, short minor) {
  return syscall_mknod(path, major, minor);
}

int exec(const char* path, char** argv) {
  return syscall_exec(path, argv); 
}

int chdir(const char* dir) {
  return syscall_chdir(dir);
}

int dup(int fd) {
  return syscall_dup(fd);
}

// ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) {
//   assert(0);
//   return -1;
// }

// int symlink(const char *target, const char *linkpath) {
//   assert(0);
//   return -1;
// }

// int ioctl(int fd, unsigned long request, ...) {
//   return -1;
// }