#ifndef	_UTILS_H
#define	_UTILS_H

#include <stdint.h>

#include "param.h"

// the kernel's HAL
#include "printf.h"

#ifdef PLAT_VIRT
#include "plat-virt.h"
#endif

#define __REG32(x)      (*((volatile uint32_t *)(x)))

struct buf; 
struct spinlock; 
struct sleeplock; 
struct file;
struct inode;
struct superblock;
struct stat; 
struct pipe; 
struct mm_struct; 
struct task_struct; 
struct fb_struct; 
// hw specific
struct sound_drv; 
struct sound_dev; 

// ------------------- misc ----------------------------- //

extern void delay (unsigned long cycles);   // utils.S
// extern void put32 ( unsigned long, unsigned int );
// extern unsigned int get32 ( unsigned long );
extern void put32(unsigned int *addr, unsigned int v);
extern unsigned int get32 (unsigned int *addr);
extern int get_el ( void );

void panic(char *s);  

// ------------------- uart ----------------------------- //
void uart_init (void);
char uart_recv ( void );
// void uart_send ( char c );
void putc ( void* p, char c );
// new apis, from xv6
void            uartintr(void);
void            uartputc(int);
void            uartputc_sync(int);
int             uartgetc(void);

// ------------------- timer ----------------------------- //
/* These are for "System Timer". See timer.c for details */
void sys_timer_init ( void );
void sys_timer_irq ( void );

// both busy spinning
void ms_delay(unsigned ms); 
void us_delay(unsigned us);

void current_time(unsigned *sec, unsigned *msec);

// kernel timers w/ callbacks, atop sys timer
typedef unsigned long TKernelTimerHandle;	// =idx in kernel table for timers 
typedef void TKernelTimerHandler (TKernelTimerHandle hTimer, void *pParam, void *pContext);

int ktimer_start(unsigned delayms, TKernelTimerHandler *handler, 
		void *para, void *context); 
int ktimer_cancel(int timerid);

/* below are for Arm generic timers */
void generic_timer_init ( void );
void handle_generic_timer_irq ( void );

extern struct spinlock tickslock;
extern unsigned int ticks; 
// ------------------- irq ---------------------------- //
void enable_interrupt_controller(int coreid); // irq.c 

// utils.S
void irq_vector_init( void );    
void enable_irq( void ); 
void disable_irq( void );
int is_irq_masked(void); 
/*return 1 if irq enabled, 0 otherwise*/
static inline int intr_get(void) {return 1-is_irq_masked();}; 
int cpuid(void);  // util.S must be called with irq disabled

// alloc.c 
unsigned int paging_init();
void *kalloc(); // Get a page. return: kernel va.
void kfree(void *p);    // p: kernel va.
unsigned long get_free_page();      // pa
void free_page(unsigned long p);    // pa 
int reserve_phys_region(unsigned long pa_start, unsigned long size); 
int free_phys_region(unsigned long pa_start, unsigned long size); 

void *malloc (unsigned s); 
void free (void *p); 
void dump_mem_info (void);

// ----------------  mm.c ---------------------- //
//src/n must be 8 bytes aligned   util.S
void memzero_aligned(void *src, unsigned long n);  
//dst/src/n must be 8 bytes aligned    util.S
void* memcpy_aligned(void* dst, const void* src, unsigned int n);

unsigned long *map_page(struct mm_struct *mm, unsigned long va, unsigned long page, int alloc, 
    unsigned long perm);
// int copy_virt_memory(struct task_struct *dst); // become dup_current_vir.. 
int dup_current_virt_memory(struct mm_struct *dstmm);
// void *allocate_user_page(struct task_struct *task, unsigned long va, unsigned long perm); 
void *allocate_user_page_mm(struct mm_struct *mm, unsigned long va, unsigned long perm);
void free_task_pages(struct mm_struct *mm, int useronly); 
unsigned long walkaddr(struct mm_struct *mm, unsigned long va);
unsigned long growproc (struct mm_struct *mm, int incr);

extern void set_pgd(unsigned long pgd);     // util.S
extern unsigned long get_pgd();

#define put32va(x,v)    put32(PA2VA(x), v)
#define get32va(x)      get32(PA2VA(x))

int             copyout(struct mm_struct *, uint64, char *, uint64);
int             copyin(struct mm_struct *, char *, uint64, uint64);
int             copyinstr(struct mm_struct *, char *, uint64, uint64);
int             either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int             either_copyin(void *dst, int user_src, uint64 src, uint64 len);

// ------------------- spinlock ---------------------------- //
void            acquire(struct spinlock*);
int             holding(struct spinlock*);
void            initlock(struct spinlock*, char*);
void            release(struct spinlock*);
void            push_off(void);
void            pop_off(void);

// ------------------- sleeplock ---------------------------- //
void            acquiresleep(struct sleeplock*);
void            releasesleep(struct sleeplock*);
int             holdingsleep(struct sleeplock*);
void            initsleeplock(struct sleeplock*, char*);

// ------------------- sched ---------------------------- //
void exit_process(int);
void sleep(void *, struct spinlock *);
int wait(uint64_t);
int wakeup(void *);
int killed(struct task_struct *p);
int kill(int pid);
void setkilled(struct task_struct *p);

// log.c
void            initlog(int, struct superblock*);
void            log_write(struct buf*); // fxl: replaces bwrite
void            begin_op(void);
void            end_op(void);

// bio.c buffer cache
void            binit(void);
struct buf*     bread(uint, uint);
void            brelse(struct buf*);
void            bwrite(struct buf*);
void            bpin(struct buf*);
void            bunpin(struct buf*);

// file.c
struct file*    filealloc(void);
void            fileclose(struct file*);
struct file*    filedup(struct file*);
void            fileinit(void);
int             fileread(struct file*, uint64, int n);
int             filestat(struct file*, uint64 addr);
int             filewrite(struct file*, uint64, int n);
int             filelseek(struct file*, int, int);  

// fs.c
void            fsinit(int);
int             dirlink(struct inode*, char*, uint);
struct inode*   dirlookup(struct inode*, char*, uint*);
struct inode*   ialloc(uint, short);
struct inode*   idup(struct inode*);
void            iinit();
void            ilock(struct inode*);
void            ilock_fat(struct inode*);
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
void            iupdate(struct inode*);
int             namecmp(const char*, const char*);
struct inode*   namei(char*);
struct inode*   namei_fat(char*);
struct inode*   nameiparent(char*, char*);
int             readi(struct inode* ip, int usr_dst, uint64 dst, uint off, uint n);
void            stati(struct inode*, struct stat*);
int             writei(struct inode*, int, uint64, uint, uint);
void            itrunc(struct inode*);
// path ex: /d/myfile   fatpath ex: 3:/myfile
int in_fatmount(const char *path);
int to_fatpath(const char *path, char *fatpath, int dev);

// string.c
int             memcmp(const void*, const void*, uint);
void*           memmove(void*, const void*, uint);
void*           memcpy(void*, const void*, uint);
void*           memset(void*, int, uint);
void            memzero(void*, uint); 
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strncmp(const char*, const char*, uint);
char*           strncpy(char*, const char*, int);
int atoi(const char *s); 
char* strchr(const char *s, char c);

//sys.c 
int             fetchstr(uint64, char*, int);
int             argstr(uint64 addr, char *buf, int max);
int             fetchaddr(uint64 addr, uint64 *ip);

// exec.c
int             exec(char*, char**);

// pipe.c
int             pipealloc(struct file**, struct file**);
void            pipeclose(struct pipe*, int);
int             piperead(struct pipe*, uint64, int);
int             pipewrite(struct pipe*, uint64, int);

// virtio_disk.c
void            virtio_disk_init(void);
void            virtio_disk_rw(struct buf *, int);
void            virtio_disk_intr(void);

// ramdisk.c
void            ramdisk_init(void);
void            ramdisk_rw(struct buf *, int);

// console.c
void            consoleinit(void);
void            consoleintr(int);
void            consputc(int);

// kb.c
int usbkb_init(void); 

void gamehat_gpio_init(void);

// sf.c
int start_sf(void);  // must be called from a task context

// stacktrace.c
void show_stack(struct task_struct *tsk, const char *loglvl);
void show_stack_user(void);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

// sched.c
extern void sched_init(void);
extern void yield(void);
extern void schedule(void);
extern void timer_tick(void);
extern void preempt_disable(void);
extern void preempt_enable(void);
extern void switch_to(struct task_struct* next);
extern void cpu_switch_to(struct task_struct* prev, struct task_struct* next);	// sched.S
void procdump(void); 

struct task_struct *myproc(void); 

int copy_process(unsigned long clone_flags, unsigned long fn, 
    unsigned long arg, const char *name);
int move_to_user_mode(unsigned long start, unsigned long size, unsigned long pc);
struct trapframe * task_pt_regs(struct task_struct *tsk);

// mbox.c
#define MAC_SIZE        6   // in bytes
int get_mac_addr(unsigned char buf[MAC_SIZE]);
int set_powerstate(unsigned deviceid, int on); 
int get_board_serial(unsigned long *s);

int fb_init(void); 
int fb_fini(void); 
int fb_set_voffsets(int offsetx, int offsety);

// sound.c
struct sound_drv * sound_init(unsigned nChunkSize /* 0=driver decide*/);
void sound_fini(struct sound_drv *drv);
void sound_playback (struct sound_drv *drv, 
            void	*pSoundData,		// sample rate 44100 Hz
            unsigned  nSamples,		// for Stereo the L/R samples are count as one
            unsigned  nChannels,		// 1 (Mono) or 2 (Stereo)
            unsigned  nBitsPerSample);
int sound_playback_active(struct sound_drv *drv);
int sound_write(struct sound_drv *drv, uint64 src, size_t nCount);
int sound_start(struct sound_drv *drv);
void sound_cancel(struct sound_drv *drv);

// sd.c
#define SD_OK                0
#define SD_TIMEOUT          -1
#define SD_ERROR            -2
int sd_init();
// read/write of whole sd card, no lock so mostly for testing
int sd_readblock(unsigned int lba, unsigned char *buffer, unsigned int num);
int sd_writeblock(unsigned char *buffer, unsigned int lba, unsigned int num);
// //  read/write of a sd "dev" (partition) (e.g. dev=SD_DEV0...)
// int sd_dev_readblock(int dev,
//     unsigned int lba, unsigned char *buffer, unsigned int num);
// int sd_part_writeblock(int dev, 
//     unsigned char *buffer, unsigned int lba, unsigned int num);
unsigned long sd_dev_get_sect_count(int dev);
int sd_dev_to_part(int dev); 

unsigned long sd_get_sect_size();

// kconsole.c
extern int kconsole_printf;
void kconsole_putc(char ch);
void kconsole_init(void); 

/*      useful macros       */
// linux
#define likely(exp)     __builtin_expect (!!(exp), 1)
#define unlikely(exp)   __builtin_expect (!!(exp), 0)

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)((char *)__mptr - __builtin_offsetof(type,member));})

static inline void warn_failed (const char *pExpr, const char *pFile, unsigned nLine) {
    printf("warning: %s at %s:%u\n", pExpr, pFile, nLine); 
}

#define assert(expr)    (likely (expr)        \
                            ? ((void) 0)           \
                            : assertion_failed (#expr, __FILE__, __LINE__))
#define BUG_ON(exp)	assert (!(exp))
#define BUG()		assert (0)

#define WARN_ON(expr)    (likely (!(expr))        \
                            ? ((void) 0)           \
                            : warn_failed (#expr, __FILE__, __LINE__))

#define MAX(a,b) ((a) > (b) ? a : b)
#define MIN(a,b) ((a) < (b) ? a : b)
#define ABS(x) ((x) < 0 ? -(x) : (x))

// debug.h
#include "debug.h"

// cache ops, util.S
void __asm_invalidate_dcache_range(void* start_addr, void* end_addr);
void __asm_flush_dcache_range(void* start_addr, void* end_addr);


#endif  /*_UTILS_H */
