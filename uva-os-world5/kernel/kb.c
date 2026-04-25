#define K2_DEBUG_INFO

// USB keyboard
//  modeled after console.c

// #include <stdarg.h>

#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"
#include "sched.h"
#include "utils.h"
#include "kb.h"

struct kb_struct the_kb = {.r=0,.w=0};

#define NUM_SCANCODES   0x64 // all the way to KEY_KPDOT, cf https://gist.github.com/MightyPork/6da26e382a7ad91b5496ee55fdc73db2  
#define KEY_RELEASED    0
#define KEY_CONT_PRESSED 1
#define KEY_JUST_PRESSED 2

char key_states[NUM_SCANCODES] = {KEY_RELEASED}; // 0: keyup, 1: keydown

/* User read()s from the kb device file (/dev/events).
    Current design: will return at most one event (even if there are multiple in
    driver) to user read(); in the future, we may also change the behaviors to
    return a batch of events

    "user_dst": whether dst is a user or kernel address (1 means dst is user va)
    "n": user buffer size;  "blocking": blocking read?     
*/
//quest: music player
int kb_read(int user_dst, uint64 dst, int off, int n, char blocking, void *content) {
    uint target;
    struct kbevent ev;
#define TXTSIZE 20     
    char ev_txt[TXTSIZE]; 

    V("called user_dst %d", user_dst);

    target = n;
    acquire(&the_kb.lock);
    while (n > 0) {     // n:remaining space in userbuf
        // if nonblocking, and nothing to read from the driver, return immediately
         
        /* STUDENT_TODO: your code here */

        // wait until interrupt handler has put some
        // input into cons.buffer.
        while (the_kb.r == the_kb.w) {
            if (killed(myproc())) {
                release(&the_kb.lock);
                return -1;
            }
            sleep(&the_kb.r, &the_kb.lock);
        }

        if (n < TXTSIZE) break; // no enough space in userbuf

        ev = the_kb.buf[the_kb.r++ % INPUT_BUF_SIZE];
        int len = snprintf(ev_txt, TXTSIZE, "%s 0x%02x\n", 
            ev.type == KEYDOWN ? "kd":"ku", ev.scancode); 

        BUG_ON(len < 0 || len >= TXTSIZE); // ev_txt too small

        if (n < len) break; // no enough space in userbuf (XXX should do kb.r--?)
        
        // copy the input byte to the user-space buffer.
        if (either_copyout(user_dst, dst, ev_txt, len) == -1)
            break;

        dst+=len; n-=len;        

        break; 
    }
    release(&the_kb.lock);

    return target - n;
}

// if buf full, drop event but still change key state
// above "mod" (modifier) -- the status of ctrl/shift/alt/meta...
// #define KEY_MOD_LCTRL  0x01
// #define KEY_MOD_LSHIFT 0x02
// #define KEY_MOD_LALT   0x04
// #define KEY_MOD_LMETA  0x08
// #define KEY_MOD_RCTRL  0x10
// #define KEY_MOD_RSHIFT 0x20
// #define KEY_MOD_RALT   0x40
// #define KEY_MOD_RMETA  0x80
// cf: https://gist.github.com/MightyPork/6da26e382a7ad91b5496ee55fdc73db2
// e.g. left alt key pressed 
// kb.c:115 mod 4 key 00 00 00 00 00 00
// the driver does not pass mod keys to userspace.
// however doom expects ctrl/alt key events... can be a project idea
void kb_intr(unsigned char mod, const unsigned char keys[6]) {
    unsigned char c;

    if (keys[0] == 1 /*KEY_ERR_OVF*/) // too many keys
        return;

    V("mod %u key %02x %02x %02x %02x %02x %02x", 
        mod, keys[0], keys[1], keys[2], keys[3], keys[4], keys[5]);

    acquire(&the_kb.lock);

    for (int i = 0; i < 6; i++) {
        c = keys[i];
        if (c > NUM_SCANCODES) {E("unknown code?"); continue;}
        if (c == 0) break; // no more scan code
        if (key_states[c] == KEY_RELEASED) { // released before, just pressed
            if (the_kb.w-the_kb.r < INPUT_BUF_SIZE) {
                struct kbevent ev = {
                    .type = KEYDOWN,
                    .mod = mod,
                    .scancode = c};
                the_kb.buf[the_kb.w++ % INPUT_BUF_SIZE] = ev;
            }
            key_states[c] = KEY_JUST_PRESSED;
        } else if (key_states[c] == KEY_CONT_PRESSED)
            key_states[c] = KEY_JUST_PRESSED; // renew the state
    }

    for (c = 2; c < NUM_SCANCODES; c++) {       //0,1 are nocode,ovf
        switch (key_states[c]) {
        case KEY_CONT_PRESSED: // pressed before, but not pressed in current scan
            if (the_kb.w-the_kb.r < INPUT_BUF_SIZE) {
                struct kbevent ev = {
                    .type = KEYUP,
                    .mod = mod,
                    .scancode = c};
                the_kb.buf[the_kb.w++ % INPUT_BUF_SIZE] = ev;
            }
            key_states[c] = KEY_RELEASED;
            break;
        case KEY_JUST_PRESSED:
            key_states[c] = KEY_CONT_PRESSED; // for future scan
            break;
        case KEY_RELEASED: // released before && not pressed
            break;
        default:
            BUG();
        }
    }

    wakeup(&the_kb.r);
    release(&the_kb.lock);
}

#include "uspios.h"
#include "uspi.h"

// return 0 on success
int usbkb_init(void) {

    initlock(&the_kb.lock, "kb");

	if (!USPiInitialize ()) {
		E("cannot init"); 
        return -1; 
	}
	if (USPiKeyboardAvailable ()) {
        USPiKeyboardRegisterKeyStatusHandlerRaw(kb_intr); 
        // still register handler below, as we may have gamehat gpio 
	} else 
        W("keyboard not found");
            
    devsw[KEYBOARD].read = kb_read;
    devsw[KEYBOARD].write = 0; // nothing
    return 0; 
}