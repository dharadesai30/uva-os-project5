#ifndef __SDL_H__
#define __SDL_H__

#include <stdint.h>
#include <stdbool.h>

#include "libc-os.h" 

typedef bool SDL_bool;
typedef uint8_t  Uint8;
typedef int16_t  Sint16;
typedef uint16_t Uint16;
typedef int32_t  Sint32;
typedef uint32_t Uint32;
typedef int64_t  Sint64;
typedef uint64_t Uint64;

#define SDL_FALSE 0
#define SDL_TRUE  1


#ifdef __cplusplus
extern "C" {
#endif

#include "sdl-general.h"
#include "sdl-scancode.h"
#include "sdl-event.h"
#include "sdl-timer.h"
#include "sdl-video.h"
#include "sdl-audio.h"
#include "sdl-file.h"
#include "sdl-keycode.h"

#ifdef __cplusplus
}
#endif

#define SDLCALL

// define correct SDL key names
// #define SDLK_a   SDLK_A
// #define SDLK_b   SDLK_B
// #define SDLK_c   SDLK_C
// #define SDLK_d   SDLK_D
// #define SDLK_e   SDLK_E
// #define SDLK_f   SDLK_F
// #define SDLK_g   SDLK_G
// #define SDLK_h   SDLK_H
// #define SDLK_i   SDLK_I
// #define SDLK_j   SDLK_J
// #define SDLK_k   SDLK_K
// #define SDLK_l   SDLK_L
// #define SDLK_m   SDLK_M
// #define SDLK_n   SDLK_N
// #define SDLK_o   SDLK_O
// #define SDLK_p   SDLK_P
// #define SDLK_q   SDLK_Q
// #define SDLK_r   SDLK_R
// #define SDLK_s   SDLK_S
// #define SDLK_t   SDLK_T
// #define SDLK_u   SDLK_U
// #define SDLK_v   SDLK_V
// #define SDLK_w   SDLK_W
// #define SDLK_x   SDLK_X
// #define SDLK_y   SDLK_Y
// #define SDLK_z   SDLK_Z

// keyboard.c
// int SDL_KeyboardInit(void);
SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode scancode);

// cf sdl-keycode.h
#if 0
// USB kb scancode, per hw specs (user.h)
// all the way to KEY_KPDOT, cf https://gist.github.com/MightyPork/6da26e382a7ad91b5496ee55fdc73db2  
// https://gist.github.com/MightyPork/6da26e382a7ad91b5496ee55fdc73db2
#define NUM_SCANCODES   0x64 
#define KEY_A 0x04 // Keyboard a and A
#define KEY_B 0x05 // Keyboard b and B
#define KEY_C 0x06 // Keyboard c and C
#define KEY_D 0x07 // Keyboard d and D
#define KEY_E 0x08 // Keyboard e and E
#define KEY_F 0x09 // Keyboard f and F
#define KEY_G 0x0a // Keyboard g and G
#define KEY_H 0x0b // Keyboard h and H
#define KEY_I 0x0c // Keyboard i and I
#define KEY_J 0x0d // Keyboard j and J
#define KEY_K 0x0e // Keyboard k and K
#define KEY_L 0x0f // Keyboard l and L
#define KEY_M 0x10 // Keyboard m and M
#define KEY_N 0x11 // Keyboard n and N
#define KEY_O 0x12 // Keyboard o and O
#define KEY_P 0x13 // Keyboard p and P
#define KEY_Q 0x14 // Keyboard q and Q
#define KEY_R 0x15 // Keyboard r and R
#define KEY_S 0x16 // Keyboard s and S
#define KEY_T 0x17 // Keyboard t and T
#define KEY_U 0x18 // Keyboard u and U
#define KEY_V 0x19 // Keyboard v and V
#define KEY_W 0x1a // Keyboard w and W
#define KEY_X 0x1b // Keyboard x and X
#define KEY_Y 0x1c // Keyboard y and Y
#define KEY_Z 0x1d // Keyboard z and Z

#define KEY_1 0x1e // Keyboard 1 and !
#define KEY_2 0x1f // Keyboard 2 and @
#define KEY_3 0x20 // Keyboard 3 and #
#define KEY_4 0x21 // Keyboard 4 and $
#define KEY_5 0x22 // Keyboard 5 and %
#define KEY_6 0x23 // Keyboard 6 and ^
#define KEY_7 0x24 // Keyboard 7 and &
#define KEY_8 0x25 // Keyboard 8 and *
#define KEY_9 0x26 // Keyboard 9 and (
#define KEY_0 0x27 // Keyboard 0 and )

#define KEY_RIGHT 0x4f // Keyboard Right Arrow
#define KEY_LEFT 0x50 // Keyboard Left Arrow
#define KEY_DOWN 0x51 // Keyboard Down Arrow
#define KEY_UP 0x52 // Keyboard Up Arrow
#endif

#endif
