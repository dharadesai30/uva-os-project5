/*
  the sdl version of main.c 
  cf comments in main.c
*/

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include "fce.h"
#include "nes.h"
#include "common.h"   

#include "mario-rom.h"    // built-in rom buffer with Super Mario

#if 0   // TODO. Our OS lacks signal
void do_exit() // normal exit at SIGINT
{
    kill(getpid(), SIGKILL);
}
#endif

static int quiet = 0;   // in fact, no need. can do "nes > /dev/null"
void pr_debug(char *fmt, ...) {
  if (quiet) return; 
  va_list va;
  va_start(va,fmt);
  printf(fmt,va); 
  va_end(va);
}

int xx=0, yy=0; 
int main(int argc, char *argv[])
{
    int fd; 

    if (argc < 2) {
        fprintf(stderr, "Usage: mynes romfile.nes\n");
        fprintf(stderr, "no rom file specified. use built-in rom\n"); 
        goto load; 
    }

    if (argc>=4) { // screen locations given.
      xx=atoi(argv[2]); yy=atoi(argv[3]);
    }

    if (argc>=5 && strcmp(argv[4], "--quiet")==0)
      quiet = 1;

    // fprintf(stdout, "start %d %d %d....\n", xx, yy, argc); 

    if (strncmp(argv[1], "--", 3) == 0)
      goto load; 
    
    if ((fd = open(argv[1], O_RDONLY)) <0) {
        fprintf(stderr, "Open rom file failed.\n");
        return 1; 
    }

    int nread = read(fd, rom, sizeof(rom));     
    if (nread==0) { // Ok if rom smaller than buf size
      fprintf(stderr, "Read rom file failed.\n");
      return 1; 
    }
    pr_debug("open rom...ok\n"); 

load: 
    if (fce_load_rom(rom) != 0) {
        fprintf(stderr, "Invalid or unsupported rom.\n");
        return 1; 
    }
    pr_debug("load rom...ok\n"); 
    // signal(SIGINT, do_exit);

    fce_init();

    pr_debug("running fce ...\n"); 
    fce_run();
    return 0;
}
