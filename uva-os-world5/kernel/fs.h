#ifndef _FS_H
#define _FS_H

// On-disk file system format.
// Both the kernel and user programs use this header file.

#define ROOTINO  1   // root i-number

// #define BSIZE 1024  // block size
#define BSIZE 512  // block size
// fxl: sd card is 512, ramdisk 512, ... so code would be cleaner
// used to be a limiter of filesize w/o lv2 indirect links, but 
// w/ it, it's not longer a concern. 

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint magic;        // Must be FSMAGIC
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};

#define FSMAGIC 0x10203040

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT*NINDIRECT)       
// fxl: max # of blocks per file. ~270KB filesize limit with only 1 indirect ptr; 
// ~16MB with an additional doubly indirect ptr

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+2];   // Data block addresses, +1 indirect tpr, +1 doubly indirect ptr
  // 68 bytes above, see below
  char paddings[60]; 
}; 

// use this to check struct size at compile time
// https://stackoverflow.com/questions/11770451/what-is-the-meaning-of-attribute-packed-aligned4
// char (*__kaboom)[sizeof(struct dinode)] = 1; 

_Static_assert(BSIZE % sizeof(struct dinode) == 0);  // fxl, as needed by mkfs

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

#endif