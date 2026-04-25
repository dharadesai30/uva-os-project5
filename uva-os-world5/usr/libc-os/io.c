// minimal user io lib 
// a thin layer over /dev/XXX and /procfs/XXX

// originally in user/uio.c, which only depends on ulib.c 
// copy it here to rebase it on libc 

// libc (newlib)
#include "fcntl.h"
#include "stdlib.h"  
#include "unistd.h"
#include "stdio.h"
#include "string.h"

#include "libc-os.h"

// #include "../../../kernel/fcntl.h"  // refactor this

#define LINESIZE 128    
// return 0 on success
int config_fbctl(int w, int d, int vw, int vh, int offx, int offy) {
    char buf[LINESIZE];
    int n, fbctl; 
    if ((fbctl = open("/proc/fbctl", O_RDWR)) <=0) return -1; 

    sprintf(buf, "%d %d %d %d %d %d\n", w, d, vw, vh, offx, offy); 
    n=write(fbctl,buf,strlen(buf)); // printf("write returns %d\n", n);
    close(fbctl);  // flush it 
    return !(n>0); 
}

// return 0 on success. nargs: # of args parsed
int read_dispinfo(int dispinfo[MAX_DISP_ARGS], int *nargs) {
    char buf[LINESIZE], *s;
    int n; 
    int dp; 

    if ((dp = open("/proc/dispinfo", O_RDONLY)) <=0) return -1; 

    n=read(dp, buf, LINESIZE); if (n<=0) return -2;

    // parse the 1st line to a list of int args... (ignore other lines
    for (s = buf, *nargs=0; s < buf + n; s++) {
        if (*s == '\n' || *s == '\0')
            break;
        if ('0' <= *s && *s <= '9') {  // start of a num
            dispinfo[*nargs] = atoi(s); // printf("got arg %d\n", dispinfo[nargs]);
            while ('0' <= *s && *s <= '9' && s < buf + n)
                s++;
            if (++*nargs == MAX_DISP_ARGS)
                break;
        }
    }
    close(dp);  
    return 0; 
}

// same as above 
// is_qemu: 1 yes 0 no (rpi3 hw) -1 unknown
// cf: procfs_gen_content() in kernel/procfs.c
int read_cpuinfo(int util[MAX_NCPU], int *ncpus, int *is_qemu) {
    char buf[LINESIZE], *s;
    int n; 
    int dp; 

    if ((dp = open("/proc/cpuinfo", O_RDONLY)) <=0) return -1; 

    n=read(dp, buf, LINESIZE); if (n<=0) return -2;

    // parse the 1st line to a list of int args... (ignore other lines
    for (s = buf, *ncpus=0; s < buf + n; s++) {
        if (*s == '\n' || *s == '\0')
            break;
        if ('0' <= *s && *s <= '9') {  // start of a num
            util[*ncpus] = atoi(s); // printf("got arg %d\n", dispinfo[nargs]);
            while ('0' <= *s && *s <= '9' && s < buf + n)
                s++;
            if (++*ncpus == MAX_NCPU)
                break;
        }
    }

    while ((*s == '\n' || *s == '\r' || *s == ' ') && s < buf + n)
        s++; // skip '\n'

    // parse the 2nd line to get is_qemu from the 1st token
    // example line: "0 # (comment)"  "1 # (comment)"
    // printf("cpuinfo1: %s s[0] %d\n", s, s[0]);
    if (s < buf + n && ('0' <= *s && *s <= '9')) {
        *is_qemu = *s - '0';
    } else {
        *is_qemu = -1; // unknown
    }

    close(dp);  
    return 0; 
}

// 0 on success 
// read a line from /dev/events and parse it into key events
// line format: [kd|ku] 0x12
//quest: music player, "strtol()" can be used for hex conversion
int read_kb_event(int events, int *evtype, unsigned int *scancode) {
    int n; 
    char buf[LINESIZE], *s;

     
    /* STUDENT_TODO: your code here */

    return 0; 
}

// 0 on success
// not all commands use 4 args. kernel will ignore unused args
// cf kernel/sound.c procfs_parse_sbctl()
int config_sbctl(int cmd, int arg1, int arg2, int arg3) {
    char line[64]; 
    int len1; 

    int sbctl = open("/proc/sbctl", O_RDWR);
    if (sbctl <= 0) {printf("open err\n"); return -1;}
    
    sprintf(line, "%d %d %d %d\n", cmd, arg1, arg2, arg3); len1 = strlen(line); 
    if ((len1 = write(sbctl, line, len1)) <= 0) {
        printf("%s: write failed with %d", __func__, len1);
        exit(1);
    }
    close(sbctl);   // flush
    return 0; 
}

// 0 on success
#define TXTSIZE  256 
int read_sbctl(struct sbctl_info *cfg) {
    char line[TXTSIZE]; 
    FILE *fp = fopen("/proc/sbctl", "r"); 
    if (!fp) {printf("%s: open err\n", __func__); return -1;}
    while (fgets(line, TXTSIZE, fp)) {
        if (line[0] == '#') 
            continue;   // skip a comment line 
        else {
            if (sscanf(line, "%d %d %d %d %d %d %d", 
                &cfg->id, &cfg->hw_fmt, &cfg->sample_rate, 
                &cfg->queue_sz, &cfg->bytes_free, &cfg->write_fmt,
                &cfg->write_channels) != 7) {
                    printf("%s wrong # of paras read", __func__); fclose(fp); return -1; 
                } else {
                fclose(fp); return 0; // done
            }
        } 
    }
    printf("%s: read err\n", __func__); fclose(fp); return -1;
}


// 0 on success
// not all commands use 5 args. kernel will ignore unused args
// cf kernel/sf.c procfs_parse_fbctl0()
int config_fbctl0(int cmd, int x, int y, int w, int h, int zorder, int trans) {
    char line[64]; 
    int len1; 

    int fbctl0 = open("/proc/fbctl0", O_RDWR);
    if (fbctl0 <= 0) {printf("open fbctl0 err\n"); return -1;}
    
    sprintf(line, "%d %d %d %d %d %d %d\n",cmd,x,y,w,h,zorder,trans); 
    len1 = strlen(line); 
    if ((len1 = write(fbctl0, line, len1)) < 0) {
        printf("write to fbctl0 failed with %d. shouldn't happen", len1);
        exit(1);
    }
    close(fbctl0);   // flush
    return 0; 
}


// simple userspace spinlock. same as in ulib.c
// XXX move it out of io.c, or rename io.c 
void spinlock_init(struct spinlock_u *lk) {
  lk->locked = 0; 
}

void spinlock_lock(struct spinlock_u *lk) {
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;
  __sync_synchronize();
}

void spinlock_unlock(struct spinlock_u *lk) {
  __sync_synchronize();
  __sync_lock_release(&lk->locked);
}
