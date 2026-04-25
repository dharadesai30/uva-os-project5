/*
    the hal implementation with dependency on SDL, libc, etc.
    cf hal.c 

    render using SDL texture APIs. indeed too many copies: 
    nes's gfx -> texture (buf) -> renderer -> /dev/fb0 -> /dev/fb
    (TBD)

    - for simplicity, no fb flip
*/

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <SDL.h>

#include "hal.h"
#include "fce.h"
#include "common.h"

#define FPS_HZ  60 
// #define FPS_HZ  120

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
SDL_Texture* texture = NULL;

static int cur_id = 0; // id of fb being rendered to, 0 or 1 (if FB_FLIP==1)
static char *vtx = 0;  // the app's drawing buffer. will be copied to texture
static int vtx_sz = 0; // in bytes
static int fps = 0;
static int pitch = SCREEN_WIDTH*PIXELSIZE;

// keyboard: key state 
#define MAX_KEYSYM SDLK_z
int key_states[MAX_KEYSYM+1]; 

void wait_for_frame() 
{
    SDL_Event ev;
    static int total=0, cnt=0, last=0;
    int t0 = uptime_ms();
    int tosleep = MAX(0, 1000/FPS_HZ - (t0-last));
    
    total += (t0 - last); cnt ++; last = t0; 
    if (!(cnt % FPS_HZ)) {
        fps = 1000/(total/cnt); 
        // printf("%s: avg %d ms\n", __func__, total/cnt);
        cnt = total = 0; 
    }

    while (SDL_PollEvent(&ev, SDL_EV_SW)) {
        if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
            ev.key.keysym.sym &= ~SDLK_SCANCODE_MASK;
            if (ev.key.keysym.sym<=MAX_KEYSYM)
                key_states[ev.key.keysym.sym]=ev.type;
            else 
                printf("warning: ignored unknown key %d\n", ev.key.keysym.sym);
            // TBD: clean up & quit 
            // switch (ev.key.keysym.sym) {
            //     case SDLK_q: goto cleanup; break; 
            // }
        }
    }
    SDL_Delay(tosleep);
}

// id: fb 0 or 1
static inline void setpixel(int id, char *buf, int x, int y, int pit, PIXEL p) {
    assert(x>=0 && y>=0); 
    y += (id*SCREEN_HEIGHT);
    *(PIXEL *)(buf + y*pit + x*PIXELSIZE) = p; 
}

static inline PIXEL getpixel(int id, char *buf, int x, int y, int pit) {
    assert(x>=0 && y>=0); 
    y += (id*SCREEN_HEIGHT);
    return *(PIXEL *)(buf + y*pit + x*PIXELSIZE); 
}

#define fcecolor_to_pixel(color) \
(((char)color.r<<16)|((char)color.g<<8)|((char)color.b))

/* Set background color to vtx. RGB value of c is defined in fce.h */
void nes_set_bg_color(int c)
{
    PIXEL p = fcecolor_to_pixel(palette[c]);
    for (int y = 0; y < SCREEN_HEIGHT; y++) 
        for (int x = 0; x < SCREEN_WIDTH; x++)
            setpixel(cur_id,vtx,x,y,pitch,p); 
}

/* Flush the pixel buffer. Materialize nes's drawing (unordered pixels) to fb.
         this lays out pixels in memory (in hw format)
   ver1: draw to an app fb, which is to be write to /dev/fb in one shot
   ver2: draw to a "back" fb (which will be made visible later)

    fb basics
    https://github.com/fxlin/p1-kernel/blob/master/docs/exp3/fb.md */
void nes_flush_buf(PixelBuf *buf) {
    for (int i = 0; i < buf->size; i ++) {
        Pixel *p = &buf->buf[i];
        int x = p->x, y = p->y;
        pal color = palette[p->c];
        PIXEL c = fcecolor_to_pixel(color);

        // Pixel could have coorindates x<0 (looks like fce shifts drawn
        //  pixels by applying offsets to them). These pixels shall be
        //  invisible on fb. 
        assert(x<SCREEN_WIDTH && y>=0 && y<SCREEN_HEIGHT);
        if (x>=0) {
            setpixel(cur_id,vtx,x,y,pitch,c); //1x scaling
        }
    }

    int x=0, y=0;
    char txt[15]; 
    snprintf(txt, 15, "FPS %d", fps);
    fb_print((unsigned char *)vtx, &x, &y, txt, pitch, 0x00FF0000/*red*/);
}

#define LINESIZE 128    // size of linebuf for reading /procfs
extern int xx,yy; // main-sdl.c

void nes_hal_init() {
    for (int i=0; i<MAX_KEYSYM+1; i++) 
        key_states[i]=SDL_KEYUP;        // init: all EV_KEYUP
    
    window = SDL_CreateWindow("NES", xx, yy, 
        SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SWSURFACE); assert(window);
    renderer = SDL_CreateRenderer(window,-1/*index*/,0/*flags*/); assert(renderer);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, 
        0 /*dont care*/, SCREEN_WIDTH, SCREEN_HEIGHT);
        
    // Calc vtx size and alloc buf. NB: pitch is in bytes
    vtx_sz = SCREEN_WIDTH * SCREEN_HEIGHT * PIXELSIZE; 
    vtx = malloc(vtx_sz);
    if (!vtx) {printf("failed to alloc vtx\n"); exit(1);}

    load_font("font.psf",0/*don'tcare*/,0/*don't care*/);  // so that we can display text with fb_print()

    // TODO: free vtx in graceful exit (if implemented
}

/* Update screen at FPS rate by drawing function. 
   Timer ensures this function is called FPS times a second. 
   On rpi3 hw, takes ~3--4 ms
*/
void nes_flip_display()
{
    // static unsigned total=0, cnt=0;
    // unsigned t0 = uptime_ms(); 

    assert(vtx && vtx_sz); 
    SDL_UpdateTexture(texture, NULL, vtx, SCREEN_WIDTH*PIXELSIZE);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);

    // total += (uptime_ms() - t0); cnt ++; 
    // if (!(cnt % FPS_HZ)) {
    //     printf("%s: avg %d ms\n", __func__, total/cnt);
    //     cnt = total = 0; 
    // }
}

/* Query a button's state. b: the button idx. 
   Returns 1 if button #b is pressed. */
int nes_key_state(int b)
{
    switch (b)
    {
        case 0: // On / Off
            return 1;
        case 1: // A  (a/gamehat or x/keyboard)
            return (key_states[SDLK_a]==SDL_KEYDOWN || key_states[SDLK_x]==SDL_KEYDOWN);
        case 2: // B  (b/gamehat or z/keyboard)
            return (key_states[SDLK_b]==SDL_KEYDOWN || key_states[SDLK_z]==SDL_KEYDOWN); 
        case 3: // SELECT (tab)
            return (key_states[SDLK_TAB]==SDL_KEYDOWN);
        case 4: // START  (enter)
            return (key_states[SDLK_RETURN]==SDL_KEYDOWN);
        case 5: // UP  (up arrow)
            return (key_states[SDLK_UP & ~SDLK_SCANCODE_MASK]==SDL_KEYDOWN);
        case 6: // DOWN (down arrow)
            return (key_states[SDLK_DOWN & ~SDLK_SCANCODE_MASK]==SDL_KEYDOWN);
        case 7: // LEFT (left arrow)
            return (key_states[SDLK_LEFT & ~SDLK_SCANCODE_MASK]==SDL_KEYDOWN);
        case 8: // RIGHT (right arrow)
            return (key_states[SDLK_RIGHT & ~SDLK_SCANCODE_MASK]==SDL_KEYDOWN);
        default:
            return 1;
    }
}

