#define K2_DEBUG_WARN
// #define K2_DEBUG_INFO

// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.  
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

// fxl: the actual fs implementation, with interfaces with disk. almost no change

#include "utils.h"
#include "stat.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"
#include "sched.h"

#ifdef CONFIG_FAT
#include "ff.h"
static FATFS fatfs; 
#endif

#define min(a, b) ((a) < (b) ? (a) : (b))
// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb; 

// Read the super block.
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Init fs
void
fsinit(int dev) {
#ifdef CONFIG_FAT
  // cf: http://elm-chan.org/fsw/ff/doc/filename.html
  char devstr[32]; int ret; 
  if (dev == SECONDDEV) {
    snprintf(devstr, 32, "%d:", dev); 
    if ((ret=f_mount(&fatfs, devstr, 1 /*mount now*/)) != FR_OK)
      {E("devstr %s ret %d", devstr, ret); BUG();}
    if ((ret=f_chdrive(devstr)) != FR_OK) // must do this, otherwise f_chdir(..) default to vol 0
      {E("f_chdrive failed"); BUG();}
    return; 
  }
#endif
  readsb(dev, &sb);
  if(sb.magic != FSMAGIC)
    panic("invalid file system");
  initlog(dev, &sb);  
  I("fsinit done");
}

//// fxl: block mgmt (see bio.c for buffer cache

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
// returns 0 if out of disk space.
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  printf("balloc: out of blocks\n");
  return 0;
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at block
// sb.inodestart. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a table of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The in-memory
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in table: an entry in the inode table
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a table entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   table entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays in the table and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The itable.lock spin-lock protects the allocation of itable
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold itable.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.
// (fxl: above: good lock designs that can be used, e.g. for sound drivers
struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} itable;

void
iinit()
{
  int i = 0;
  
  initlock(&itable.lock, "itable");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&itable.inode[i].lock, "inode");
  }
}

static struct inode* iget(uint dev, uint inum);

// Allocate an inode on device dev.
// Mark it as allocated by  giving it type type.
// Returns an unlocked but allocated and referenced inode,
// or NULL if there is no free inode.
// fxl: type is T_xx (T_DIR, T_FILE.. cf stat.h)
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;
  
  BUG_ON(is_fattype(type));   // fat wont have disk inodes
  
    // fxl: read a block of inodes at a time, go through them all..
    //    rely on lock inside bread()/brelse()
  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));      
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  printf("ialloc: no inodes\n");
  return 0;
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk.
// Caller must hold ip->lock.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  BUG_ON(is_fattype(ip->type));   // fat wont have disk inodes

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
// fxl: but incr its ref cnt so the inode wont go away....
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&itable.lock);

  // Is the inode already in the table?
  empty = 0;
  for(ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&itable.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode entry. 
  // fxl: only fill in basic info of the in-mem inode. wont read from disk, which is 
  // done by ilock()
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&itable.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&itable.lock);
  ip->ref++;
  release(&itable.lock);
  return ip;
}

// Lock the given inode.    
// Reads the inode from disk if necessary.
// fxl: grab ip::lock (sleeplock). wont incre refcnt (which is just for in-mem inode slot mgmt)
//  this serializes concurrent access to a file (i.e. inode). therefore needed by iread/iwrite/etc
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));  // fxl: map inum to disk inode
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;   // fxl: map to in-mem inode
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

#ifdef CONFIG_FAT
void ilock_fat(struct inode *ip)
{
  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0) {  
    ip->type = T_FILE_FAT;   // default
    ip->nlink = 1; 
    // ip->size = f_size(ip->fatfp);    // no needed. & we may haven't open() the file yet
    ip->valid = 1;
    ip->fatdir = 0; ip->fatfp = 0;
  }
}
#endif

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode table entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode. fxl: meaning what
void
iput(struct inode *ip)
{
  acquire(&itable.lock);

  if(ip->ref == 1 && ip->valid && ip->nlink == 0){
    // inode has no links and no other references: truncate and free.

    // fat cannot do auto truncate/free, nlink wont reach 0
    // the actual file will be f_close() before last iput() was called. 
    // so only recycle inode (by dec ip->ref below)
    BUG_ON(is_fattype(ip->type));  

    // ip->ref == 1 means no other process can have ip locked, (fxl: otherwise ref>1)
    // so this acquiresleep() won't block (or deadlock).
    acquiresleep(&ip->lock);

    release(&itable.lock);  // fxl: why release spinlock? b/c disk op below can be slow? 

    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    ip->valid = 0;

    releasesleep(&ip->lock);

    acquire(&itable.lock); // fxl: reacquire...
  }

  ip->ref--;            // fxl: reduce the inmem inode refcnt...
  release(&itable.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
// returns 0 if out of disk space.
// (unused by fat)
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[bn] = addr;
    }
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[bn] = addr;
        log_write(bp);
      }
    }
    brelse(bp);  
    
    return addr;
  }
  bn -= NINDIRECT; 

  // doubly indirect ptr 
  if (bn < NINDIRECT * NINDIRECT) {
      if ((addr = ip->addrs[NDIRECT + 1]) == 0) // locate lv1 indirect block
          ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev); // inode will be done by iwrite()
      bp = bread(ip->dev, addr);
      a = (uint *)bp->data;
      if ((addr = a[bn / (NINDIRECT)]) == 0) {  // lv2 indirect block
          a[(bn / NINDIRECT)] = addr = balloc(ip->dev); BUG_ON(!addr); 
          log_write(bp);   // W("===> bn %u alloc blk %u", bn, addr);
      }
      brelse(bp);
      bp = bread(ip->dev, addr);
      a = (uint *)bp->data;
      if ((addr = a[bn % (NINDIRECT)]) == 0) { // locate the data block 
          a[(bn % NINDIRECT)] = addr = balloc(ip->dev);
          log_write(bp);  // W("===> bn0 %u alloc data blk %u", bn0, addr);
      } // else W("ok: bn %u found blk %u", bn, addr);
      brelse(bp);
      return addr;
  }

  panic("bmap: out of range");
  return 0; 
}

// Truncate inode (discard contents). fxl: also free all data blocks
// Caller must hold ip->lock.
// (unused by fat)
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }
  // fxl: TODO deal with doubly indirect
  // cf https://github.com/nxbyte/Advanced-xv6/blob/master/6%20-%20Filesystem%20-%20Triply%20Indirect/fs.c#L476
  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

// Read data from inode.
// Caller must hold ip->lock.
// If user_dst==1, then dst is a user virtual address;
// otherwise, dst is a kernel address. (fxl: kernel va)
int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return 0;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    uint addr = bmap(ip, off/BSIZE);
    if(addr == 0)
      break;
    bp = bread(ip->dev, addr);
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      brelse(bp);
      tot = -1;
      break;
    }
    brelse(bp);
  }
  return tot;
}

// Write data to inode.
// Caller must hold ip->lock.
// If user_src==1, then src is a user virtual address;
// otherwise, src is a kernel address.
// Returns the number of bytes successfully written.
// If the return value is less than the requested n,
// there was an error of some kind.
int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    uint addr = bmap(ip, off/BSIZE);
    if(addr == 0)
      break;
    bp = bread(ip->dev, addr); //fxl: read back then write. never called bwrite() directly
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp);
      break;
    }
    log_write(bp);
    brelse(bp);
  }

  if(off > ip->size)
    ip->size = off;

  // write the i-node back to disk even if the size didn't change
  // because the loop above might have called bmap() and added a new
  // block to ip->addrs[].
  iupdate(ip);

  V("writei succeeds");
  return tot;
}

// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum); // fxl: also return inode found (refcnt'd)
    }
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
// Returns 0 on success, -1 on failure (e.g. out of disk blocks).
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    return -1;

  return 0;
}

// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If nameiparent != 0, return the inode for the parent and copy the final
// path element into "name", which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
// fxl: needed by sys_chdir() exec() etc. too tightly coupled with xv6fs path 
// resolution, don't implement fat logic here (better in its callers)
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  // fxl: traverse from '/'. there's a default inode in mem. 
  //  first time we ilock() it, the root inode will be loaded from disk, which 
  //  goes to fs then block driver... 
  V("namex path %s", path);

  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);    // fxl: hardcoded
  else {
    ip = idup(myproc()->cwd);
    // V("cwd is %lx", (unsigned long)(myproc()->cwd));
  }
  BUG_ON(!ip); 

  // fxl: traverse... from cwd
  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){ // fxl: will do iget()
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0 /*need inode for path, not its parent*/, name);
}

// fxl: return inode for parent
struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1 /*need parent inode*/, name);
}

// fat glue, etc


#ifdef CONFIG_FAT
// http://www.cse.yorku.ca/~oz/hash.html
// fatpath: native, abs path for fatfs, e.g. "3:/myfile"
// applies to both dir and file
// TBD: normalize the path, like removing trailing '/'?
static unsigned int fatpath_to_inum(const char *fatpath) {
    unsigned hash = 5381;
    int c;
    while ((c = *fatpath++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash;
}

// only make an inode, no need to open file/dir etc, b/c we may be 
// just doing f_chdir() etc.
// fatpath: a native fatpath
struct inode* namei_fat(char *fatpath) {
  return iget(SECONDDEV, fatpath_to_inum(fatpath)); 
}

#include <fcntl.h>
// on success, return inode. will take a inode ref
// path: native, abs path for fatfs, e.g. "3:/myfile"
//
// will lock then unlock inode
// populate inode fileds with fat fileinfo, b/c 
// f_stat requires path, which we lost after returning from open()
// XXX we could also save path in inode XXX
struct inode* fat_open(const char *path, int omode) {
  struct inode* ip = 0;
  int flag = 0, isdir = 0;
  uint fsize = 0;   
  FRESULT ret; 
  FILINFO info;

  I("%s got path %s omode %x", __func__, path, omode); 

  if ((ret = f_stat(path, &info)) == FR_OK) { // file/dir already exists
    isdir = info.fattrib & AM_DIR;
    if (!isdir) 
      fsize = info.fsize;

    // access rule check (more?)
    if (isdir && omode != O_RDONLY)
      return 0; 
    if ((info.fattrib & AM_RDO) && (omode&0xfff) != O_RDONLY)
      return 0; 
  }

  if ((omode&0xfff) == O_RDONLY)
    flag |= FA_READ; 
  else if (omode & O_WRONLY || omode & O_RDWR) 
    flag |= FA_WRITE; 
  if (omode & O_CREATE)
    flag |= FA_CREATE_NEW;  
  if (omode & O_TRUNC) {  // used in sh.c ">"
    flag |= FA_CREATE_ALWAYS; 
  }
  // gen a pseudo inum, which is used idx in itable
  // (XXX need a better way to generate inum..., maybe obj id in FILINFO?)
  // the problem: need to open the file/dir before iget()
  ip = iget(SECONDDEV, fatpath_to_inum(path)); 
  ilock_fat(ip);
  if (isdir) { 
    ip->type = T_DIR_FAT; 
    if (!(ip->fatdir = malloc(sizeof(DIR)))) {
      BUG(); // TODO iunlock, clean up... 
    }
    if ((ret=f_opendir(ip->fatdir, path))!=FR_OK) {
      W("f_opendir '%s' failed with ret %d", path, ret); 
      iunlockput(ip); 
      return 0; 
    }    
  } else { // normal file
    ip->type = T_FILE_FAT; 
    if (!(ip->fatfp = malloc(sizeof(FIL)))) {
      BUG(); // TODO iunlock, clean up... 
    }    
    if ((ret=f_open(ip->fatfp, path, flag))!=FR_OK) {
      I("f_open '%s' failed with ret %d", path, ret); 
      iunlockput(ip); 
      return 0; 
    }
    ip->size = (flag & FA_CREATE_ALWAYS) ? 0 : fsize;  // O_TRUNC or not?      
  }
  iunlock(ip);     
  return ip; 
}

// http://elm-chan.org/fsw/ff/doc/filename.html
int in_fatmount(const char *path) {
  return (strncmp(path, "/d/", 3) == 0); 
}

// path is like "/d/file.txt", whereas
// ffs expects a path name like: "2:file.txt" where 2 is the volume id (i.e.
// our dev id) used in f_mount()
// pathout must be preallocated
int to_fatpath(const char *path, char *fatpath, int dev) {
  return snprintf(fatpath, MAXPATH, "%d:%s", dev, path+2/*skip*/); 
}

#endif  
