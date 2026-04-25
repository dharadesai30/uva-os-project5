// #include <NDL.h>
#include "SDL.h"
#include "stdio.h"

extern int sdl_init_audio(void); // audio.c 
extern void sdl_fini_audio(void); // audio.c

int SDL_Init(uint32_t flags) {
  if (flags & SDL_INIT_AUDIO)
    return sdl_init_audio(); 
  return -1; 
}

void SDL_Quit() {
  printf("sdl quit\n"); 
  sdl_fini_audio(); 
}

char *SDL_GetError() {
  return "Navy does not support SDL_GetError()";
}

int SDL_SetError(const char* fmt, ...) {
  return -1;
}

int SDL_ShowCursor(int toggle) {
  return 0;
}

void SDL_WM_SetCaption(const char *title, const char *icon) {
}
