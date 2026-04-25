/*
Principle: minimum OS requirement. NO dependency on:
    -- thread (CLONE_VM, spinlock, semaphore, etc)
    -- non-blocking IO (O_NONBLOCK) 
    -- SDL_xxx (libminisdl) functions (hence directly writes to /dev/fb)

Event dispatch is implemented as multiple tasks (processes) communicating with
pipelines. 

Known issues: minor flickring & glitches at the canvas top (TBD)

---- Below: comment from NJU OS project ---- 
https://github.com/NJU-ProjectN/LiteNES/blob/master/src/hal.c

This file presents all abstractions needed to port LiteNES.
  (The current working implementation uses allegro library.)

To port this project, replace the following functions by your own:
1) nes_hal_init()
    Do essential initialization work, including starting a FPS HZ timer.

2) nes_set_bg_color(c)
    Set the back ground color to be the NES internal color code c.

3) nes_flush_buf(*buf)
    Flush the entire pixel buf's data to frame buffer.

4) nes_flip_display()
    Fill the screen with previously set background color, and
    display all contents in the frame buffer.

5) wait_for_frame()
    Implement it to make the following code is executed FPS times a second:
        while (1) {
            wait_for_frame();
            do_something();
        }

6) int nes_key_state(int b) 
    Query button b's state (1 to be pressed, otherwise 0).
*/
#include "hal.h"
#include "fce.h"
#include "common.h"
#include "../user.h"

#define FB_FLIP  1   // use double framebuffer (fb) buffering?

static int cur_id = 0; // id of fb being rendered to, 0 or 1 (if FB_FLIP==1)

static char *vtx = 0;  // byte-to-byte mirror of hw fb (inc row padding - pitch)
static int vtx_sz = 0; // in bytes

static int dispinfo[MAX_DISP_ARGS]={0}; // display config, loaded from procfs
static int fb = 0;              // /dev/fb
static int fds[2];              //pipes for exchanging events

// input events
struct event {
    int type; 
#define EV_INVALID      INVALID     // consistent with read_kb_event (user.h)
#define EV_KEYDOWN      KEYDOWN     // ditto
#define EV_KEYUP        KEYUP       // ditto
#define EV_TIMER        (KEYUP+10)
    int scancode;   // for EV_KEY
}; 

// keyboard: key state 
char key_states[NUM_SCANCODES] = {EV_KEYUP}; // init: all EV_KEYUP

/* Wait until next timer event is fired. */
void wait_for_frame()
{
    struct event ev; 

    if (fds[0]<=0) {printf("fatal:pipe invalid\n"); exit(1);};

    while (1) { 
        if (read(fds[0], &ev, sizeof ev) != sizeof ev) {
            printf("read ev failed"); exit(1); 
        }
        switch (ev.type)
        {
        case EV_TIMER:
            return;     
        case EV_KEYDOWN: 
        case EV_KEYUP:
            assert(ev.scancode<NUM_SCANCODES); 
            key_states[ev.scancode]=ev.type;   
            // printf("key code %d %s\n", ev.scancode, ev.type == EV_KEYDOWN ? "down":"up");
            break;      // continue to wait for timer ev   
        default:
            printf("unknown ev"); exit(1); 
            break;
        }
    }
    /* Project idea: support graceful exit. For a special key press (e.g. 'q'),
    returns from this func and goes back to fce_run which further exits the
    program but before that, need to notify the timer task & event task to quit
    too; the main task shall wait() them to quit.    */
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
    int pitch = dispinfo[PITCH];
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
    int pitch = dispinfo[PITCH]; //in bytes

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
            // Below: original code scales fce's raw pixels by 2x for larger display;
            //   since Rpi3 GPU will scale fb to display anyway, we don't need this
            // Keep it here for reference. 
            // setpixel(vtx, x*2,      y*2,    pitch, c); 
            // setpixel(vtx, x*2+1,    y*2,    pitch, c); 
            // setpixel(vtx, x*2,      y*2+1,  pitch, c); 
            // setpixel(vtx, x*2+1,    y*2+1,  pitch, c); 
        }
    }
}

#define LINESIZE 128    // size of linebuf for reading /procfs
#define FPS_HZ  60 

/* Initialization: (1) start a 1/FPS Hz timer. (2) create tasks that produce
    timer/kb events & dispatch them to the main task over pipes */
void nes_hal_init() {
    int n; 
    struct event ev; 

    if (pipe(fds) != 0) { // create pipe for passing events
        printf("%s: pipe() failed\n", __func__);
        exit(1);
    }

    // timer task, wakeup every frame and writes to pipe
    if (fork() == 0) { 
        close(fds[0]);
        ev.type = EV_TIMER; 
        printf("timer task running\n");
        while (1) {            
            sleep(1000/FPS_HZ); // in ms
            if(write(fds[1], &ev, sizeof ev) != sizeof ev){
                printf("write timerev failed"); 
                exit(1);
            }            
        }
        exit(0); // shall never reach here
    }

    // keyboard task, read kb events and writes to pipe
    if (fork() == 0)  { 
        close(fds[0]);
        int events = open("/dev/events", O_RDONLY); assert(events>0); 
        int evtype; unsigned int scancode; 
        printf("input task running\n");

        while (1) {
        #if 0 
            // read a line from /dev/events and parse it into key events
            // line format: [kd|ku] 0x12
            n = read(events, buf, LINESIZE); assert(n>0); 
            s=buf; ev.type=EV_INVALID; ev.scancode = 0; // init
            if (buf[0] == 'k' && buf[1] == 'd') {
                ev.type = EV_KEYDOWN;
            } else if (buf[0] == 'k' && buf[1] == 'u') {
                ev.type = EV_KEYUP;
            }
            s += 2;
            while (*s == ' ')
                s++;
            if (s[0] == '0' && s[1] == 'x')
                ev.scancode = atoi16(s);
        #endif            
            n = read_kb_event(events, &evtype, &scancode);
            if (n||scancode==0||scancode>=NUM_SCANCODES||evtype==EV_INVALID) {
                printf("read_kb_event failed\n"); 
            } else { // pass the kb event to main task 
                // printf("evtype %d scancode %d\n", evtype, (int)scancode); 
                ev.type = evtype; ev.scancode = (int)scancode;
                if(write(fds[1], &ev, sizeof ev) != sizeof ev) {
                    printf("write keyev failed\n"); exit(1);
                }
            }
        }
        exit(0); // shall never reach here
    }
    close(fds[1]); 

    fb = open("/dev/fb/", O_RDWR); assert(fb>0); 
    
    // Configure fb hardware via procfs
    // ask for a vframebuf that scales the native fce canvas by 2x
    n = config_fbctl(SCREEN_WIDTH, SCREEN_HEIGHT,    // desired viewport ("phys")
#ifdef FB_FLIP
        SCREEN_WIDTH, 2*SCREEN_HEIGHT,0,0);  // two fbs, each contig (can be write to /dev/fb in a shot
#else        
        SCREEN_WIDTH, SCREEN_HEIGHT,0,0); 
#endif        
    assert(n==0); 

    read_dispinfo(dispinfo, &n); assert(n>0); 
    printf("FB_FLIP=%d SCREEN_WIDTH=%d SCREEN_HEIGHT=%d\n ", 
        FB_FLIP, SCREEN_WIDTH, SCREEN_HEIGHT); 
    printf("/proc/dispinfo: width %d height %d vwidth %d vheight %d"
        "pitch %d depth %d isrgb %d\n", 
        dispinfo[WIDTH], dispinfo[HEIGHT], dispinfo[VWIDTH], dispinfo[VHEIGHT], 
        dispinfo[PITCH], dispinfo[DEPTH], dispinfo[ISRGB]); 

    // Calc vtx size and alloc buf. NB: pitch is in bytes
    vtx_sz = dispinfo[PITCH] * dispinfo[VHEIGHT]; 
    vtx = malloc(vtx_sz);
    if (!vtx) {printf("failed to alloc vtx\n"); exit(1);}
    printf("fb alloc ...ok\n"); 

    // TODO: free vtx in graceful exit (if implemented
}

/* Update screen at FPS rate by drawing function. 
   Timer ensures this function is called FPS times a second. */
void nes_flip_display()
{
    int n;     
    assert(fb && vtx && vtx_sz); 
    
#ifdef FB_FLIP
    int sz = SCREEN_HEIGHT*dispinfo[PITCH]; 
    n = lseek(fb, cur_id*sz, SEEK_SET); assert(n==cur_id*sz); 
    if ((n=write(fb, vtx+cur_id*sz, sz)) != sz) {   // a bug??
        printf("%s: failed to write to hw fb. fb %d sz %d ret %d\n",
            __func__, fb, sz, n); 
    }    
    // make the current fb visible on display 
    n = config_fbctl(0,0,0,0/*dc*/, 0/*xoff*/, cur_id*SCREEN_HEIGHT/*yoff*/);
    assert(n==0);     
    cur_id = 1-cur_id; 
#else
    n = lseek(fb, 0, SEEK_SET); assert(n==0); 
    if ((n=write(fb, vtx, vtx_sz)) != vtx_sz) {
        printf("%s: failed to write to hw fb. fb %d vtx_sz %d ret %d\n",
            __func__, fb, vtx_sz, n); 
    }
#endif
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
            return (key_states[KEY_A]==EV_KEYDOWN || key_states[KEY_X]==EV_KEYDOWN);
        case 2: // B  (b/gamehat or z/keyboard)
            return (key_states[KEY_B]==EV_KEYDOWN || key_states[KEY_Z]==EV_KEYDOWN); 
        case 3: // SELECT (tab)
            return (key_states[KEY_TAB]==EV_KEYDOWN);
        case 4: // START  (enter)
            return (key_states[KEY_ENTER]==EV_KEYDOWN);
        case 5: // UP  (up arrow)
            return (key_states[KEY_UP]==EV_KEYDOWN);
        case 6: // DOWN (down arrow)
            return (key_states[KEY_DOWN]==EV_KEYDOWN);
        case 7: // LEFT (left arrow)
            return (key_states[KEY_LEFT]==EV_KEYDOWN);
        case 8: // RIGHT (right arrow)
            return (key_states[KEY_RIGHT]==EV_KEYDOWN);
        default:
            return 1;
    }
}

