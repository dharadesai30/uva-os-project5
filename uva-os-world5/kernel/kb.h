#ifndef _KB_H
#define _KB_H
// kb event, modeled after SDL keyboard event
// https://wiki.libsdl.org/SDL2/SDL_KeyboardEvent
struct kbevent {
    uint32 type;
#define KEYDOWN 0
#define KEYUP 1
    uint32 timestamp;
    uint8 mod;
    uint8 scancode;
};

struct kb_struct {
    struct spinlock lock;
    // input
#define INPUT_BUF_SIZE 8
    struct kbevent buf[INPUT_BUF_SIZE];
    uint r; // Read index
    uint w; // Write index
}; 

// needed by surface flinger to interpret surface commands (e.g. Ctrl-tab)
// https://gist.github.com/MightyPork/6da26e382a7ad91b5496ee55fdc73db2
#define KEY_MOD_LCTRL  0x01
#define KEY_MOD_LSHIFT 0x02
#define KEY_MOD_LALT   0x04
#define KEY_MOD_LMETA  0x08
#define KEY_MOD_RCTRL  0x10
#define KEY_MOD_RSHIFT 0x20
#define KEY_MOD_RALT   0x40
#define KEY_MOD_RMETA  0x80

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

#define KEY_ENTER 0x28 // Keyboard Return (ENTER)
#define KEY_TAB 0x2b // Keyboard Tab

#define KEY_PAGEUP 0x4b // Keyboard Page Up
#define KEY_PAGEDOWN 0x4e // Keyboard Page Down

#define KEY_RIGHT 0x4f // Keyboard Right Arrow
#define KEY_LEFT 0x50 // Keyboard Left Arrow
#define KEY_DOWN 0x51 // Keyboard Down Arrow
#define KEY_UP 0x52 // Keyboard Up Arrow

#endif

