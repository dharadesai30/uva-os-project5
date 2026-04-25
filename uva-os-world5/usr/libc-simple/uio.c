// minimal user io lib 
// a thin layer over /dev/XXX and /procfs/XXX
#include "user.h"

#define LINESIZE 128    

// return 0 on success
int config_fbctl(int w, int d, int vw, int vh, int offx, int offy) {
    char buf[LINESIZE];
    int n, fbctl; 
    if ((fbctl = open("/proc/fbctl", O_RDWR)) <=0) return -1; 

    sprintf(buf, "%d %d %d %d %d %d\n", w, d, vw, vh, offx, offy); 
    n=write(fbctl,buf,strlen(buf)); 
    // printf("write returns %d\n", n);
    close(fbctl);  // flush it 
    return !(n>0); 
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

// read a line from /dev/events and parse it into key events
// line format: [kd|ku] 0x12
// "events": fd for /dev/events
// return: 0 on success 
int read_kb_event(int events, int *evtype, unsigned int *scancode) {
    int n; 
    char buf[LINESIZE], *s;

    *evtype = INVALID; *scancode = 0; //invalid

    n = read(events, buf, LINESIZE); if (n<=0) return -1; 
    // printf("%s: %s\n", __func__, buf);
    s=buf;         
    if (buf[0]=='k' && buf[1]=='d') {
      *evtype = KEYDOWN; 
    } else if (buf[0]=='k' && buf[1]=='u') {
      *evtype = KEYUP; 
    } 
    s += 2; while (*s==' ') s++; 
    if (s[0]=='0' && s[1]=='x')
      *scancode = atoi16(s); 
    return 0; 
}

