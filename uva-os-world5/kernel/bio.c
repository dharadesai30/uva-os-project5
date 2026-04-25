// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
// fxl: the buffer returned from bread() must be freed with brelse...

#include "utils.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  // walk it - for dbg
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   W("b %lx", (unsigned long)b); 
  // }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);
  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    // W("b %lx", (unsigned long)b);  
    // if (!b->next) 
    //   W("nullptr at %lx", &(b->next));
    BUG_ON(!b); 
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");

  return 0; 
}

extern void sd_part_rw(int part, struct buf *b, int write); // sd.c

// Return a locked buf with the contents of the indicated block.
//quest: FAT
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    if (dev == DEV_RAMDISK)
      ramdisk_rw(b, 0);
#ifdef PLAT_VIRT      
    else if (dev == DEV_VIRTDISK)
      virtio_disk_rw(b, 0);
#endif
    else if (is_sddev(dev)) { // device is an SD card
      sd_part_rw(sd_dev_to_part(dev), 0, 0); /* STUDENT_TODO: replace this */
    }
    else 
      BUG(); 
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
//quest: FAT
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  if (b->dev == DEV_RAMDISK)
    ramdisk_rw(b, 1);
#ifdef PLAT_VIRT          
  else if (b->dev == DEV_VIRTDISK)
    virtio_disk_rw(b, 1);
#endif    
  else if (is_sddev(b->dev)) { // device is an SD card
    sd_part_rw(sd_dev_to_part(0), 0, 1); /* STUDENT_TODO: replace this */
  }
  else
    BUG(); 
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


// diskop as needed by fat32. instead of interfacing fat32 with the disk (SD) 
// driver, we make it read/write of buffer cache, hence better performance
//quest: FAT
#ifdef CONFIG_FAT

#include "ffconf.h"
#include "ff.h"
#include "diskio-ff.h"

/* Initialize a Drive */
// extern int sd_init(); 
DSTATUS disk_initialize(BYTE drv)
{
    W("disk_initialize, drv %u", drv); 
    return 0;
}

/* Return Disk Status */
DSTATUS disk_status(BYTE drv)
{
    return 0;
}

_Static_assert(BSIZE == FF_MAX_SS);
_Static_assert(BSIZE == FF_MIN_SS);

/* Read Sector(s) */
DRESULT disk_read(BYTE drv, BYTE *buff, DWORD sector, UINT count)
{
    struct buf *bp; 
    uint dev = drv;     
    for (unsigned int i = 0; i < count; i++) {
      if (!(bp = bread(dev, 0))) { /* STUDENT_TODO: replace this */
          return RES_ERROR; 
      }
      memmove(0, 0, BSIZE); /* STUDENT_TODO: replace this */
      brelse(bp);
    }
    return RES_OK; 
}

/* Write Sector(s) */
DRESULT disk_write(BYTE drv, const BYTE *buff, DWORD sector, UINT count)
{
    struct buf *bp; 
    uint dev = drv; 
    unsigned int i; 
    for (i = 0; i < count; i++) {
      if ((bp = bget(dev, 0))) { /* STUDENT_TODO: replace this */
        memmove(0, 0, BSIZE); /* STUDENT_TODO: replace this */
        bwrite(0); /* STUDENT_TODO: replace this */
        brelse(bp);
      } else 
        break;         
    }
    if (i == count) return RES_OK;
    return RES_ERROR;      
}

/* Miscellaneous Functions */
extern unsigned long sd_dev_get_sect_count(int dev);   // sd.c
extern unsigned long sd_get_sect_size();
DRESULT disk_ioctl(BYTE drv, BYTE ctrl, void *buff)
{
    if (ctrl == GET_SECTOR_COUNT)
    {
        *(DWORD *)buff = sd_dev_get_sect_count(drv);
    }
    else if (ctrl == GET_SECTOR_SIZE)
    {
        *(DWORD *)buff = sd_get_sect_size(); 
    }
    else if (ctrl == GET_BLOCK_SIZE) /* Get erase block size in unit of sectors (DWORD) */
    {
        *(DWORD *)buff = sd_get_sect_size(); 
    }
    else if (ctrl == CTRL_SYNC)
    {
    }
    else if (ctrl == CTRL_TRIM)
    {
    }
    return RES_OK;
}
#endif // #ifdef CONFIG_FAT