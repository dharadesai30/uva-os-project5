// Support functions for system calls that involve file descriptors.
//  Like vfs; derived from xv6-riscv
//  tightly coupled with fs.c (fs code...)

#define K2_DEBUG_WARN 

#include "plat.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"
#include "sched.h"
#include "stat.h"
#include "utils.h"
#include "procfs.h"

#ifdef CONFIG_FAT
#include "ff.h"
#endif

// /dev/ and their direct read/write funcs
// if left unregistered (NULL), syscall will return -1
struct devsw devsw[NDEV]; 

// the system-wide table for all opened files
struct {
    struct spinlock lock;
    struct file file[NFILE];
} ftable;

static void init_devfs(); 
void fileinit(void) {
    initlock(&ftable.lock, "ftable");
    init_devfs(); 
}

// Allocate a file structure.
struct file *
filealloc(void) {
    struct file *f;

    acquire(&ftable.lock);
    for (f = ftable.file; f < ftable.file + NFILE; f++) {
        if (f->ref == 0) {
            f->ref = 1;
            release(&ftable.lock);
            return f;
        }
    }
    release(&ftable.lock);
    return 0;
}

// Increment ref count for file f.
struct file *
filedup(struct file *f) {
    acquire(&ftable.lock);
    if (f->ref < 1)
        panic("filedup");
    f->ref++;
    release(&ftable.lock);
    return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void fileclose(struct file *f) {
    struct file ff;

    V("%s called", __func__);

    acquire(&ftable.lock);
    if (f->ref < 1)
        panic("fileclose");
    if (--f->ref > 0) {
        release(&ftable.lock);
        return;
    }
    /* Careful: while we hold ftable.lock, don't do anything slow or complex. 
        Will easily cause deadlock. Instead, duplicate "file" and work on it
        after we release ftable.lock. 
        Ex procfs_parse_usercontent() on /dev/fb0
        may launch a new process via copy_process(), which will call filedup,
        which grabs ftable.lock --> deadlock! 
    */
    ff = *f;  // dup "file". continue to work on it after rls the spinlock
    f->ref = 0;
    f->type = FD_NONE;
    release(&ftable.lock);
    
    /* Done with ftable. continue to work on "file" or inode */

    if (ff.type == FD_PROCFS && ff.content) {
        struct procfs_state * st = (struct procfs_state *)(ff.content);
        if (st->usize)
            procfs_parse_usercontent(&ff); 
        kfree(ff.content); 
    } else if (ff.type == FD_PIPE) {
        pipeclose(ff.pipe, ff.writable);
    } else if (ff.type == FD_INODE || ff.type == FD_DEVICE 
        || ff.type == FD_PROCFS) {
        begin_op();
        iput(ff.ip);
        end_op();
    }

#if 0 /* kept for future ref*/
    if (ff.type == FD_DEVICE) {
        if (ff.major == FRAMEBUFFER && ff.content)
            ; // fb_fini(); // display will go black
        else if (ff.major == DEVSB && ff.content) {
            ; // sound_fini((struct sound_drv *)f->content); 
            // do it upon writing of sbctl
        }
    }
#endif

#ifdef CONFIG_FAT
    // close file & free inode 
    else if (ff.type == FD_INODE_FAT)  {
        if (ff.ip->fatfp) {f_close(ff.ip->fatfp); free(ff.ip->fatfp);}
        else if (ff.ip->fatdir) {f_closedir(ff.ip->fatdir); free(ff.ip->fatdir);}
        iput(ff.ip);
    }
#endif
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int filestat(struct file *f, uint64 addr) {
    struct task_struct *p = myproc();
    struct stat st;

    if (f->type == FD_INODE || f->type == FD_DEVICE ||
        f->type == FD_PROCFS || f->type == FD_INODE_FAT) {
        ilock(f->ip);
        stati(f->ip, &st);
        iunlock(f->ip);
        if (copyout(p->mm, addr, (char *)&st, sizeof(st)) < 0) // copy to user
            return -1;
        return 0;
    }
    return -1;
}

static int procfs_read(struct file *f, uint64 dst, uint n);

// Read from file f.
// addr is a user virtual address. 
// return actual bytes read. <0 on error
//quest: music player
int fileread(struct file *f, uint64 addr, int n) {
    int r = 0;

    if (f->readable == 0)
        return -1;

    if (f->type == FD_PIPE) {
        r = piperead(f->pipe, addr, n);
    } else if (f->type == FD_DEVICE) { // device read  
        if (f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
            return -1;
        r = devsw[f->major].read(1, addr, 0, n, 1, 0); /* STUDENT_TODO: replace this */
    } else if (f->type == FD_INODE) { // normal file
        ilock(f->ip);
        if ((r = readi(f->ip, 1, addr, f->off, n)) > 0) // fs read
            f->off += r;
        iunlock(f->ip);
    } 
#ifdef CONFIG_FAT    
    else if (f->type == FD_INODE_FAT) { 
        if (f->ip->type == T_FILE_FAT) {
            //  alloc a kernel buf as large as n, and read in. 
            //  TODO optimize: kalloc() one page only, and read in one page at a time
            char *buf = malloc(n); if (!buf) {return -2;}
            unsigned n1; 
            ilock_fat(f->ip);            
            if ((r=f_read(f->ip->fatfp, buf, n, &n1)) == FR_OK &&
                either_copyout(1/*usrdst*/, addr, buf, n1) == 0)
                {f->off+=n1; r = (int)n1;}
            else 
                {W("f_read failed returns %d, n=%d", r, n); r = -1;}
            iunlock(f->ip);
            free(buf); 
        } else if (f->ip->type == T_DIR_FAT) {
            // read one entry at a time            
            int sz = sizeof (FILINFO); 
            if (n < sz) 
                r=-1;
            else {
                FILINFO fno; 
                if (!f->ip->fatdir) BUG();
                if (f_readdir(f->ip->fatdir, &fno) == FR_OK) {
                    // http://elm-chan.org/fsw/ff/doc/readdir.html
                    // "When all items in the directory have been read and no item to 
                    // read, a null string is stored into the fno->fname[] without an error"                    
                    if (fno.fname[0]) {
                        if (either_copyout(1/*userdst*/, addr, &fno, sz)==0)
                            {V("f_readdir ok"); r = sz;}
                        else 
                            {r=-1;BUG();}
                    } else 
                        r = 0; // no more dir entries
                } else 
                    {W("f_readdir failed"); r=-1;}
            }
        } else BUG(); 
    } 
#endif                    
    else if (f->type == FD_PROCFS) {
        // cf file.h procfs_state comments
        // procfs maintains offs for read/write separately. 
        // so don't use f->off.
        // ilock(f->ip); uint sz = f->ip->size; iunlock(f->ip);     
        return procfs_read(f, addr, n);  // maintains read off within
    } else {
        panic("fileread");
    }

    return r;
}

// if ok, return the resulting offset location as measured in bytes from 
//  the beginning of the file. 
//  On error, the value -1 is returned
// https://man7.org/linux/man-pages/man2/lseek.2.html
#include "fb.h"
extern int sf_size(int pid);  //sf.c
int filelseek(struct file *f, int offset, int whence) {
    int newoff; uint size=0; 

    // sanity checks 
    if (f->type == FD_PIPE)     
        return -1;      
    if (f->type == FD_DEVICE) {
        if (f->major < 0 || f->major >= NDEV || f->major == DEVSB)
            return -1;        
    }
    if (f->type == FD_PROCFS)  // ambiguous, as there are read/write offs
        return -1; 
    if (f->type == FD_DEVICE) {
        if (f->major == FRAMEBUFFER) {
            acquire(&mboxlock); 
            size = the_fb.size; 
            release(&mboxlock); 
            // fall through below
        } else if (f->major == FRAMEBUFFER0) {
            int r = sf_size(myproc()->pid); 
            if (r<0) return -1; 
            size = (unsigned int)r; 
        }
    } 
#ifdef CONFIG_FAT    
    else if (f->type == FD_INODE_FAT) { 
        // pass through to f_lseek() 
        // fatfs maintains offset by itself. we don't maintain it in f->off        
        // NB1: f_lseek only supports offset starting from the begin. so has to 
        // wrap around it 
        // NB2: although f_lseek() allows to seek beyond file end (hence expanding it), 
        // we throw a warning upon detection
        // NB3: limitation: SEEK_END does not support dynamic file size. it 
        //      is based on the file size when file is opened. 
        int ret = 0; 
        ilock(f->ip); 
        size = f->ip->size;
        switch (whence) { 
        case SEEK_SET:
            if (offset>size) {W("warning: seek beyond file end. will expand file");}
            if (offset<0) 
                ret = -1;
            break;
        case SEEK_CUR:            
            if (offset<0 && -offset>f->off) 
                {E("bad offset %d", offset); ret=-1;}
            else {
                offset = f->off + offset; 
                if (offset>size) {W("warning: seek beyond file end. will expand file");}
            }
            break; 
        case SEEK_END:  // "set file offset to EOF plus offset", i.e. offset shall <0 if seek backward
            if (offset<0 && -offset>size) 
                {E("bad offset %d", offset); ret=-1;}
            else {
                offset = size + offset; 
                if (offset>size) {W("warning: seek beyond file end. will expand file");}
            }
            break; 
        default:
            E("unrecog option");
            ret = -1; 
        }

        if (ret != -1) {
            if ((ret = f_lseek(f->ip->fatfp, offset) != FR_OK))
                {W("f_lseek failed with %d", ret); ret = -1;}
            else 
                {ret = offset; f->off=offset;}
        }
        iunlock(f->ip); 
        return ret; 
    }
#endif    
    else if (f->type == FD_INODE) { // normal file
        // fxl: take a snapshot of filesize. but nothing prevents file size changes
        // after we unlock inode, making f->off invalid. in that case, read/write
        // shall fail and nothing bad shall happen
        ilock(f->ip);
        size = f->ip->size; 
        iunlock(f->ip);
    } else BUG();
    
    switch (whence)
    {
    case SEEK_SET:
        newoff = offset; 
        break;
    case SEEK_CUR:
        newoff = f->off + offset; 
        break; 
    case SEEK_END:  // "set file offset to EOF plus offset", i.e. offset shall <0
        if (offset > 0) { W("unsupported"); return -1;}
        newoff = size + offset; 
        break; 
    default:
        E("unrecog option");
        return -1; 
    }

    if (newoff < 0 || newoff > size) {E("newoff %d OOB", newoff); return -1;}

    f->off = newoff; 
    return newoff; 
}

static int procfs_write(struct file *f, uint64 src, uint n);

// Write to file f.
// addr is a user virtual address.
int filewrite(struct file *f, uint64 addr, int n) {
    int r, ret = 0;

    if (f->writable == 0)
        return -1;

    if (f->type == FD_PIPE) {
        ret = pipewrite(f->pipe, addr, n);
    } else if (f->type == FD_DEVICE) {
        if (f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
            return -1;
        ret = devsw[f->major].write(1/*userdst*/, addr, f->off, n, f->content);
        if (ret>0 && (f->major == FRAMEBUFFER || f->major == FRAMEBUFFER0))
            f->off += ret; 
    } else if (f->type == FD_INODE) {
        // write a few blocks at a time to avoid exceeding
        // the maximum log transaction size, including
        // i-node, indirect block, allocation blocks,
        // and 2 blocks of slop for non-aligned writes.
        // this really belongs lower down, since writei()
        // might be writing a device like the console.  fxl: last sentence means??
        int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
        int i = 0;
        while (i < n) {
            int n1 = n - i;
            if (n1 > max) // write n1 (<=max) blocks at a time...
                n1 = max;

            begin_op();
            ilock(f->ip);
            if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
                f->off += r;
            iunlock(f->ip);
            end_op();

            if (r != n1) {
                // error from writei
                break;
            }
            i += r;
        }
        ret = (i == n ? n : -1);
    } 
#ifdef CONFIG_FAT    
    else if (f->type == FD_INODE_FAT) { 
            //  alloc a kernel buf as large as n, and write. 
            //  TODO optimize: kalloc() one page only, and read in one page at a time
            char *buf = malloc(n); if (!buf) {return -2;}
            unsigned n1; 
            FRESULT fr = 9999;  // invalid
            ilock_fat(f->ip);
            if (either_copyin(buf, 1/*usrdst*/, addr, n) == 0 &&
                (fr=f_write(f->ip->fatfp, buf, n, &n1)) == FR_OK) {
                if (n!=n1) W("f_write ok but disk is full");
                f->off += n1; ret = (int)n1;
            } else {
                E("failed. f_write returns %d", fr); ret = -1; BUG();
            }
            iunlock(f->ip);
            free(buf); 
    } 
#endif               
    else if (f->type == FD_PROCFS) {
        return procfs_write(f, addr, n); 
    } else
        panic("filewrite");

    return ret;
}

///////////// devfs
int devnull_read(int user_dst, uint64 dst, int off, int n, char blocking, void *content) {
    return 0; 
}

int devnull_write(int user_src, uint64 src, int off, int n, void *content) {
    return 0; 
}

int devzero_read(int user_dst, uint64 dst, int off, int n, char blocking, void *content) {
    char zeros[64]; memzero(zeros, 64); 
    int target=n;
    while (n>0) {
        int len = MIN(64,n); 
        if (either_copyout(user_dst, dst, zeros, len) == -1)
            break;
        dst+=len; 
        n-=len; 
    }    
    return target; 
}

#include "fb.h"
int devfb_write(int user_src, uint64 src, int off, int n, void *content) {
    int ret = 0, len; 
    acquire(&mboxlock); 
    if (!the_fb.fb)
        goto out; 
    len = MIN(the_fb.size - off, n); 
    if (either_copyin(the_fb.fb + off, 1, src, len) == -1)
        goto out; 
    ret = len;
    __asm_flush_dcache_range(the_fb.fb+off, the_fb.fb+off+len); 
    // __asm_flush_dcache_range(the_fb.fb, the_fb.fb+len); // a bug by fxl. what will happen on display?
out: 
    release(&mboxlock); 
    return ret; 
}

///////// procfs 

void procfs_init_state(struct procfs_state *st) {
    st->ksize = st->usize = 0; 
    st->koff = st->uoff = 0; 
}

extern int procfs_sbctl_gen(char *txtbuf, int sz);  // sound.c 

#include "build_info.h" // automatically gen, cf Makefile

// return: len of content generated. can be multi lines.
//quest: multicore
#define TXTSIZE  256 
int procfs_gen_content(int major, char *txtbuf, int sz) {
    int len = 0; 

    switch (major)
    {
    case PROCFS_DISPINFO:
        acquire(&mboxlock);
        len = snprintf(txtbuf, sz, 
            "%6d %6d %6d %6d %6d %6d %6d %6d %6d\n"
            "# %6s %6s %6s %6s %6s %6s %6s %6s %6s\n",
            the_fb.width, the_fb.height, 
            the_fb.vwidth, the_fb.vheight, 
            the_fb.scr_width, the_fb.scr_height, 
            the_fb.pitch, the_fb.depth, the_fb.isrgb,
            "width","height","vwidth","vheigh","swidth","sheigh",
            "pitch","depth","isrgb"); 
        release(&mboxlock); 
        break;    
    case PROCFS_FBCTL: 
        acquire(&mboxlock);
        len = snprintf(txtbuf, sz, 
            "# format: %6s %6s %6s %6s [%6s] [%6s]\n"
            "# ex1: echo 256-256-128-128 > /procfs/fbctl // will reinit fb \n"
            "# ex2: echo 0-0-0-0-32-32 > /procfs/fbctl // won't init. only change offsets\n",
            "width","height","vwidth","vheigh", "offsetx", "offsety"); 
        release(&mboxlock); 
        break; 
    case PROCFS_FBCTL0: 
        len = snprintf(txtbuf, sz, "TBD"); 
        break; 
    case PROCFS_SBCTL: 
        len = procfs_sbctl_gen(txtbuf, sz); 
        break; 
    case PROCFS_CPUINFO:         
        //line1: cpu util, e.g. "10 20 30 40"
        //line2 (optional): (1|0|-1)  # 1:qemu 0:rpi3 -1:unknown
        //line3: human readable build string, 
        // e.g. "Sat Nov  2 18:08:17 EDT 2024 git:2882aa4 host:FelixLin-XPS15"
        {
            //line1: cpu util, e.g. "10 20 30 40" 
            // XXX probably need certain locks for cpu::last_util
            char *p = txtbuf; int ll; 
            for (int i = 0; i<NCPU; i++) {                
                ll = snprintf(p, sz, "%d ", -1); /* STUDENT_TODO: replace this */
                len += ll; sz -= ll; p += ll; 
            }
            ll = snprintf(p, sz, "\n"); 
            len += ll; sz -= ll; p += ll;
            //line2 (optional): (1|0|-1)  # 1:qemu 0:rpi3 -1:unknown
#ifdef PLAT_ISQEMU
            ll = snprintf(p, sz, "%d   #1:qemu 0:rpi3 -1:unknown\n", PLAT_ISQEMU);
#else
            ll = snprintf(p, sz, "-1  #1:qemu 0:rpi3 -1:unknown\n");
#endif
            len += ll; sz -= ll; p += ll;
            //line3: human readable build string,
            len += snprintf(p, sz, "%s %s on:%s qemu=%d\n", 
                BUILD_DATE, GIT_COMMIT, BUILD_MACHINE, PLAT_ISQEMU);
        }

        break;
    default:
        // implement more procfs type here
        // len = snprintf(txtbuf, sz, "%s\n", "TBD"); 
        len = 0; 
        break;
    }
    BUG_ON(len<0||len>=sz);
    return len; 
}

// will maintain (read) offset, otherwise user don't know
//        if reads all contents & should stop
// return: actual bytes read
static int procfs_read(struct file *f, uint64 dst, uint n) {    
    uint len, off, sz; 
    char *buf; 
    struct procfs_state *st = (struct procfs_state *) f->content; BUG_ON(!st);
    off = st->koff; sz = st->ksize; buf = st->kbuf; BUG_ON(off > sz); 
    len = MIN(n, sz-off); 
    if (either_copyout(1/*userdst*/, dst, buf+off, len) == -1) {
        BUG(); return -1; 
    }
    st->koff += len; 
    return len; 
}

////////// procfs write handlers....
#define LINESIZE   32

// will maintain (write) offset 
// return: actual bytes written; -1 on failure
static int procfs_write(struct file *f, uint64 src, uint n) {
    uint len, off; 
    char *buf; 
    struct procfs_state *st = (struct procfs_state *) f->content; BUG_ON(!st);

    off = st->uoff; buf = st->ubuf; 

    len = MIN(n, MAX_PROCFS_SIZE - off); 
    if (either_copyin(buf+off, 1, src, len) == -1) {
        BUG(); return -1;
    }
    st->uoff += len; 
    if (off+len > st->usize) st->usize = off+len; 
    return len; 
}

// parse a line of write into a list of MAX_ARGS integers, and 
//      call relevant subsystems
// limit: only support dec integers
// so far, procfs extracts a line from each write() 
//           (not parsing a line across write()s
// return # of args parsed. 0 on failure

extern int procfs_parse_fbctl(int args[PROCFS_MAX_ARGS]); // mbox.c
extern int procfs_parse_fbctl0(int args[PROCFS_MAX_ARGS]); // sf.c
extern int procfs_parse_sbctl(int args[PROCFS_MAX_ARGS]); // sound.c

int procfs_parse_usercontent(struct file *f) {
    struct procfs_state *st = (struct procfs_state *) f->content; BUG_ON(!st);
    char *buf = st->ubuf, *s;  
    uint len = st->usize, nargs = 0; 
    int args[PROCFS_MAX_ARGS]={0}; 

    // parse the 1st line of write to a list of int args... (ignore other lines
    for (s = buf; s < buf+len; s++) {
        if (*s=='\n' || *s=='\0')
            break;         
        if ('0' <= *s && *s <= '9') { // start of a num
            args[nargs] = atoi(s); // W("got arg %d", args[nargs]);             
            while ('0' <= *s && *s <= '9' && s<buf+len) 
                s++; 
            if (nargs++ == PROCFS_MAX_ARGS)
                break; 
        } 
    }
    
    // W("args");
    // for (int i = 0; i < PROCFS_MAX_ARGS; i++)
    //     printf("%d ", args[i]); 
    // printf("\n");

    // TBD need a way to return semantic error?
    // or caller simply read from procfs to see if it succeeds
    switch (f->major)
    {
    case PROCFS_FBCTL:
        procfs_parse_fbctl(args); 
        break;    
    case PROCFS_FBCTL0:
        procfs_parse_fbctl0(args); 
        break;
    case PROCFS_SBCTL: 
        procfs_parse_sbctl(args); 
        break;                 
    default:
        return 0;
        break;
    }
    return nargs; 
}

extern 
int devsb_write(int user_src, uint64 src, int off, int n, void *content);  // sound.c
// extern 
// int devfb0_write(int user_src, uint64 src, int off, int n, void *content); // sf.c

static void init_devfs() {
    devsw[DEVNULL].read = devnull_read;
    devsw[DEVNULL].write = devnull_write; 

    devsw[DEVZERO].read = devzero_read;
    devsw[DEVZERO].write = 0; // nothing

    devsw[FRAMEBUFFER].read = 0; // TBD (readback?
    devsw[FRAMEBUFFER].write = devfb_write; 
}