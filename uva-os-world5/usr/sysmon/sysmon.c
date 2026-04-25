/* 
  Display CPU usage, on serial console; or display (/dev/fb or /deb/fb0)
  Depends on: SDL_xxx for drawing; libc (newlib)
*/

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "SDL.h"
#include "math.h"

#define SHOW_TEXT 1

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;

#define FPS 10
#define W 200
#define H 80

// each cpu: a horizontal bar
// below in pixels
#define BAR_SPACING (W/20) 
#define BAR_ORIGIN_X  BAR_SPACING // top-left of bar 0, wrt to the current window
#define BAR_ORIGIN_Y  BAR_SPACING 
#define BAR_MAX_LEN (W-2*BAR_SPACING)
#define BAR_HEIGHT ((H-2*BAR_SPACING)/(MAX_NCPU)-BAR_SPACING)

static_assert(BAR_MAX_LEN>0 && BAR_HEIGHT>0);

// video.c 
#define rgba_to_pixel(r, g, b, a) \
    (((char)a << 24) | ((char)r << 16) | ((char)g << 8) | ((char)b))

#define max(a, b) ((a) > (b) ? (a) : (b))

// #define COLOR_BKGND_R 56 
// #define COLOR_BKGND_G 56
// #define COLOR_BKGND_B 56
// #define COLOR_BKGND rgba_to_pixel(56,56,56,0)  
SDL_Color COLOR_BKGND = {.r=56, .g=56, .b=56, .a=0};  // gray

static SDL_Rect bars[MAX_NCPU]; 

void init_bars(void) {
  for (int i = 0; i < MAX_NCPU; i++) {
    bars[i].x = BAR_ORIGIN_X; 
    bars[i].y = BAR_ORIGIN_Y + BAR_HEIGHT*i + BAR_SPACING*i; 

    bars[i].h = BAR_HEIGHT;
    bars[i].w = BAR_MAX_LEN;
  }
}

SDL_Color util_to_color(int util) {
  SDL_Color c = {
    .r = util*255 / 100,
    .g = 0, 
    .b = 255 - c.r,
    .a = 0
  };
  return c; 
}

void visualize(int util[MAX_NCPU], int ncpus) {
  int i;
  static int last_util[MAX_NCPU];

  if (memcmp(last_util, util, sizeof(last_util)) == 0)  // why not working??
    return; 
  
  memcpy(last_util, util, sizeof(last_util)); 

  // set the bkgnd color to the avg cpu util, for "flashy" effect
  int avg_util=0;
  for (i=0; i<ncpus; i++) 
    avg_util += util[i];
  avg_util /= ncpus; // printf("avg util %d\n", avg_util);
  renderer->c = util_to_color(avg_util);
  SDL_RenderClear(renderer);

  for (i=0; i<ncpus; i++) {
    bars[i].w = BAR_MAX_LEN;
    renderer->c = COLOR_BKGND; SDL_RenderFillRect(renderer, bars+i);
    bars[i].w = util[i]*BAR_MAX_LEN/100;
    renderer->c = util_to_color(util[i]); SDL_RenderFillRect(renderer, bars+i);

#if SHOW_TEXT
    // draw text
    char buf[16]; 
    snprintf(buf, sizeof(buf), "CPU%d:%3d", i, util[i]);
    char *tgt = renderer->tgt[renderer->cur_id];
    int pitch = renderer->pitch;
    int xx = 10; // bars[i].x + BAR_MAX_LEN;
    int yy = bars[i].y + BAR_HEIGHT/2;
    fb_print((unsigned char*)tgt, &xx, &yy, 
      buf, pitch, rgba_to_pixel(255,255,255,0));
#endif
  }
  SDL_RenderPresent(renderer);
}

// visualize on the serial terminal 
#define BAR_MAX_LEN_CHAR 10 
void visual_console(int util[MAX_NCPU], int ncpus) {
  int i,j,len;
  // clear current line
  // https://stackoverflow.com/questions/1508490/erase-the-current-printed-console-line
  // printf("\33[2K\r");  // not working well??
  printf("\033[2J");  // reset the temrinal 
  for (i=0; i<ncpus; i++) {
    len = util[i]*BAR_MAX_LEN_CHAR/100;
    printf("cpu%d[",i); 
    for (j=0;j<len;j++) printf("*");
    for (j=0;j<BAR_MAX_LEN_CHAR-len;j++) printf(" ");
    printf("]   ");     
  }  
  printf("\n");
}

#define MOD_CON 1
#define MOD_FB 2
#define MOD_FB0 3

int mode=MOD_CON;

/* usage: 
  ./sysmon          # direct render to /dev/fb
  ./sysmon con      # no gfx. print to stdout
  ./sysmon -- [x][y]   # indirect render to /dev/fb0 with offset, floating, transparent
*/
int main(int argc, char *argv[]) {
  int util[MAX_NCPU], ncpu, isqemu; 
  int x=0,y=0,flags=0; 
  int evflags = SDL_EV_HW;

  if (argc>1) { 
    if (strcmp(argv[1], "con")==0) 
      mode=MOD_CON; 
    else {    
      mode=MOD_FB0; 
      x=atoi(argv[1]); y=atoi(argv[2]); 
      flags = (SDL_WINDOW_SWSURFACE|SDL_WINDOW_TRANSPARENCY|SDL_WINDOW_FLOATING); 
      evflags = SDL_EV_SW; 
    }
  } else 
    { mode=MOD_FB; flags = (SDL_WINDOW_HWSURFACE|SDL_WINDOW_FULLSCREEN); }

  if (mode!=MOD_CON) {
    window = SDL_CreateWindow("SYSMON", x, y, W, H, flags); assert(window);
    renderer = SDL_CreateRenderer(window,-1/*index*/,0/*flags*/); assert(renderer);
    // SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

#ifdef SHOW_TEXT    
    load_font("font.psf",0/*don'tcare*/,0/*don't care*/);
#endif
    init_bars(); 
  }

  while (1) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev, evflags)) {
      if (ev.type == SDL_KEYDOWN) {
        switch (ev.key.keysym.sym) {
          case SDLK_q: goto cleanup; break; 
        }
      }
    }
    SDL_Delay(1000 / FPS);
    read_cpuinfo(util, &ncpu, &isqemu); 
    // printf("isqemu = %d\n", isqemu);
    // for (int i=0; i<ncpu; i++) printf("%d ", util[i]);  // debugging
    // printf("\n"); 
    if (mode==MOD_CON)
      visual_console(util, ncpu);
    else 
      visualize(util, ncpu);
  }

cleanup:
  if (mode!=MOD_CON)
    SDL_Quit();

  return 0;
}