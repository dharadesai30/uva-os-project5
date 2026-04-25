// fxl: from NJU project. the choice of SDL interfaces seems good.
// the plan is to implement the same SDL interface atop our kernel,
// then build/port apps atop the SDL interface

// in impl, we refer back to SDL API defs.

#include "SDL.h"
#include "assert.h"
#include "sdl-video.h"
// Below: newlib/libc/include/
#include "fcntl.h"  // not kernel/fcntl.h
#include "stdio.h"
#include "unistd.h"
#include "stdlib.h"
#include "string.h"

//////////////////////////////////////////
//// New APIs (renderer, window, texture)

// Create direct or indirect windows (i.e. /dev/fb vs /dev/fb0)
// flags are ignored. expected to be SDL_WINDOW_FULLSCREEN
//quest: desktop
SDL_Window *SDL_CreateWindow(const char *title,
                             int x, int y, int w,
                             int h, Uint32 flags) {
    int n, fb;
    char isfb = (flags & SDL_WINDOW_HWSURFACE)?1:0; // direct or indirect?

    if ((fb = open("/dev/fb/", O_RDWR)) <= 0) /* STUDENT_TODO: replace this */
        return 0;

    if (isfb) {
        // ask for a vframebuf that scales the window (viewport) by 2x
        if (config_fbctl(w, h,     // desired viewport ("phys")
                        w, h * 2, // two fbs, each contig, each can be written to /dev/fb in a write()
                        0, 0) != 0) {
            return 0;
        }
    } else {
         
        /* STUDENT_TODO: your code here */
    }

    SDL_Window *win = malloc(sizeof(SDL_Window)); assert(win);
    memset(win, 0, sizeof(SDL_Window));

    if (isfb) {
        read_dispinfo(win->dispinfo, &n); assert(n > 0);
        printf("/proc/dispinfo: width %d height %d vwidth %d vheight %d pitch %d depth %d\n",
            win->dispinfo[WIDTH], win->dispinfo[HEIGHT], win->dispinfo[VWIDTH],
            win->dispinfo[VHEIGHT], win->dispinfo[PITCH], win->dispinfo[DEPTH]);
    } else {
        win->dispinfo[WIDTH]=w; win->dispinfo[HEIGHT]=h; 
        // win->dispinfo[VWIDTH]=x; win->dispinfo[VHEIGHT]=y; // dirty: steal these fields for passing x/y
        win->dispinfo[PITCH]=w*PIXELSIZE; // no padding
    }
    win->fb = fb;
    win->flags = flags; 
    win->title = title; 
    return win;
}

void SDL_DestroyWindow(SDL_Window *win) {
    assert(win && win->fb);
    close(win->fb);
    free(win);
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *win,
                                 int index, Uint32 flags) {

    char isfb = (win->flags & SDL_WINDOW_HWSURFACE)?1:0; // hw surface or not 

    SDL_Renderer *render = malloc(sizeof(SDL_Renderer));
    assert(render);
    memset(render, 0, sizeof(SDL_Renderer));

    render->win = win;
    render->w = win->dispinfo[WIDTH];
    render->h = win->dispinfo[HEIGHT];
    render->pitch = win->dispinfo[PITCH];

    if (isfb) {
        assert(win->dispinfo[VHEIGHT] >= 2 * render->h); // enough for 2 tgts
        render->buf_sz = win->dispinfo[PITCH] * win->dispinfo[VHEIGHT];
        render->buf = malloc(render->buf_sz); assert(render->buf);
        render->tgt_sz = render->h * render->pitch;
        render->tgt[0] = render->buf;
        render->tgt[1] = render->buf + render->tgt_sz;
        render->cur_id = 0;
    } else { 
        render->buf_sz = render->pitch * render->h;
        render->buf = malloc(render->buf_sz); assert(render->buf);
        render->tgt_sz = render->buf_sz;
        render->tgt[0] = render->buf; 
        render->tgt[1] = 0; render->cur_id = 0; // no flipping 
    }

    return render;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer) {
    assert(renderer);
    free(renderer);
}

int SDL_SetRenderDrawColor(SDL_Renderer *renderer,
                           Uint8 r, Uint8 g, Uint8 b,
                           Uint8 a) {
    assert(renderer);
    renderer->c.r = r;
    renderer->c.g = g;
    renderer->c.b = b;
    renderer->c.a = a;
    return 0;
}

#define rgba_to_pixel(r, g, b, a) \
    (((char)a << 24) | ((char)r << 16) | ((char)g << 8) | ((char)b))

// quest: music player
static inline void setpixel(char *buf, int x, int y, int pit, PIXEL p) {
    assert(x >= 0 && y >= 0);
     
    /* STUDENT_TODO: your code here */
}

static inline PIXEL getpixel(char *buf, int x, int y, int pit) {
    assert(x >= 0 && y >= 0);
    return *(PIXEL *)(buf + y * pit + x * PIXELSIZE);
}

/* Clear the current rendering target with the drawing color. */
//quest: music player
int SDL_RenderClear(SDL_Renderer *rdr) {
    char *tgt = rdr->tgt[rdr->cur_id];

    int pitch = rdr->pitch;
    int SCREEN_HEIGHT = rdr->h;
    int SCREEN_WIDTH = rdr->w;

    PIXEL p = rgba_to_pixel(rdr->c.r, rdr->c.g, rdr->c.b, rdr->c.a);    
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < SCREEN_WIDTH; x++)
            setpixel(0, 0, 0, 0, 0); /* STUDENT_TODO: replace this */
    return 0;
}

/* Update the screen with any rendering performed since the previous call.
    Write window buf contents to /dev/fb(0) */
void SDL_RenderPresent(SDL_Renderer *rdr) {
    SDL_Window *win = rdr->win;
    int n;
    assert(rdr && rdr->buf && rdr->buf_sz);
    int sz = rdr->tgt_sz;

    n = lseek(win->fb, rdr->cur_id * sz, SEEK_SET);
    assert(n == rdr->cur_id * sz);
    if ((n = write(win->fb, rdr->tgt[rdr->cur_id], sz)) != sz) {
        printf("%s: failed to write to hw fb. fb %d sz %d ret %d\n",
               __func__, win->fb, sz, n);
    }

    if (win->flags & SDL_WINDOW_HWSURFACE) {
        // make the cur fb visible
        n = config_fbctl(0, 0, 0, 0 /*dc*/, 0 /*xoff*/, 
            rdr->cur_id * rdr->h /*yoff*/); assert(n == 0);
        rdr->cur_id = 1 - rdr->cur_id; // flip buf
    }
}

/* Create a texture for a rendering context. */
SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer,
                               Uint32 format,
                               int access /*ignored*/, int w, int h) {
    assert(format == SDL_PIXELFORMAT_RGB888); // only support this 

    SDL_Texture *tx = malloc(sizeof(SDL_Texture)); assert(tx);
    tx->w = w;
    tx->h = h;
    tx->buf_sz = w * h * sizeof(uint32_t);
    tx->buf = malloc(tx->buf_sz); assert(tx->buf);

    return tx;
}

void SDL_DestroyTexture(SDL_Texture *tx) {
    assert(tx && tx->buf);
    free(tx->buf);
    free(tx);
}

/* Update the given texture rectangle with new pixel data.
    rect: if NULL, update the entire texture
    pixels: the raw pixel data in the format of the texture
    pitch: # of bytes in a row of pixel data, including padding */
//quest: DOOM
int SDL_UpdateTexture(SDL_Texture *texture,
                      const SDL_Rect *r,
                      const void *pixels, int pitch) {
    // update whole texture
    if (!r) {
        assert(pitch == texture->w * PIXELSIZE); // TBD
        memcpy(texture->buf, pixels, texture->buf_sz);
    } else { // update only a rect
        assert(pitch == r->w * PIXELSIZE); // TBD
        char *dest = texture->buf + r->y * texture->w * PIXELSIZE + r->x * PIXELSIZE;
        const char *src = pixels;
        for (int y = 0; y < r->h; y++) {
            //copy one row at a time, from "pixels" to "texture"    
             
            /* STUDENT_TODO: your code here */
        }
    } 

    return 0;
}

/* Copy a portion of the texture to the current rendering target.
    srcrect, dstrect: NULL for the entire texture / rendering target */
//quest: DOOM
int SDL_RenderCopy(SDL_Renderer *rdr,
                   SDL_Texture *texture,
                   const SDL_Rect *srcrect,
                   const SDL_Rect *dstrect) {
    int sz = rdr->tgt_sz;
        
    // fast path: copy entire texture to the rendering target
    //    rendering tgt no row padding
    if (!srcrect && !dstrect) {
        if (texture->h == rdr->h &&
            texture->w == rdr->w && rdr->w * sizeof(PIXEL) == rdr->pitch) {
            // fast path: texture & rendering target same dimensions quest:complete
            memcpy(rdr->tgt[0], (void*)0xdeadbeef, 0); /* STUDENT_TODO: replace this */
        } else
        // TBD texture needs to be stretched to match rendering tgt
        // or update part of the tgt (not full), or use part of the texture
        assert(0);
    }
    return 0;
}

void SDL_SetWindowTitle(SDL_Window *window,
                        const char *title) { 
    printf("%s: window title set to [%s]\n", __func__, title); 
}

// rect==NULL to fill the entire target
// Returns 0 on success or a negative error code on failure
int SDL_RenderFillRect(SDL_Renderer * rdr,
                   const SDL_Rect * r) {
    char *tgt = rdr->tgt[rdr->cur_id];
    int pitch = rdr->pitch;
    PIXEL p = rgba_to_pixel(rdr->c.r, rdr->c.g, rdr->c.b, rdr->c.a);

    if (r) { // fill a region 
        for (int y = r->y; y < r->y + r->h; y++)
                for (int x = r->x; x < r->x + r->w; x++)
                    setpixel(tgt, x, y, pitch, p);
    } else { // fill the whole surface
        for (int y = 0; y < rdr->h; y++)
            for (int x = 0; x < rdr->w; x++)
                setpixel(tgt, x, y, pitch, p);
    }
    return 0; 
}

// Draw a line on the current rendering target.
// Returns 0 on success or a negative error code on failure
//quest: music player
int SDL_RenderDrawLine(SDL_Renderer * rdr,
                   int x1, int y1, int x2, int y2) {    
    // if (!(x1>=0 && y1>=0 && x2>=0 && y2>=0)) 
    //     printf("%d %d %d %d \n", x1,y1,x2,y2);
    assert(x1>=0 && y1>=0 && x2>=0 && y2>=0);
    assert(x1==x2); // TBD only support vertical line

    char *tgt = rdr->tgt[rdr->cur_id];
    int pitch = rdr->pitch;    
    PIXEL p = rgba_to_pixel(rdr->c.r, rdr->c.g, rdr->c.b, rdr->c.a);

    int yy1=y1, yy2=y2; 
    if (y1>y2) {yy1=y2;yy2=y1;}
    for (int y=yy1; y<yy2; y++) {            
        setpixel(0,0,0,0,0); /* STUDENT_TODO: replace this */
    }

    return 0; 
}

//////////////////////////////////////////
//// Old APIs (e.g. SDL_Surface)

// (fxl) unlike window/texture, surface APIs are more "direct", 
// allows app to access surface-provided pixels, which are 
// then written to /dev/fb. so less buffering vs. window APIs


// https://wiki.libsdl.org/SDL2/SDL_CreateRGBSurface
// depth: the depth of the surface in bits (only support 32 XXX)
// flags: cf SDL_HWSURFACE etc
// return 0 on failure
// XXX support /dev/fb only, dont support /dev/fb0 (interface limitation)
// XXX ignored: R/G/B/A mask, 
SDL_Surface* SDL_CreateRGBSurface(uint32_t flags, int w, int h, int depth,
    uint32_t Rmask, uint32_t Gmask, uint32_t Bmask, uint32_t Amask) {
    
    int n, fb; 
    int dispinfo[MAX_DISP_ARGS]; 
    char isfb = (flags & SDL_HWSURFACE)?1:0; // hw surface or not 

    if (depth!=32) return 0;
    
    if ((fb = open(isfb?"/dev/fb/":"/dev/fb0/", O_RDWR)) <= 0)
        return 0;

    if (isfb) {
        // ask for a vframebuf that scales the window (viewport) by 2x
        if (config_fbctl(w, h,     // desired viewport ("phys")
                        w, h * 2, // two fbs, each contig, each can be written to /dev/fb in a write()
                        0, 0) != 0) {
            return 0;
        }
    } else {
        // quest: complete the call below
        // only support x=y=0, limited by this func's interface
        int trans=100;
        if (flags & SDL_TRANSPARENCY) trans=80;        
        if (config_fbctl0(FB0_CMD_INIT,0,0,w,h,ZORDER_TOP,trans) != 0) return 0;
    }

    SDL_Surface * suf = calloc(1, sizeof(SDL_Surface)); assert(suf); 
    
    if (isfb) {
        read_dispinfo(dispinfo, &n); assert(n > 0);
        suf->w = dispinfo[VWIDTH]; assert(w<=dispinfo[VWIDTH]);
        suf->h = h;  assert(2*h <= dispinfo[VHEIGHT]); 
        suf->pitch = dispinfo[PITCH];   // app must respect this
        // if (suf->w * sizeof(PIXEL) != suf->pitch) {printf("TBD"); return 0;}
    } else {
        suf->w=w; suf->h=h; suf->pitch=suf->w*PIXELSIZE;
    }

    suf->pixels = calloc(suf->h, suf->pitch); assert(suf->pixels); 
    suf->fb = fb; 
    suf->cur_id = 0; 
    suf->flags = flags; 

    return suf; 
}

SDL_Surface* SDL_SetVideoMode(int w, int h, int bpp, uint32_t flags) {
    return SDL_CreateRGBSurface(flags, w, h, bpp, 0, 0, 0, 0); 
}

void SDL_FreeSurface(SDL_Surface *s) {
    if (!s) return; 
    if (s->pixels) free(s->pixels); 
    if (s->fb) close(s->fb); 
    free(s); 
}

// Perform a fast fill of a rectangle with a specific color.
// https://wiki.libsdl.org/SDL2/SDL_FillRect
// rect==NULL to fill the entire surface
// Returns 0 on success or a negative error code on failure
int SDL_FillRect(SDL_Surface *suf, SDL_Rect *r, uint32_t color) {
    if (r) { // fill a region 
        for (int y = r->y; y < r->y + r->h; y++)
                for (int x = r->x; x < r->x + r->w; x++)
                    setpixel((char *)suf->pixels, x, y, suf->pitch, color);            
    } else { // fill the whole surface
        for (int y = 0; y < suf->h; y++)
            for (int x = 0; x < suf->w; x++)
                setpixel((char *)suf->pixels, x, y, suf->pitch, color);
    }
    return 0; 
}

// legacy API
// Makes sure the given area is updated on the given screen. 
// The rectangle must be confined within the screen boundaries (no clipping is done).
// If 'x', 'y', 'w' and 'h' are all 0, SDL_UpdateRect will update the entire surface
// https://www.libsdl.org/release/SDL-1.2.15/docs/html/sdlupdaterect.html
void SDL_UpdateRect(SDL_Surface *suf, int x, int y, int w, int h) {
    int n; 
    assert(x==0 && y==0 && w==0 && h==0); // only support full scr now
    int sz = suf->h * suf->pitch; 
    char isfb = (suf->flags & SDL_HWSURFACE)?1:0; // hw surface or not 
    
    if (isfb) { // /dev/fb
        n = lseek(suf->fb, suf->cur_id * sz, SEEK_SET);
        assert(n == suf->cur_id * sz); 
        if ((n = write(suf->fb, suf->pixels, sz)) != sz) {
            printf("%s: failed to write to hw fb. fb %d sz %d ret %d\n",
                __func__, suf->fb, sz, n);
        }
        // make the cur fb visible
        n = config_fbctl(0, 0, 0, 0 /*dc*/, 0 /*xoff*/, suf->cur_id * suf->h /*yoff*/);
        assert(n == 0);
        suf->cur_id = 1 - suf->cur_id;
    } else {    // /dev/fb0
        n = lseek(suf->fb, 0, SEEK_SET); assert(n==0); 
        if ((n = write(suf->fb, suf->pixels, sz)) != sz) {
            printf("%s: failed to write to hw fb. fb %d sz %d ret %d\n",
                __func__, suf->fb, sz, n);
        }
    }
}

// https://wiki.libsdl.org/SDL2/SDL_LockSurface
// "Between calls to SDL_LockSurface() / SDL_UnlockSurface(), 
// you can write to and read from surface->pixels, using the pixel format 
// stored in surface->format. Once you are done accessing the surface, 
// you should use SDL_UnlockSurface() to release it."

// Set up a surface for directly accessing the pixels.
// Returns 0 on success
int SDL_LockSurface(SDL_Surface *s) {
    return 0; 
}

// Release a surface after directly accessing the pixels.
void SDL_UnlockSurface(SDL_Surface *s) {
}


////
// render text to an existing pixel buffer, atop existing content

/* PC Screen Font as used by Linux Console */
typedef struct {
    unsigned int magic;
    unsigned int version;
    unsigned int headersize;
    unsigned int flags;
    unsigned int numglyph;
    unsigned int bytesperglyph;
    unsigned int height;
    unsigned int width;
    unsigned char glyphs;
} __attribute__((packed)) psf_t;

static psf_t *font = 0;     // static lib, we should be fine with global var

// width,height: (out) filled / the size of the loaded font (if not null)
// return 0 on success
int load_font(const char *path, int *width, int *height) {
  int ret; 

  FILE *fp = fopen(path, "r");
  if (!fp) return -1;
  
  /* read the whole font file  */
  fseek(fp, 0, SEEK_END);
  size_t size = ftell(fp);
  font = (psf_t *)malloc(size);  // alloc buf as large as the file
  assert(size); 
  fseek(fp, 0, SEEK_SET);
  ret = fread(font, size, 1, fp);
  assert(ret == 1);
  fclose(fp);

  if (width) *width = font->width;
  if (height) *height = font->height;
  
//   printf("loaded font %s %lu KB magic 0x%x\n", path, size, font->magic);
  return 0; 
}

// #ifndef PIXELSIZE
// #define PIXELSIZE 4 /*ARGB, expected by /dev/fb*/ 
// #endif

/* Display a string using fixed size PSF update x,y screen coordinates
  fb: existing pixel buffer
  x/y IN|OUT: the postion (in pixel) before/after text rendering

  return 0 on success, -1 on error 
*/
int fb_print(unsigned char *fb, int *x, int *y, char *s, int pitch /* in bytes*/, 
    unsigned color)
{
    if (!font) return -1; 

    // draw next character if it's not zero
    while(*s) {
        // get the offset of the glyph. Need to adjust this to support unicode table
        unsigned char *glyph = (unsigned char*)font +
         font->headersize + (*((unsigned char*)s)<font->numglyph?*s:0)*font->bytesperglyph;
        // calculate the offset on screen
        int offs = (*y * pitch) + (*x * 4);
        // variables
        int i,j, line,mask, bytesperline=(font->width+7)/8;
        // handle carrige return
        if(*s == '\r') {
            *x = 0;
        } else
        // new line
        if(*s == '\n') {
            *x = 0; *y += font->height;
        } else {
            // display a character
            for(j=0;j<font->height;j++){
                // display one row of pixels
                line=offs;
                mask=1<<(font->width-1);
                for(i=0;i<font->width;i++){
                    // if bit set, we use white color, otherwise black
                    // fxl: "mask" extracts a bit from the glyph. if the bit is 
                    // set (1), meaning the pixel should be drawn; otherwise not
                    // *((unsigned int*)(fb + line))=((int)*glyph) & mask?0xFFFFFF:0;
                    // fxl: below: only draw a pixel if the bit is set; otherwise
                    //  do NOT draw
                    if (((int)*glyph) & mask)
                      *((unsigned int*)(fb + line))=color;
                    mask>>=1;
                    line+=4;
                }
                // adjust to next line
                glyph+=bytesperline;
                offs+=pitch;
            }
            *x += (font->width+1);
        }
        // next character
        s++;
    }

    return 0; 
}
