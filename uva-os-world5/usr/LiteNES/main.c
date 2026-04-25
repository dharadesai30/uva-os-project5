/*
---- Below: comment from NJU OS project ---- 

LiteNES originates from Stanislav Yaglo's mynes project:
  https://github.com/yaglo/mynes

LiteNES is a "more portable" version of mynes.
  all system(library)-dependent code resides in "hal.c" and "main.c"
  only depends on libc's memory moving utilities.

How does the emulator work?
  1) read file name at argv[1]
  2) load the rom file into array rom
  3) call fce_load_rom(rom) for parsing
  4) call fce_init for emulator initialization
  5) call fce_run(), which is a non-exiting loop simulating the NES system
  6) when SIGINT signal is received, it kills itself (TBD)
*/

#include "fce.h"   
#include "common.h"   
#include "../user.h"

#define stderr 2

#include "mario-rom.h"    // built-in rom buffer with Super Mario

#if 0   // TODO. Our OS lacks signal
void do_exit() // normal exit at SIGINT
{
    kill(getpid(), SIGKILL);
}
#endif

int main(int argc, char *argv[])
{
    int fd; 

    if (argc != 2) {
        fprintf(stderr, "Usage: mynes romfile.nes\n");
        fprintf(stderr, "no rom file specified. use built-in rom\n"); 
        goto load; 
    }
    
    if ((fd = open(argv[1], O_RDONLY)) <0) {
        fprintf(stderr, "Open rom file failed.\n");
        exit(1);
    }

    int nread = read(fd, rom, sizeof(rom));     
    if (nread==0) { // Ok if rom smaller than buf size
      fprintf(stderr, "Read rom file failed.\n");
      exit(1);
    }
    printf("open rom...ok\n"); 

load: 
    if (fce_load_rom(rom) != 0) {
        fprintf(stderr, "Invalid or unsupported rom.\n");
        exit(1);
    }
    printf("load rom...ok\n"); 
    // signal(SIGINT, do_exit);

    fce_init();

    printf("running fce ...\n"); 
    fce_run();
    return 0;
}
