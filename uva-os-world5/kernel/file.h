#include "sleeplock.h"
#include "fs.h"

// from xv6-riscv 
// a vfs layer
struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE, FD_PROCFS, FD_INODE_FAT } type; 
  int ref; // reference count
  char readable;
  char writable;
  char nonblocking; // FD_DEVICE only
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE (& FD_PROCFS)
  uint off;          // FD_INODE and FD_INODE_FAT (needed for lseek)
  short major;       // FD_DEVICE (& FD_PROCFS)

  // kernel-side info associated with the file
  // e.g. drv desc, procfs content. allocated & generated when the file is 
  // open()'d; freed when the file is close()'d. 
  // 1 PAGE for FD_PROCFS or FD_DEVICE      
  void *content;
};
// fxl: no lock for a struct file, b/c it's task private.
// there is a global lock for ftable, for allocating/freeing file slots

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

#ifdef CONFIG_FAT
struct FIL;
typedef struct FIL FIL;
struct DIR;
typedef struct DIR DIR;
#endif

// in-memory copy of an inode
// fxl: NB inode has "dev", so inum is per-device; 
struct inode {
  uint dev;           // Device number  (fxl: disk no, cf ROOTDEV, not major/minor num)
  uint inum;          // Inode number  (fxl: idx of on-disk inode array, not in-mem
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here (fxl: above protected by itable spinlock)
  int valid;          // inode has been read from disk?

#ifdef CONFIG_FAT
  // #define INUM_INVALID  (uint)(-1)  
  FIL *fatfp;
  DIR *fatdir; 
  // a dirty hack, b/c fp will be kernel va (linear mapping), so we are fine <4GB
  // XXX should use filename instead???
  // #define fp_to_inum(fp) (uint)(fp)  
#endif  

  // copy of disk inode 
  short type;         // cf stat.h
  short major;
  short minor;
  short nlink;
  uint size;    // fat: loaded once when open()
  uint addrs[NDIRECT+2]; 
};

// map major device number to device functions.
struct devsw {  
  int (*read)(int /*usrdst?*/, uint64 /*usrptr*/, 
    int /*off*/, int /*sz*/, char /*blocking?*/, void *content /*file-specific info*/);
  int (*write)(int, uint64, int, int, void *);
};

extern struct devsw devsw[];