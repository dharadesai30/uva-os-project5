#include "stdio.h"
#include "stdint.h"

#include "libc-os.h"
#include "sdl-timer.h"

SDL_TimerID SDL_AddTimer(uint32_t interval, SDL_NewTimerCallback callback, void *param) {
  return NULL;
}

int SDL_RemoveTimer(SDL_TimerID id) {
  return 1;
}

// syscall-newlib.c
// extern unsigned int uptime_ms(void);  
// extern unsigned int msleep(unsigned int msec); 

// Get the number of milliseconds since SDL library initialization.
//quest: DOOM
uint32_t SDL_GetTicks() {
  return 0; /* STUDENT_TODO: replace this */
}

void SDL_Delay(uint32_t ms) {
  if (ms)
    msleep(ms); 
}
