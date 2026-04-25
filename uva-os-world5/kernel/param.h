// included by both the kernel (c and asm) and mkfs

// -------------- configuration -------------------------- //
#define NOFILE          16  // open files per process 
#define NCPU	        1   // # of cpu cores /* STUDENT_TODO: replace this */
#define MAXPATH         128   // maximum file path name
#define NINODE          50  // maximum number of active i-nodes
// #define NDEV            10  // maximum major device number
#define MAXARG       32  // max exec arguments
#define NFILE       100  // open files per system
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes fxl:too small?
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define NR_TASKS				32   // 128     // 32 should be fine. usertests.c seems to expect > 100
#define NR_MMS              NR_TASKS

// on boot, redirect kernel's printf() and user's console write to fb console
// ("kconsole") this is in addition to the uart output.
// to avoid kconsole interfere with app UI, kconsole will be deactivated once fb_init() gets called for a second time
// #define KCONSOLE_PRINTF        1        // for demo, experimental

#define DEV_RAMDISK     1       // ramdisk
#define DEV_VIRTDISK    2       // qemu's virtio device
// below is for sd card (phys or emulated)
#define DEV_SD0          3       // partition0
#define DEV_SD1          4       // partition1
#define DEV_SD2          5       // partition2
#define DEV_SD3          6       // partition3
#define is_sddev(dev) (dev>=DEV_SD0 && dev<=DEV_SD3)

// device number of file system root disk NB: this is block disk id, not major/minor
#define ROOTDEV       DEV_RAMDISK  
#if defined(PLAT_RPI3QEMU)
#define SECONDDEV     DEV_SD0       // secondary dev to mount under /d/
#elif defined(PLAT_RPI3)
#define SECONDDEV     DEV_SD1       // secondary dev to mount under /d/
#endif
    
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define FSSIZE       15000 // 2000  // size of file system in blocks
// (32 * 1024)  ok, but results in a very large ramdisk...

// the malloc()/free() region which is carved out from the paging area
// #define MALLOC_PAGES  (16*1024*1024 / PAGE_SIZE)  
#define MALLOC_PAGES  (8*1024*1024 / PAGE_SIZE)  

// Large user vm. but usertests slow b/c freecount()
#define USER_VA_END         (128 * 1024 * 1024) // == user stack top, 128MB user va
#define USER_MAX_STACK      (1 * 1024 * 1024)  // in bytes, must be page aligned. 
#define MAX_TASK_USER_PAGES		(USER_VA_END / PAGE_SIZE)      // max userpages per task, 
#define MAX_TASK_KER_PAGES       96      //max kernel pages per task. (128 seems to corrupt kernel stack

// Small user vm (enough for: userstests,slider,etc. OOM for: liteNes etc)
// #define USER_VA_END         (4 * 1024 * 1024) // == user stack top
// #define USER_MAX_STACK      (1 * 1024 * 1024)  // in bytes, must be page aligned. 
// #define MAX_TASK_USER_PAGES		(USER_VA_END / PAGE_SIZE)      // max userpages per task, 
// #define MAX_TASK_KER_PAGES      16       //max kernel pages per task. 

// keep them here b/c needed by lots of files. moved from mmu.h. 
#define VA_START 			0xffff000000000000
#define VA2PA(x) ((unsigned long)x - VA_START)          // kernel va to pa
#define PA2VA(x) ((void *)((unsigned long)x + VA_START))  // pa to kernel va

#ifndef __ASSEMBLER__
// keep xv6 code happy. TODO: separate them out...
typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int  uint32;
typedef unsigned long uint64;
#endif