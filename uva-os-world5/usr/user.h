// kernel headers also included by user programs
#include "../kernel/param.h"        // only need types
#include "../kernel/stat.h" 
#include "../kernel/fcntl.h"
#include "../kernel/fs.h"
#include "../kernel/ff.h"  // for fat dir definitions; refactor this later

// misc
#define min(a, b) ((a) < (b) ? (a) : (b))

// system calls
// to add new syscalls:
//      add the prototype in this file below 
//      add stub. update usys.S (gen by usys.py; 
//      kernel: syscall.h 
//      kernel: sys.c update syscall table 
//      kernel: add the sys_XXX implementation

int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(const char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);        // return 0 on ok 
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int ms);
int uptime(void);      // return ms since boot
int lseek(int, int, int whence); // return -1 on failure. otherwise offset
// https://man7.org/linux/man-pages/man2/clone.2.html#NOTES
// return -1 on failure, otherwise pid (or 0)
int clone(int (*fn)(void *), void *stack, int flags, void *arg); 
// semaphores. see kernel sys_semcreate() for design comments
int semcreate(int count); 
int semfree(int id); 
int semp(int id); // P()
int semv(int id); // V()

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void fprintf(int, const char*, ...);
void printf(const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
unsigned atoi16(const char*); // fxl

// simplified spinlock for user
struct spinlock_u {
    unsigned int locked; 
}; 
void spinlock_init(struct spinlock_u *lk); 
void spinlock_lock(struct spinlock_u *lk); 
void spinlock_unlock(struct spinlock_u *lk); 

// tinyprintf.c, so far we only uses its sprintf()
void tfp_printf(char *fmt, ...);
void tfp_sprintf(char* s,char *fmt, ...);   // fxl: TODO const chat *fmt...
// void tfp_format(void* putp,void (*putf) (void*,char),char *fmt, va_list va);
// #define printf tfp_printf
#define sprintf tfp_sprintf

/////////////// uio.c

// display config
// the field of /proc/dispinfo. order must be right
// check by "cat /proc/dispinfo"
enum{WIDTH=0,HEIGHT,VWIDTH,VHEIGHT,SWIDTH,SHEIGHT,
    PITCH,DEPTH,ISRGB,MAX_DISP_ARGS}; 
int config_fbctl(int w, int d, int vw, int vh, int offx, int offy);
int config_fbctl0(int cmd, int x, int y, int w, int h, int zorder, int trans);
int read_dispinfo(int dispinfo[MAX_DISP_ARGS], int *nargs);

// /dev/fb
#define PIXELSIZE 4 /*ARGB, expected by /dev/fb*/ 
typedef unsigned int PIXEL; 

// USB keyboard 
enum{INVALID=0,KEYDOWN,KEYUP}; 
int read_kb_event(int events, int *evtype, unsigned *scancode); // uio.c

// scancode defs: 
// all the way to KEY_KPDOT, cf https://gist.github.com/MightyPork/6da26e382a7ad91b5496ee55fdc73db2  
// https://gist.github.com/MightyPork/6da26e382a7ad91b5496ee55fdc73db2
// also consistent with SDL's scancode def: 
// https://wiki.libsdl.org/SDL2/SDL_Scancode
#define NUM_SCANCODES   0x64 
#define KEY_A 0x04 // Keyboard a and A
#define KEY_B 0x05 // Keyboard b and B
#define KEY_C 0x06 // Keyboard c and C
#define KEY_D 0x07 // Keyboard d and D
#define KEY_E 0x08 // Keyboard e and E
#define KEY_F 0x09 // Keyboard f and F
#define KEY_G 0x0a // Keyboard g and G
#define KEY_H 0x0b // Keyboard h and H
#define KEY_I 0x0c // Keyboard i and I
#define KEY_J 0x0d // Keyboard j and J
#define KEY_K 0x0e // Keyboard k and K
#define KEY_L 0x0f // Keyboard l and L
#define KEY_M 0x10 // Keyboard m and M
#define KEY_N 0x11 // Keyboard n and N
#define KEY_O 0x12 // Keyboard o and O
#define KEY_P 0x13 // Keyboard p and P
#define KEY_Q 0x14 // Keyboard q and Q
#define KEY_R 0x15 // Keyboard r and R
#define KEY_S 0x16 // Keyboard s and S
#define KEY_T 0x17 // Keyboard t and T
#define KEY_U 0x18 // Keyboard u and U
#define KEY_V 0x19 // Keyboard v and V
#define KEY_W 0x1a // Keyboard w and W
#define KEY_X 0x1b // Keyboard x and X
#define KEY_Y 0x1c // Keyboard y and Y
#define KEY_Z 0x1d // Keyboard z and Z

#define KEY_1 0x1e // Keyboard 1 and !
#define KEY_2 0x1f // Keyboard 2 and @
#define KEY_3 0x20 // Keyboard 3 and #
#define KEY_4 0x21 // Keyboard 4 and $
#define KEY_5 0x22 // Keyboard 5 and %
#define KEY_6 0x23 // Keyboard 6 and ^
#define KEY_7 0x24 // Keyboard 7 and &
#define KEY_8 0x25 // Keyboard 8 and *
#define KEY_9 0x26 // Keyboard 9 and (
#define KEY_0 0x27 // Keyboard 0 and )

#define KEY_ENTER 0x28 // Keyboard Return (ENTER)
#define KEY_TAB 0x2b // Keyboard Tab

#define KEY_HOME 0x4a // Keyboard Home
#define KEY_PAGEUP 0x4b // Keyboard Page Up
#define KEY_DELETE 0x4c // Keyboard Delete Forward
#define KEY_END 0x4d // Keyboard End
#define KEY_PAGEDOWN 0x4e // Keyboard Page Down

#define KEY_RIGHT 0x4f // Keyboard Right Arrow
#define KEY_LEFT 0x50 // Keyboard Left Arrow
#define KEY_DOWN 0x51 // Keyboard Down Arrow
#define KEY_UP 0x52 // Keyboard Up Arrow



