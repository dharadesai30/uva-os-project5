#ifndef __SDL_EVENT_H__
#define __SDL_EVENT_H__

#if 0 
#define _KEYS(_) \
  _(ESCAPE) _(F1) _(F2) _(F3) _(F4) _(F5) _(F6) _(F7) _(F8) _(F9) _(F10) _(F11) _(F12) \
  _(GRAVE) _(1) _(2) _(3) _(4) _(5) _(6) _(7) _(8) _(9) _(0) _(MINUS) _(EQUALS) _(BACKSPACE) \
  _(TAB) _(Q) _(W) _(E) _(R) _(T) _(Y) _(U) _(I) _(O) _(P) _(LEFTBRACKET) _(RIGHTBRACKET) _(BACKSLASH) \
  _(CAPSLOCK) _(A) _(S) _(D) _(F) _(G) _(H) _(J) _(K) _(L) _(SEMICOLON) _(APOSTROPHE) _(RETURN) \
  _(LSHIFT) _(Z) _(X) _(C) _(V) _(B) _(N) _(M) _(COMMA) _(PERIOD) _(SLASH) _(RSHIFT) \
  _(LCTRL) _(APPLICATION) _(LALT) _(SPACE) _(RALT) _(RCTRL) \
  _(UP) _(DOWN) _(LEFT) _(RIGHT) _(INSERT) _(DELETE) _(HOME) _(END) _(PAGEUP) _(PAGEDOWN)

#define enumdef(k) SDLK_##k,

enum SDL_Keys {
  SDLK_NONE = 0,
  _KEYS(enumdef)
};

// fxl: above: define a bunch of key constants. note the order -- diff from alphabetic order 
// this seems how sdl does things. 
#endif

enum SDL_EventFlags {
  SDL_EV_HW = 0,  /* /dev/events */
  SDL_EV_SW = 1,  /* /dev/events0 */
};

enum SDL_EventType {
    /* Application events */
    SDL_QUIT = 0x100,    /**< User-requested quit */
    SDL_KEYDOWN = 0x300, /**< Key pressed */
    SDL_KEYUP,
    SDL_USEREVENT,
};

#define SDL_EVENTMASK(ev_type) (1u << (ev_type))

enum SDL_EventAction {
  SDL_ADDEVENT,
  SDL_PEEKEVENT,
  SDL_GETEVENT,
};

typedef struct {
  uint32_t sym;
} SDL_keysym;

typedef struct {
  uint32_t type;
  SDL_keysym keysym;
  SDL_Scancode scancode; 
} SDL_KeyboardEvent;

typedef struct {
  uint32_t type;
  int code;
  void *data1;
  void *data2;
} SDL_UserEvent;

typedef union {  // fxl: note it's not a struct
  uint32_t type;
  SDL_KeyboardEvent key;
  SDL_UserEvent user;
} SDL_Event;

int SDL_PushEvent(SDL_Event *ev);
int SDL_PollEvent(SDL_Event *ev, int flags);  // fxl: non blocking
int SDL_WaitEvent(SDL_Event *ev, int flags);
int SDL_PeepEvents(SDL_Event *ev, int numevents, int action, uint32_t mask);
uint8_t* SDL_GetKeyState(int *numkeys);

#endif
