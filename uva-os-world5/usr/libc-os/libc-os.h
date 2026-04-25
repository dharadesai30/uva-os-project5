// For non-standard OS interfaces, e.g. not part of libc
// referenced by user apps (c and cpp)

#ifndef _VOS_H
#define _VOS_H

#ifdef __cplusplus
extern "C" {
#endif
/////////////// uio.c
// USB keyboard 
enum{INVALID=0,KEYDOWN,KEYUP}; // evtype below
int read_kb_event(int events, int *evtype, unsigned *scancode);

// display config
// the field of /proc/dispinfo. order must be right
// check by "cat /proc/dispinfo"
enum{WIDTH=0,HEIGHT,VWIDTH,VHEIGHT,SWIDTH,SHEIGHT,
    PITCH,DEPTH,ISRGB,MAX_DISP_ARGS}; 
int config_fbctl(int w, int d, int vw, int vh, int offx, int offy);
int read_dispinfo(int dispinfo[MAX_DISP_ARGS], int *nargs);

int config_fbctl0(int cmd, int x, int y, int w, int h, int zorder, int trans);

#define MAX_NCPU 4
int read_cpuinfo(int util[MAX_NCPU], int *ncpus, int *is_qemu); 

// /proc/sbctl
struct sbctl_info {
    int id;
    int hw_fmt;
    int sample_rate; 
    int queue_sz; 
    int bytes_free; 
    int write_fmt;
    int write_channels; 
}; 
int read_sbctl(struct sbctl_info *cfg); 
int config_sbctl(int cmd, int arg1, int arg2, int arg3); 

// /proc/sbctl command, cf kernel/sound.c
enum {
  SB_CMD_FINI = 0, 
  SB_CMD_INIT, 
  SB_CMD_START, 
  SB_CMD_CANCEL, 
  SB_CMD_WR_FMT, 
  SB_CMD_TEST = 9
}; 

// from kernel/fcntl.h (not in newlib/libc header)
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

#define CLONE_VM	0x00000100		// linux/sched.h

// syscall.c
extern unsigned int uptime_ms(void);  
extern unsigned int msleep(unsigned int msec); 

// simple spinlock for user. same as ulib.c
struct spinlock_u {
    unsigned int locked; 
}; 
void spinlock_init(struct spinlock_u *lk); 
void spinlock_lock(struct spinlock_u *lk); 
void spinlock_unlock(struct spinlock_u *lk); 

// semaphores. see kernel sys_semcreate() for design comments
// direct call syscalls, as they have no libc wrappers
int sys_semcreate(int count); 
int sys_semfree(int id); 
int sys_semp(int id); // P()
int sys_semv(int id); // V()

// not in newlib/libc
// https://man7.org/linux/man-pages/man2/clone.2.html#NOTES
// return -1 on failure, otherwise pid (or 0)
int clone(int (*fn)(void *), void *stack, int flags, void *arg); 

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // _VOS_H