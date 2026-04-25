//doomgeneric for cross-platform development library 'Simple DirectMedia Layer'

#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>

#include <stdbool.h>
#include "SDL.h"

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
SDL_Texture* texture;

#define KEYQUEUE_SIZE 16

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

// https://doom.fandom.com/wiki/Controls
// many of the controls also mapped to Waveshare's RPI GAMEHAT
//quest: DOOM. remap the controls as you like 
static unsigned char convertToDoomKey(unsigned int key){
  // key &= 0xffff;  // fxl: exclude SDLK_SCANCODE_MASK
  switch (key)
    {
    /* confirm in game menu. unused in gameplay? */
    case SDLK_RETURN:   // gamehat, "start" 
      key = KEY_ENTER; 
      break;

    /* escape to game menu */
    case SDLK_ESCAPE:
    case SDLK_PAGEUP:   // gamehat, left trigger
    // case SDLK_TAB:
      key = KEY_ESCAPE;
      break;

    case SDLK_LEFT:
      key = KEY_LEFTARROW;
      break;
    case SDLK_RIGHT:
      key = KEY_RIGHTARROW;
      break;
    case SDLK_UP:
      key = KEY_UPARROW;
      break;
    case SDLK_DOWN:
      key = KEY_DOWNARROW;
      break;
    
    /* fire */
    // case SDLK_LCTRL:   
    // case SDLK_RCTRL:
    case SDLK_z:  // our /dev/events have no ctrl. cf kernel/kb.c 
    case SDLK_a:  // gamehat, 'A'
      key = KEY_FIRE;
      break;
    
    case SDLK_SPACE:
    case SDLK_x:  // gamehat, 'X'
      key = KEY_USE;  // (like open door?
      break;
    
    /* hold it and use arrow to "run" */
    // case SDLK_LSHIFT:
    // case SDLK_RSHIFT:
    case SDLK_r:  // fxl: ditto
    case SDLK_PAGEDOWN:  // gamehat, right trigger
      key = KEY_RSHIFT; 
      break;

    /* strafe in game */
    // case SDLK_LALT:
    // case SDLK_RALT:
    case SDLK_s: // fxl: ditto
    case SDLK_y:  // gamehat, Y
    // case SDLK_PAGEUP:  // gamehat, left trigger
      key = KEY_LALT; 
      break;

    // below not mapped to gamehat
    case SDLK_F2:
      key = KEY_F2;
      break;
    case SDLK_F3:
      key = KEY_F3;
      break;
    case SDLK_F4:
      key = KEY_F4;
      break;
    case SDLK_F5:
      key = KEY_F5;
      break;
    case SDLK_F6:
      key = KEY_F6;
      break;
    case SDLK_F7:
      key = KEY_F7;
      break;
    case SDLK_F8:
      key = KEY_F8;
      break;
    case SDLK_F9:
      key = KEY_F9;
      break;
    case SDLK_F10:
      key = KEY_F10;
      break;
    case SDLK_F11:
      key = KEY_F11;
      break;
    case SDLK_EQUALS:
    case SDLK_PLUS:
      key = KEY_EQUALS;
      break;
    case SDLK_MINUS:
      key = KEY_MINUS;
      break;
    default:
      // key = tolower(key); // fxl: caused exception... maybe link to some nonexisting code? ESR 0x92000005 why?
      if (key >= 'A' && key <= 'Z') {
        key = key + ('a' - 'A');
      }
      break;
    }

  return key;
}

// quest: DOOM
static void addKeyToQueue(int pressed, unsigned int keyCode){
  unsigned char key = convertToDoomKey(keyCode);

  unsigned short keyData = (pressed << 8) | key;
  // if (pressed)
  //   printf("addKeyToQueue  pressed keyCode %x doomkey %x\n", keyCode, key);

   
  /* STUDENT_TODO: your code here */
}

// retrive (as many as possible) key events from the OS. 
// queue the events so that they can be consumed by the game later. (same thread, so no lock needed)
// called from rendering loop. non blocking.
// quest: DOOM
static int evflags = SDL_EV_HW;
static void handleKeyInput(){
  SDL_Event e;
  while (SDL_PollEvent(&e, evflags)){
    if (e.type == SDL_QUIT){
      puts("Quit requested");
      atexit(SDL_Quit);
      exit(1);
    } 
    if (e.type == SDL_KEYDOWN) {
      // printf("KeyPress:%d \n", e.key.keysym.sym);
      /* STUDENT_TODO: your code here */
    } else if (e.type == SDL_KEYUP) {
      // printf("KeyRelease:%d \n", e.key.keysym.sym);
      /* STUDENT_TODO: your code here */
    }
  }
}

/* Usage 
  # sw surface, offset at 10, 10
  ./doom [args] -offsets 10 10 
  # (default, hw surface) 
    
*/
void DG_Init(){
  int x=0, y=0, flags=(SDL_WINDOW_HWSURFACE|SDL_WINDOW_FULLSCREEN); 

  int p = M_CheckParmWithArgs("-offset", 2 /* # of following args */);
  if (p > 0) { // screen locations given. use sw surface
    x=atoi(myargv[p+1]); y=atoi(myargv[p+2]);
    flags=SDL_WINDOW_SWSURFACE;
    evflags = SDL_EV_SW; 
  }

  window = SDL_CreateWindow("DOOM",
                            x, y, 
                            DOOMGENERIC_RESX,
                            DOOMGENERIC_RESY,
                            flags 
                            );

  // Setup renderer
  renderer =  SDL_CreateRenderer( window, -1, 0 /* dont care */);
  // Clear winow
  SDL_RenderClear( renderer );
  // Render the rect to the screen
  SDL_RenderPresent(renderer);

  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, 
    0 /*dont care*/, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
}

void fxl_check() {}

void DG_DrawFrame()
{
  SDL_UpdateTexture(texture, NULL, DG_ScreenBuffer, DOOMGENERIC_RESX*sizeof(uint32_t));
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);
  handleKeyInput();
}

void DG_SleepMs(uint32_t ms)
{
  SDL_Delay(ms);
}

//quest: DOOM
uint32_t DG_GetTicksMs()
{
  // call SDL function to get ticks
  return 0; /* STUDENT_TODO: replace this */
}

// called by game loop to retrieve one event. nonblocking
// "pressed" and "doomKey" are output params
//quest: DOOM
int DG_GetKey(int* pressed, unsigned char* doomKey)
{
  unsigned short keyData = 0; 

  // printf("fxl: %s %d\n", __FILE__, __LINE__); 
  if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex){
    //key queue is empty
    return 0;
  }else{
     
    /* STUDENT_TODO: your code here */
    *pressed = keyData >> 8;
    *doomKey = keyData & 0xFF;
    return 1;
  }

  return 0;
}

void DG_SetWindowTitle(const char * title)
{
  if (window != NULL){
    SDL_SetWindowTitle(window, title);
  }
}

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);

    for (int i = 0; ; i++)
    {
        doomgeneric_Tick();
    }
    

    return 0;
}