#ifndef _FCNTL_H
#define _FCNTL_H

// also included by user source
// NB: these names collide with libc, but the values are the same....
// cf: libc/include/sys/_default_fcntl.h
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400
#define O_NONBLOCK 0x4000     // apr 2024: only for /dev/XXX
#define O_BINARY   0x10000    //  passed in by doom.. ignore it as of now

#define	SEEK_SET	0	/* set file offset to offset */
#define	SEEK_CUR	1	/* set file offset to current plus offset */
#define	SEEK_END	2	/* set file offset to EOF plus offset */

#define CLONE_VM	0x00000100		// linux/sched.h

// major num for devices
// modeled after: https://github.com/NJU-ProjectN/navy-apps/blob/master/README.md
// TODO: make procfs a seperate file type (not just a major dev number...)
enum {
  CONSOLE = 1,
  KEYBOARD,
  KEYBOARD0,        // events dispatched per task
  FRAMEBUFFER,      // /dev/fb
  FRAMEBUFFER0,     // a surface
  DEVNULL,          // /dev/null
  DEVZERO,          // /dev/zero
  DEVSB,            // /dev/sb, sound buffer
  // procfs below
  PROCFS_DISPINFO,
  PROCFS_CPUINFO,
  PROCFS_MEMINFO,
  PROCFS_FBCTL,
  PROCFS_FBCTL0,    // configs a task's surface or the surface flinger
  PROCFS_SBCTL,
  NDEV              // max
};

// procfs, devfs
struct proc_dev_info {
  char type;
#define TYPE_DEVFS  0  
#define TYPE_PROCFS  1
  int major;
  const char * path; // 0 no specific path
}; 

// procfs/devfs lookup table. define as macro here so that it can be included
// in both user (sh creates them) & kernel. the macro is only instantiated in specific files 
// as needed. 
// quest: desktop
#define PROC_DEV_TABLE  \
static struct proc_dev_info pdi[] =            \
{                                       \
  {.type = TYPE_DEVFS, .major = CONSOLE, .path = "/dev/console"}, \
  {.type = TYPE_DEVFS, .major = KEYBOARD, .path = "/dev/events"},  \
  {.type = TYPE_DEVFS, .major = KEYBOARD0, .path = "/dev/events0"},  \
  {.type = TYPE_DEVFS, .major = FRAMEBUFFER, .path = "/dev/fb"},\
  {.type = TYPE_DEVFS, .major = FRAMEBUFFER0, .path = "/dev/fb0"},\
  {.type = TYPE_DEVFS, .major = DEVNULL, .path = "/dev/null"}, \
  {.type = TYPE_DEVFS, .major = DEVZERO, .path = "/dev/zero"}, \
  {.type = TYPE_DEVFS, .major = DEVSB, .path = "/dev/sb"}, \
  {.type = TYPE_PROCFS, .major = PROCFS_DISPINFO, .path = "/proc/dispinfo"}, \
  {.type = TYPE_PROCFS, .major = PROCFS_CPUINFO, .path = "/proc/cpuinfo"},   \
  {.type = TYPE_PROCFS, .major = PROCFS_MEMINFO, .path = "/proc/meminfo"},   \
  {.type = TYPE_PROCFS, .major = PROCFS_FBCTL, .path = "/proc/fbctl"},       \
  {.type = TYPE_PROCFS, .major = PROCFS_FBCTL0, .path = "/proc/fbctl0"},       \
  {.type = TYPE_PROCFS, .major = PROCFS_SBCTL, .path = "/proc/sbctl"},        \
};

// max # of args passed to the kernel, via writing to a procfs
#define PROCFS_MAX_ARGS   8

// /proc/sbctl command, cf kernel/sound.c
enum {
  SB_CMD_FINI = 0, 
  SB_CMD_INIT, 
  SB_CMD_START, 
  SB_CMD_CANCEL, 
  SB_CMD_WR_FMT, 
  SB_CMD_TEST = 9
}; 

// /proc/fbctl0 command cf kernel/sf.c
enum {
  FB0_CMD_FINI = 0, 
  FB0_CMD_INIT, 
  FB0_CMD_CONFIG, 
  FB0_CMD_TEST = 9
}; 

#define ZORDER_TOP          0
#define ZORDER_BOTTOM       -1
#define ZORDER_INC          2   // zorder+=1
#define ZORDER_DEC          3   // zorder-=1
#define ZORDER_UNCHANGED    4   // zorder-=1
#define ZORDER_FLOATING     5   // always top, rendered after all "normal" surfaces


#endif