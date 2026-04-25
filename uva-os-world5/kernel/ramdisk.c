#include "utils.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// assuming a ramdisk "sector" is 512B

// defined in linker.ld, kernel va
extern char ramdisk_start, ramdisk_end; 

static struct spinlock ramdisk_lock;

void            
ramdisk_init(void) {
    initlock(&ramdisk_lock, "ramdisk");
}

void            
ramdisk_rw(struct buf *b, int write) {
    uint sec_no = b->blockno * (BSIZE / 512);

    if (&ramdisk_start + 512*sec_no > &ramdisk_end) {
        E("requesting sec %u, total sec %d", sec_no, 
            (int)(&ramdisk_end - &ramdisk_start)/512);
        BUG(); 
    }

    acquire(&ramdisk_lock);
    if (write)
        memmove(&ramdisk_start + 512*sec_no, b->data, BSIZE); 
    else 
        memmove(b->data, &ramdisk_start + 512*sec_no, BSIZE); 
    release(&ramdisk_lock);
}