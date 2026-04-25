#ifndef FB_H
#define FB_H
struct fb_struct {
    unsigned char *fb;  // framebuffer, kernel va
    unsigned width, height, vwidth, vheight, pitch; 
    unsigned scr_width, scr_height; // screen dimension probed before init fb
    unsigned depth; 
    unsigned isrgb; 
    unsigned offsetx, offsety; 
    unsigned size; 
}; 

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

extern volatile unsigned char _binary_font_psf_start;

/* Scalable Screen Font (https://gitlab.com/bztsrc/scalable-font2) */
typedef struct {
    unsigned char  magic[4];
    unsigned int   size;
    unsigned char  type;
    unsigned char  features;
    unsigned char  width;
    unsigned char  height;
    unsigned char  baseline;
    unsigned char  underline;
    unsigned short fragments_offs;
    unsigned int   characters_offs;
    unsigned int   ligature_offs;
    unsigned int   kerning_offs;
    unsigned int   cmap_offs;
} __attribute__((packed)) sfn_t;

extern volatile unsigned char _binary_font_sfn_start; // linker script

extern struct fb_struct the_fb; //mbox.c
extern struct spinlock mboxlock; 

#define PIXELSIZE 4 /*ARGB, in hw framebuffer also expected by /dev/fb*/ 

#endif
