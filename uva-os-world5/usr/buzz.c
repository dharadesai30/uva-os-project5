// play pre-recorded sound samples
// uncompressed samples in C header file
// NO dep on libc or libvorbis

#include <assert.h>
#include <stdint.h>

#include "sound-sample-trimmed.h"
#include "user.h"

#define SOUND_SAMPLES (sizeof Sound / sizeof Sound[0] / SOUND_CHANNELS)

int sb, sbctl;

// cf kernel/sound.c procfs_parse_sbctl()
int sbctl_cmd(int fd, int arg0, int arg1) {
    char line[128]; 
    int len1; 
    
    sprintf(line, "%d %d\n", arg0, arg1); len1 = strlen(line); 
    if ((len1 = write(sbctl, line, len1)) < 0) {
        printf("write to sbctl failed with %d. shouldn't happen", len1);
        exit(1);
    }
    close(sbctl);
    sbctl = open("/proc/sbctl", O_RDWR); // flush
    return len1; 
}

int main() {
    sbctl = open("/proc/sbctl", O_RDWR);
    sb = open("/dev/sb/", O_RDWR); // printf("fb is %d\n", fb);
    assert(sb > 0 && sbctl > 0);

    int len, total = SOUND_SAMPLES;
    unsigned char *p = Sound;

    sbctl_cmd(sbctl, 1 /*init*/, 0/*dont care*/);

    // init load sound samlpes ...
    if ((len = write(sb, p, total)) <= 0) {
        printf("init write failed with %d. shouldn't happen", len);
        exit(1);
    }
    printf("written %d bytes\n", len);
    total -= len;
    p += len;

    sbctl_cmd(sbctl, 2 /*start*/, 0/*drv id*/);

    while (total > 0) {
        if ((len = write(sb, p, total)) <= 0)
            break;
        printf("written %d bytes\n", len);
        total -= len;
        p += len;
    }
    printf("write ret %d. total %d", len, total);
    sleep(100); // let playback finish

    // streaming play wont stop by itself (with pad null frames 
    sbctl_cmd(sbctl, 3 /*cancel*/, 0/*drv id*/); 

    sleep(100); // let playback finish
    
    sbctl_cmd(sbctl, 0 /*fini*/, 0/*drv id*/);
    return 0;
}
