// #define K2_DEBUG_WARN 1
#define K2_DEBUG_INFO 1

// an interface appeases user sound code. does not play anything
// for rpi3 qemu. 

#include "utils.h"
#include "plat.h"
#include "spinlock.h"
#include "file.h"
#include "fcntl.h"
// #include "procfs-defs.h"

#define MAX_SOUND_DRV 1

#define TXTSIZE  256 
int procfs_sbctl_gen(char *txtbuf, int sz) {
    char line[TXTSIZE]; int len, len1; char *p = txtbuf; 

    len = snprintf(line, TXTSIZE, 
        "# %6s %6s %6s %6s %6s %6s %s\n",
        "id","HwFmt","SplRate","QSize", "BytesFree", "WrFmt", "WrChans"); 
    if (len > sz) return 0; 
    memmove(p, line, len);
    p += len; 

    // fake a line
    for (int i=0; i<MAX_SOUND_DRV; i++) {
        len = snprintf(line, TXTSIZE, "%6d %6d %6d %6d %6d %6d %6d\n",
            0, 3, 44100, 35281, 0, 1, 2); 
        len1 = MIN(len, sz-(p-txtbuf));
        memmove(p, line, len1); p += len1; 
        if (p >= txtbuf) 
            break; 
    }

    return p-txtbuf; 
}

int devsb_write(int user_src, uint64 src, int off, int n, void *content) {
    short minor = (short)(unsigned long)content; 
    if (minor >= MAX_SOUND_DRV) return -2;

    // emulate the actual hw delay, which will eventually block write() 
    // otherwise user writes() are too fast; animation will have no time to display
    // assume 44KHz, dual channel
    us_delay(n*1000/44/2); 

    return n; 
}

struct sound_drv * sound_init(unsigned nChunkSize) {
    devsw[DEVSB].read = 0; 
    devsw[DEVSB].write = devsb_write; 
    I("audio init ok"); 
    return 0;
}

int procfs_parse_sbctl(int args[PROCFS_MAX_ARGS]) {  
    int id = -1; 
    int cmd = args[0]; 
    int ret = 0; 

    switch (cmd)
    {  // fxl: XXX refactor lock acquire/release below
    case SB_CMD_FINI: // arg1=devid
        if (id>=MAX_SOUND_DRV) return 0; 
        I("sound fini drv %d", id);
        ret = 2; 
        break;
    case SB_CMD_INIT: // init
        if (args[1] >= 0) {
            I("sound init chunksize=%d", args[1]);
            sound_init(args[1]);
            ret = 1; 
        }
        break; 
    case SB_CMD_START: // arg1=devid
        id = args[1]; 
        if (id>=MAX_SOUND_DRV) return 0; 
        I("sound_start drv %d", id);
        ret = 2; 
        break; 
    case SB_CMD_CANCEL: // arg1=devid
        id = args[1]; 
        if (id>=MAX_SOUND_DRV) return 0; 
        I("sound_cancel drv %d", id);
        ret = 2; 
        break; 
    case SB_CMD_WR_FMT: // arg1=devid, arg2=write fmt, arg3=channels
        id = args[1]; 
        if (id>=MAX_SOUND_DRV) return 0; 
        I("SetWriteFormat drv %d wrfmt %d nchans %d", id, args[2], args[3]);
        ret = 2; 
        break;
    case SB_CMD_TEST:  // play sound samples built-in the kernel
        I("test sound");
        break; 
    default:
        W("unknown cmd %d", cmd); 
        break;
    }
    return ret; 
}