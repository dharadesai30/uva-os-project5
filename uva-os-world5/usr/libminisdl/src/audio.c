/* 
  A simple implementation of SDL's audio interface, based on procfs: 
    /dev/sb (for writing sound samples)
    /dev/sbctl (for configuring the sound device)

  The lib launches a worker thread, feeding audio data to /dev/sb. 
  The app code should provide the audio data via callback: 
  when the thread runs out of audio data, it calls the callback to refill 
  audio buffer. 
  
 */

#include "SDL.h"
#include "assert.h"

#include "fcntl.h"
// newlib
#include "stdio.h"
#include "unistd.h"
#include "stdlib.h"
#include "string.h"
#include "sys/wait.h"

static int sb = -1; // fd for /dev/sb/ 
static struct sbctl_info info; 
static int ps = 1, cls = 0; // close flag

// 0 on success 
int sdl_init_audio(void) {
  if ((sb = open("/dev/sb/", O_RDWR)) <= 0)  return -1; 
  if (config_sbctl(SB_CMD_INIT, 0/*default chunk*/,-1,-1 /*dont care*/) != 0) return -1; 
  if (read_sbctl(&info) != 0) return -1;   
  return 0; 
}

void sdl_fini_audio(void) {
  if (sb>0)
    close(sb); 
}

/* the audio thread feeding samples to /dev/sb

  note: our kernel sound driver (kernel/sound.c) is smart enough to unblock writer
  when the kernel buffer is below certain threshold (often 1/2 of the buffer
  size). so it's fine even if we don't do user-level double buffering
*/
//quest: music player
#define BUFSIZE (1024 * 16)
static int thread_func(void *param) {
  int len, total, ret = 0; 
  void (*fill)(void *userdata, uint8_t *stream, int len) 
    = param;   // callback to refill audio buffer
  assert(sb>0); 
  unsigned char *buf, *p; 
  
  buf = malloc(BUFSIZE); assert(buf); // the thread's working buf

  int once = 1; 
  int cls0; 
  while (1) {
    // graceful thread shutdown
    while (ps && !(cls0=__atomic_load_n(&cls, __ATOMIC_SEQ_CST)) /*cls==0*/)
      msleep(100);
    if (cls0) {ret = 0; goto done;}
    
    // invoke the provided callback to refill the audio buffer
    fill(0,0,0); /* STUDENT_TODO: replace this */
    total = BUFSIZE; p = buf;
    while (total > 0) {
      // write the data from the audio buffer ("buf") to /dev/sb
       
      /* STUDENT_TODO: your code here */
      if (once) {
        // printf("start device\n"); 
        config_sbctl(SB_CMD_START, 0/*drv id*/,-1,-1);
        once = 0;
      }
    }
  }
done: 
  free(buf); return ret;   
}

// Returns 0 if successful
// legacy way to open audio device
// sdl2 should use SDL_OpenAudioDevice()
// https://wiki.libsdl.org/SDL2/SDL_OpenAudioDevice
//quest: music player
int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
  // TODO check "desired"
  int wrfmt; // as known to /proc/sbctl, cf kernel sound.c TSoundFormat 
  if (desired->format == AUDIO_U8) 
    wrfmt = 0; 
  else if (desired->format == AUDIO_S16)
    wrfmt = 1; 
  else
    return -1; 

  if (desired->channels>2) return -1; 

  if (config_sbctl(SB_CMD_WR_FMT, 0/*drv id*/, wrfmt, desired->channels)!=0)
    return -1; // failed

  // call clone() to create a new thread out of thread_func()
   
  /* STUDENT_TODO: your code here */

  return 0; /* STUDENT_TODO: replace this */
}

void SDL_CloseAudio() {
  __atomic_store_n(&cls, 1, __ATOMIC_SEQ_CST);  // cls=1
  if (wait(0)<0) printf("warn: worker thread stopped early\n"); 
  else printf("worker thread stopped\n"); 
  config_sbctl(SB_CMD_FINI, 0/*drv*/,-1,-1);
  printf("audio closed");  // wont close /dev/sb until sdl_fini_audio()
}

// This function is a legacy means of pausing the audio device.
// https://wiki.libsdl.org/SDL2/SDL_PauseAudio
// new API: SDL_PauseAudioDevice()
void SDL_PauseAudio(int pause_on) {
  ps = pause_on; 
}

void SDL_MixAudio(uint8_t *dst, uint8_t *src, uint32_t len, int volume) {
}

SDL_AudioSpec *SDL_LoadWAV(const char *file, SDL_AudioSpec *spec, uint8_t **audio_buf, uint32_t *audio_len) {
  return NULL;
}

void SDL_FreeWAV(uint8_t *audio_buf) {
}

void SDL_LockAudio() {
}

void SDL_UnlockAudio() {
}
