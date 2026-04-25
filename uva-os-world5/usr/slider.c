/* 
  display a sequence of slides (as bmps), controlled by usb keyboard

  - an example of self contained user app
    low dependency: no newlib, minisdl, snprintf, sscanf; 
  as such, this file includes small implementation of common functions like atoi
  - also a basic test case for /dev/fb /dev/fb0 etc
  - About fb: https://github.com/fxlin/p1-kernel/blob/master/docs/exp3/fb.md

  Mar 2024 FL

  Inspired by nslider 
  https://github.com/NJU-ProjectN/navy-apps/blob/master/apps/nslider/src/main.cpp
  
  How to gen slides: see slides/conver.sh 

  USAGE:
  ./slider      # direct mode. will use /dev/fb & /dev/events
  ./slider X Y W H    # indirect mode.  will use /dev/fb0 & /dev/events0
    -X/Y/W/H surface location; -1 for default values
 */

#include <stdint.h> 
#include <assert.h>

#include "user.h"

const char * usage = \
  "Keys:\n"
  "* j/PgDn - page down\n"
  "* k/PgUp - page up\n"
  "* gg - first page\n"
  "* q - quit\n"
  "* Number then j/k - jmp fwd/back by X slides\n";

////////// bmp load ////////// 

struct BitmapHeader {
  uint16_t type;
  uint32_t filesize;
  uint32_t resv_1;
  uint32_t offset;
  uint32_t ih_size;
  uint32_t width;
  uint32_t height;
  uint16_t planes;
  uint16_t bitcount; // 1, 4, 8, or 24
  uint32_t compression;
  uint32_t sizeimg;
  uint32_t xres, yres;
  uint32_t clrused, clrimportant;
} __attribute__((packed));

/* load bmp file into a pixel buf. 
  bmp file: each pixel 3 bytes (r/g/b) packed. 
  width, height: out, the dimension of the loaded bmp
  return pixel buf: each pixel 4 bytes 0/r/g/b 
  caller must free the buffer  
  TBD: scale the bmp based on actual fb size 
*/
void* BMP_Load(const char *filename, int *width, int *height) {
  int nread; 
  int fd = open(filename, O_RDONLY);
  if (fd<0) return 0; 

  struct BitmapHeader hdr;
  assert(sizeof(hdr) == 54);
  nread = read(fd, &hdr, sizeof hdr); 
  assert(nread == sizeof(hdr)); 

  if (hdr.bitcount != 24) return 0;
  if (hdr.compression != 0) return 0;
  int w = hdr.width;
  int h = hdr.height;
  uint32_t *pixels = malloc(w * h * sizeof(uint32_t));  // each pixel 4bytes
  assert(pixels); 
  
  int line_off = (w * 3 + 3) & ~0x3;
  for (int i = 0; i < h; i ++) {
    lseek(fd, hdr.offset + (h - 1 - i) * line_off, SEEK_SET);
    nread = read(fd, &pixels[w * i], 3*w);  assert(nread==3*w); // read a row
    // reshape a row (backward): from pixel r/g/b to 0/r/g/b 
    for (int j = w - 1; j >= 0; j --) {
      uint8_t b = *(((uint8_t*)&pixels[w * i]) + 3 * j);
      uint8_t g = *(((uint8_t*)&pixels[w * i]) + 3 * j + 1);
      uint8_t r = *(((uint8_t*)&pixels[w * i]) + 3 * j + 2);
      pixels[w * i + j] = (r << 16) | (g << 8) | b;
    }
  }

  close(fd); 
  if (width) *width = w;
  if (height) *height = h;
  return pixels;
}

//////////  main loop ////////// 

/* the virt fb size */
// #define W 400
// #define H 300
// #define W 960
// #define H 720
int W=960, H=720;
int X=0, Y=0, W0=320, H0=240;      // /dev/fb0 only

const int N = 10; // max slide num 
// slides path pattern (starts from 1, per pptx export naming convention)
const char *path = "/Slide%d.BMP"; // load from rootfs
// const char *path = "/d/Slide%d.BMP";  // load from fatfs

int cur = 1;   // cur slide num 
int config_direct=0; // 1: using /dev/fb (direct rendering); 0: using /dev/fb0 (indirect rendering)

int dispinfo[MAX_DISP_ARGS]={0};
int fb = 0;    // file desc for /dev/fb

// direct, /dev/fb
void set_bkgnd(unsigned int clr, int pitch /*in bytes*/, int h) {
    unsigned int *p = (unsigned int *) malloc(pitch*h);
    assert(p); 
    for (int i = 0; i < (pitch/PIXELSIZE)*h; i++)
      p[i] = clr; 
    
    int n = lseek(fb, 0, SEEK_SET); assert(n>=0); 

    if (write(fb, p, pitch*h) < pitch*h)
      printf("failed to write fb\n"); 
    free(p);
}

// indirect, /dev/fb0
void set_bkgnd0(unsigned int clr, int w, int h) {
    unsigned int *p = (unsigned int *) malloc(w*h*PIXELSIZE);
    assert(p); 
    for (int i = 0; i < w*h; i++)
      p[i] = clr; 
    
    int n = lseek(fb, 0, SEEK_SET); assert(n>=0); 

    if (write(fb, p, w*h*PIXELSIZE) < w*h*PIXELSIZE)
      printf("failed to write fb0\n"); 
    free(p);

    // config_fbctl0(FB0_CMD_TEST,0,0,0,0,0); // test: force compose
}

// return 0 on success; <0 on err 
int render() {
  char fname[256];
  int w, h; 
  int t0=uptime();
  sprintf(fname, (char *)path, cur);
  char *pixels = BMP_Load(fname, &w, &h); 
  if (!pixels) {printf("load from %s failed\n", fname); return -1;}

#if 0
  // Test our understanding of hw color. rewrite pixbuf with a single color (argb)
  // esp: is r and b swapped??
  // (Keee it here for reference)
  {
    char a=0xaa, r=0xff, g=0x00, b=0x00;
    unsigned int *p = (unsigned int *)pixels; 
    unsigned int clr = (a<<24) | (r<<16) | (g<<8) | b; 
    for (int i = 0; i < w*h; i++)
      p[i] = clr; 
  }
#endif

  int fb_w, fb_h; // actual size for visible area
  // crop img data per the framebuf size  
  if (config_direct) {
    int vwidth= dispinfo[VWIDTH], vheight = dispinfo[VHEIGHT];     
    fb_w = min(vwidth,w), fb_h = min(vheight,h);
  } else {
    fb_w = min(W,w), fb_h = min(H,h);
  }

  printf("%s:img size: w %d h %d; visible: w %d h %d\n", fname, w, h, fb_w, fb_h); 

  assert(fb); 
  int n, y; 
  if (config_direct) {  // write to /dev/fb by row 
    int pitch = dispinfo[PITCH];     
    // (TBD add a fast path when only 1 write is needed
    for(y=0;y<fb_h;y++) {
      n = lseek(fb, y*pitch, SEEK_SET); assert(n>=0); 
      if (write(fb, pixels+y*w*PIXELSIZE, fb_w*PIXELSIZE) < fb_w*PIXELSIZE) {
        printf("failed to write (row %d) to fb\n", y); 
        break; 
      }    
    }
  } else {  // write to /dev/fb0 by row     
    // (TBD add a fast path when only 1 write is needed
    n = lseek(fb, 0, SEEK_SET); assert(n==0); 
    for(y=0;y<fb_h;y++) {
      // still needs to seek, in case an img row < a fb0 row 
      n = lseek(fb, y*W*PIXELSIZE, SEEK_SET); assert(n>=0);
      if (write(fb, pixels+y*w*PIXELSIZE, fb_w*PIXELSIZE) < fb_w*PIXELSIZE) {
        printf("failed to write (row %d) to fb0\n", y); 
        break; 
      }
    }
    // config_fbctl0(FB0_CMD_TEST,0,0,0,0,0); // test: force compose
  }
  free(pixels); 

  printf("show %s (%d ms)\n", fname, uptime()-t0);
  return 0; 
}

void prev(int rep) {
  int oldcur=cur; 
  if (rep == 0) rep = 1;
  cur -= rep;
  if (cur <= 0) cur = 1;  
  if (render()<0) cur=oldcur; // update "cur" only if load & render ok
}

void next(int rep) {
  int oldcur=cur; 
  if (rep == 0) rep = 1;
  cur += rep;
  if (cur > N) cur = N;
  if (render()<0) cur=oldcur;
}

int main(int argc, char **argv) {
  int n;
  int rep = 0, g = 0; // rep: num of slides to skip
  int trans=100; //opaque

  if (argc>=5) {    
      int x,y,w,h;
      x=atoi(argv[1]);y=atoi(argv[2]);w=atoi(argv[3]);h=atoi(argv[4]);
      if (x>=0) X=x; 
      if (y>=0) Y=y; 
      if (w>=0) W=w; else W=W0;
      if (h>=0) H=h; else H=H0;
      if (argc==6) trans=atoi(argv[5]);
      config_direct = 0; printf("Indirect rendering. Surface %d %d\n", W,H);
  }
  else
    {config_direct = 1; printf("Direct rendering\n");}

  printf(usage); 

  int events = open(config_direct ? "/dev/events":"/dev/events0", O_RDONLY); 
  fb = open(config_direct ? "/dev/fb":"/dev/fb0", O_RDWR); 
  assert(fb>0 && events>0); 

  if (config_direct) {
    n = config_fbctl(W, H, W, H, 0, 0); assert(n==0); 
    read_dispinfo(dispinfo, &n); assert(n>0);
    printf("/proc/dispinfo: width %d height %d vwidth %d vheight %d pitch %d\n", 
      dispinfo[WIDTH], dispinfo[HEIGHT], dispinfo[VWIDTH], dispinfo[VHEIGHT], 
      dispinfo[PITCH], dispinfo[DEPTH]); 
  } else {
    n = config_fbctl0(FB0_CMD_INIT, X, Y, W, H, ZORDER_TOP,trans); assert(n==0); 
  }

  // rpi3 hw seems to clear fb with 0x0, while rpi3qemu does not 
  if (config_direct)
    set_bkgnd(0x00ffffff /*white*/, dispinfo[PITCH], dispinfo[VHEIGHT]);
  else 
    set_bkgnd0(0x00ffffff /*white*/, W, H);

  render();
  
  /* main loop. handle keydown event, switch among slides */
  int evtype;
  unsigned scancode; 
  while (1) {
    evtype = INVALID; scancode = 0;
    n = read_kb_event(events, &evtype, &scancode); assert(n==0); //blocking
    if (!(scancode && evtype)) {
      printf("warning: ev %d scancode 0x%x\n", evtype, scancode); 
      assert(0); 
    }
    
    if (evtype == KEYDOWN) {
      switch(scancode) {
        case KEY_0: rep = rep * 10 + 0; break;
        case KEY_1: rep = rep * 10 + 1; break;
        case KEY_2: rep = rep * 10 + 2; break;
        case KEY_3: rep = rep * 10 + 3; break;
        case KEY_4: rep = rep * 10 + 4; break;
        case KEY_5: rep = rep * 10 + 5; break;
        case KEY_6: rep = rep * 10 + 6; break;
        case KEY_7: rep = rep * 10 + 7; break;
        case KEY_8: rep = rep * 10 + 8; break;
        case KEY_9: rep = rep * 10 + 9; break;
        case KEY_J:
        case KEY_RIGHT:
        case KEY_PAGEDOWN: next(rep); rep = 0; g = 0; break;
        case KEY_K:
        case KEY_LEFT:
        case KEY_PAGEUP: prev(rep); rep = 0; g = 0; break;
        case KEY_G:
          g ++;
          if (g > 1) {
            prev(100000);
            rep = 0; g = 0;
          }
          break;
        case KEY_Q:
          goto cleanup;
          break; 
      }
    }
  }

cleanup: 
  if (events) close(events); 
  if (config_direct) {
    set_bkgnd(0x00000000 /*black*/, dispinfo[PITCH], dispinfo[VWIDTH]);
    if (fb) close(fb); 
  } else {
    n = config_fbctl0(FB0_CMD_FINI, 0,0,0,0,0,0); assert(n==0); 
    // release surface, which triggers the SF to compose
    if (fb) close(fb); 
  }
  printf("slider quits\n"); 
  return 0;
}
