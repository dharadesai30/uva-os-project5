#define K2_DEBUG_WARN
// #define K2_DEBUG_INFO

/*
 * Copyright (C) 2018 bzt (bztsrc@github)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

 // https://github.com/raspberrypi/firmware/wiki/Mailboxes
// ref: https://github.com/RT-Thread/rt-thread/blob/master/bsp/raspberry-pi/raspi3-64/driver/mbox.c 
// ref: https://www.valvers.com/open-software/raspberry-pi/bare-metal-programming-in-c-part-5/#part-5armc-016
// ref: uboot arch/arm/mach-bcm283x/msg.c

#include "plat.h"
#include "mmu.h"
#include "utils.h"
#include "spinlock.h"

struct spinlock mboxlock = {.locked=0, .cpu=0, .name="mbox_lock"};

/* mailbox message buffer */
// fxl: dont have to be "coherent" mem?
volatile unsigned int  __attribute__((aligned(16))) mbox[36];

#define MMIO_BASE       0x3F000000
#define VIDEOCORE_MBOX  (MMIO_BASE+0x0000B880)
#define MBOX_READ       ((volatile unsigned int*)(PA2VA(VIDEOCORE_MBOX)+0x0))
#define MBOX_POLL       ((volatile unsigned int*)(PA2VA(VIDEOCORE_MBOX)+0x10))
#define MBOX_SENDER     ((volatile unsigned int*)(PA2VA(VIDEOCORE_MBOX)+0x14))
#define MBOX_STATUS     ((volatile unsigned int*)(PA2VA(VIDEOCORE_MBOX)+0x18))
#define MBOX_CONFIG     ((volatile unsigned int*)(PA2VA(VIDEOCORE_MBOX)+0x1C))
#define MBOX_WRITE      ((volatile unsigned int*)(PA2VA(VIDEOCORE_MBOX)+0x20))
#define MBOX_RESPONSE   0x80000000
#define MBOX_FULL       0x80000000
#define MBOX_EMPTY      0x40000000

/**
 * Make a mailbox call. Use the "mbox" buffer for both request and response.
 * response overwrites request
 * Spin wait for mailbox hw.  
 * Returns 0 on failure, non-zero on success
 * 
 * caller must hold mboxlock
 */
int mbox_call(unsigned char ch)
{
    // the buf addr (pa) w/ ch (chan id) in LSB 
    // unsigned int r = (((unsigned int)((unsigned long)&mbox)&~0xF) | (ch&0xF));
    unsigned int r = (unsigned int)(VA2PA(&mbox)&~0xF) | (ch&0xF);
    r = BUS_ADDRESS(r); 
    /* wait until we can write to the mailbox */
    do{asm volatile("nop");}while(*MBOX_STATUS & MBOX_FULL);
    __asm__ volatile ("dmb sy" ::: "memory");    // mem barrier, ensuring msg in mem
    __asm_flush_dcache_range((void *)mbox, (char *)mbox + sizeof(mbox)); 

    /* write the address of our message to the mailbox with channel identifier */
    *MBOX_WRITE = r; 
    /* now wait for the response */
    while(1) {
        /* is there a response? */
        do{asm volatile("nop");}while(*MBOX_STATUS & MBOX_EMPTY);
        /* is it a response to our message? */
        if(r == *MBOX_READ) {
            V("r is 0x%x", r); 
            __asm_invalidate_dcache_range((void *)mbox, (char *)mbox + sizeof(mbox)); 
            /* is it a valid successful response? (strange it's benign) */
            if (mbox[1]!=MBOX_RESPONSE) I("mbox[1] is %08x", mbox[1]);            
            return mbox[1]==MBOX_RESPONSE;
        } else {
            W("got an irrelvant msg. bug?"); 
        }
    }
    return 0;
}

///////////////////////////////////////////////////
// property interfaces via mbox
#define MBOX_REQUEST    0
#define CODE_RESPONSE_SUCCESS	0x80000000
#define CODE_RESPONSE_FAILURE	0x80000001
	
/* channels */
#define MBOX_CH_POWER   0
#define MBOX_CH_FB      1
#define MBOX_CH_VUART   2
#define MBOX_CH_VCHIQ   3
#define MBOX_CH_LEDS    4
#define MBOX_CH_BTNS    5
#define MBOX_CH_TOUCH   6
#define MBOX_CH_COUNT   7
#define MBOX_CH_PROP    8

/* tags */ // redundant, TO DEL
// #define MBOX_TAG_SETPOWER       0x28001
// #define MBOX_TAG_SETCLKRATE     0x38002
#define MBOX_TAG_LAST           0

// in a successful resp, b31 is set; b30-0 is "value length in bytes"
#define VALUE_LENGTH_RESPONSE	(1 << 31)

#define PROPTAG_GET_FIRMWARE_REVISION	0x00000001
#define PROPTAG_GET_BOARD_MODEL		0x00010001
#define PROPTAG_GET_BOARD_REVISION	0x00010002
#define PROPTAG_GET_MAC_ADDRESS		0x00010003
#define PROPTAG_GET_BOARD_SERIAL	0x00010004
#define PROPTAG_GET_ARM_MEMORY		0x00010005
#define PROPTAG_GET_VC_MEMORY		0x00010006
#define PROPTAG_SET_POWER_STATE		0x00028001
    #define DEVICE_ID_SD_CARD	0   // fxl: SDHCI, not SDHOST
	#define DEVICE_ID_USB_HCD	3
    #define POWER_STATE_OFF		(0 << 0)
	#define POWER_STATE_ON		(1 << 0)
	#define POWER_STATE_WAIT	(1 << 1)
	#define POWER_STATE_NO_DEVICE	(1 << 1)	// in response
#define PROPTAG_GET_CLOCK_RATE		0x00030002
#define PROPTAG_GET_TEMPERATURE		0x00030006
#define PROPTAG_GET_EDID_BLOCK		0x00030020
#define PROPTAG_GET_DISPLAY_DIMENSIONS	0x00040003
#define PROPTAG_GET_COMMAND_LINE	0x00050001
// undocumented. cf https://github.com/raspberrypi/firmware/issues/719
// also sound/sample/env.c EnableVCHIQ
#define PROPTAG_VCHIQ_INIT  	    0x48010 

///////////////////////////////////////////////////
//  a simple framebuffer 
//
#include "fb.h"

// default (upon boot): 1024x768, phys WH = virt WH, offset (0,0)
// said to support up to 1920x1080
struct fb_struct the_fb = {
    .fb = 0,
#ifdef PLAT_RPI3QEMU
    // these are just initial fb sizes; app will ask for diff
    // sizes based on their logic. so we keep them small for qemu
    // to avoid a big blank screen upon boot
    .width = 320,
    .height = 240, 
    .vwidth = 320, 
    .vheight = 240,
#else // rpi3 hw
    // =0 same as the detected scr dim, see below (there's caveat)
    .width  = 0, // 1024,  
    .height = 0, // 768, 
    .vwidth = 0, // 1024, 
    .vheight = 0, // 768,
#endif
    .scr_width = 0,
    .scr_height = 0, 
    .depth = 32, 
    .isrgb = 0,     // see below 
    .offsetx = 0,
    .offsety = 0,
    .size = 0, 
}; 
// isrgb: whatever the doc says, 0 seems rgb; 1 seems bgr
// rpi3 hw will return "0" even if we asks for "1"
// qemu will do whatever we ask ("0" or "1"), but if "1", color channels are bgr

/* detect phys display optimal x/y, if unconfigured
    caller must hold mboxlock
    return: 0 on success 

    FL's 720p monitor: 1360 768
    qemu 640 480 (initial? subject to reconfig for larger fb)

    Waveshare game HAT: actual 480 320, but detected to be 1184x624? likely a hw bug
    https://www.waveshare.com/wiki/Game_HAT
*/
int fb_detect_scr_dim(uint *w, uint *h) {
    mbox[0] = 8*4;     // size of the whole buf that follows
    mbox[1] = MBOX_REQUEST; // cpu->gpu request
        mbox[2] = 0x40003;     // rls framebuffer
        mbox[3] = 8;           // total buf size
        mbox[4] = 0;           // req para size
        mbox[5] = 0;           // resp: width
        mbox[6] = 0;           // resp: height
    mbox[7] = MBOX_TAG_LAST;

    if(!mbox_call(MBOX_CH_PROP)) {
        E("failed to get screen dim");
        return -1;
    } 

    *w=mbox[5];*h=mbox[6]; I("detected screen dim %d %d", *w, *h);

    if (*w == 1184 || *h == 624) {
        W("detected screen 1184x624. assume a Waveshare HAT. force 480 320");
        *w = 480; *h = 320;
    }
    return 0; 
}

// caller must hold mboxlock
// 0 on success
int fb_set_voffsets(int offsetx, int offsety) {

    mbox[0] = 8*4;
    mbox[1] = MBOX_REQUEST;

    //set virt offset
    mbox[2] = 0x48009; 
    mbox[3] = 8;
    mbox[4] = 8;
    mbox[5] =  offsetx;           //FrameBufferInfo.x_offset
    mbox[6] =  offsety;           //FrameBufferInfo.y.offset    

    mbox[7] = MBOX_TAG_LAST;

    if(!mbox_call(MBOX_CH_PROP)) {
        E("failed to set virt offsets, requested x=%d y=%d", offsetx, offsety);
        return -1;
    }     
     if (mbox[5] != offsetx || mbox[6] != offsety) {
        E("failed set: offsetx %u offsety %u res: offsetx %u offsety %u", 
            offsetx, offsety, mbox[5], mbox[6]);
        return -1;     
     }
     V("set OK: offsetx %u offsety %u res: offsetx %u offsety %u", 
            offsetx, offsety, mbox[5], mbox[6]);
     return 0; 
}


/**
 * For mailbox property interface,
 * cf: https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
 * 
 * more undocumented cf: 
 * https://github.com/raspberrypi/firmware/issues/719
 * 
 * below uses mbox "property channel". another way is to use the "fb" channel 
 * directly. cf: https://github.com/rsta2/circle/blob/master/lib/bcmframebuffer.cpp
 * 
 * code ex: 
 * https://github.com/RT-Thread/rt-thread/blob/master/bsp/raspberry-pi/raspi3-64/driver/mbox.c
 * 
 * return 0 if succeeds. 
 */
static int do_fb_init(struct fb_struct *fbs)
{    
    if (!fbs) return -1; 

    acquire(&mboxlock); 

#ifdef PLAT_RPI3
    // if (v)width/(v)height is 0, set them = the scr size
    if (fb_detect_scr_dim(&fbs->scr_width,&fbs->scr_height)==0) {
        fbs->vwidth = fbs->vwidth ? fbs->vwidth:fbs->scr_width;
        fbs->vheight = fbs->vheight ? fbs->vheight:fbs->scr_height;
        fbs->width  = fbs->width ? fbs->width:fbs->scr_width;
        fbs->height  = fbs->height ? fbs->height:fbs->scr_height;        
    }
#endif

    mbox[0] = 35*4;     // size of the whole buf that follows
    mbox[1] = MBOX_REQUEST; // cpu->gpu request

    // a sequence of tags below 

    mbox[2] = 0x48003;  //set phy width & height
    mbox[3] = 8;        // total buf size of this tag
    mbox[4] = 8;        // req val size (needed?), to be overwritten as resp val size
    mbox[5] = fbs->width;           //(val) FrameBufferInfo.width
    mbox[6] = fbs->height;          //(val) FrameBufferInfo.height

    mbox[7] = 0x48004;  //set virt width & height
    mbox[8] = 8;
    mbox[9] = 8;
    mbox[10] = fbs->vwidth;        //FrameBufferInfo.virtual_width
    mbox[11] = fbs->vheight;         //FrameBufferInfo.virtual_height

    mbox[12] = 0x48009; //set virt offset
    mbox[13] = 8;
    mbox[14] = 8;
    mbox[15] = fbs->offsetx;           
    mbox[16] = fbs->offsety;           

    mbox[17] = 0x48005; //set depth
    mbox[18] = 4;
    mbox[19] = 4;
    mbox[20] = fbs->depth;       

    mbox[21] = 0x48006;     //set pixel order
    mbox[22] = 4;
    mbox[23] = 4;
    mbox[24] = fbs->isrgb;           //RGB, not BGR preferably

    mbox[25] = 0x40001;     //get framebuffer, gets alignment on request
    mbox[26] = 8;
    mbox[27] = 8;           // fxl: should be 4?? (req para size)
    mbox[28] = 4096;        //req: alignment; resp: FrameBufferInfo.pointer
    mbox[29] = 0;           //resp: FrameBufferInfo.size

    mbox[30] = 0x40008;     //get pitch
    mbox[31] = 4;
    mbox[32] = 4;
    mbox[33] = 0;           //FrameBufferInfo.pitch

    mbox[34] = MBOX_TAG_LAST;   // the end of tag seq

    // make call, then check some response vals that may fail
    if(mbox_call(MBOX_CH_PROP) 
        && mbox[20]==fbs->depth /*depth*/ 
        && mbox[28]!=0 /*framebuf*/) {
        // extract framebuf info from resp...
        mbox[28]&=0x3FFFFFFF;  
        fbs->fb = PA2VA((unsigned long)mbox[28]);   // save framebuf ptr
        fbs->width=mbox[5];
        fbs->height=mbox[6];
        fbs->vwidth=mbox[10];
        fbs->vheight=mbox[11];        
        fbs->depth=mbox[20]; 
        fbs->isrgb=mbox[24];         // channel order        
        fbs->pitch=mbox[33];
        if(fbs->pitch * fbs->vheight > mbox[29])  // possible that pitch*vheight < actual allocation
            {W("pitch %d x vheight %d!= mbox[29] %u", fbs->pitch, fbs->vheight, mbox[29]);BUG();}
        fbs->size = PGROUNDUP(fbs->pitch * fbs->vheight);  // roundup b/c we'll reserve pages for it
        I("OK. fb pa: 0x%08x w %u h %u vw %u vh %u pitch %u isrgb %u", 
            mbox[28], fbs->width, fbs->height, fbs->vwidth, fbs->vheight, 
                fbs->pitch, fbs->isrgb); 
    } else {
        E("Unable to set scr res to %d x %d\n", fbs->width, fbs->height);
        return -2; 
    }
    release(&mboxlock); 
    if (reserve_phys_region(mbox[28], fbs->size)) {
        E("failed to reserve fb mem. pa 0x%x size 0x%x already in use.",
            mbox[28], fbs->size); BUG(); 
        return -1; 
    } else 
        return 0; 
}

void fb_showpicture();
int fb_init(void) {
    static int once = 1; 
    int ret = do_fb_init(&the_fb); 
    if (ret==0 && once) {
        fb_showpicture(); once=0; 
        kconsole_init(); 
#if KCONSOLE_PRINTF     
        // kconsole will be activted initially, deactivated when fb_init() gets
        // called for a second time        
        kconsole_printf=1;
#endif
    } else 
        kconsole_printf=0;
    return ret; 
}

// return 0 on success (display will go black)
int fb_fini(void) {
    int ret = 0; 

    acquire(&mboxlock); 
    if (!the_fb.fb || !the_fb.size) {
        ret = -1; 
        goto out; 
    }

#ifdef PLAT_RPI3QEMU    // avoid artifacts: qemu does not clear old fb
    memset(the_fb.fb, 0, the_fb.size);     
#endif

    mbox[0] = 6*4;     // size of the whole buf that follows
    mbox[1] = MBOX_REQUEST; // cpu->gpu request

    mbox[2] = 0x48001;     // rls framebuffer
    mbox[3] = 0;           // total buf size
    mbox[4] = 0;           // req para size
        
    mbox[5] = MBOX_TAG_LAST;

    if(!mbox_call(MBOX_CH_PROP))
        I("failed to rls fb with GPU."); 
        // response code always 0x80000001 (failure). couldn't figure out why

    if (free_phys_region(VA2PA(the_fb.fb), the_fb.size)) {
        E("failed to free fb memory. bug?"); 
        ret = -2; 
    }
    the_fb.fb = 0; 
out:
    release(&mboxlock);          
    return ret; 
}

// Display a string using fixed size PSF update x,y screen coordinates
// x/y IN|OUT: the postion before/after the screen output
// void fb_print(struct fb_struct *fbs, int *x, int *y, char *s)
void fb_print(int *x, int *y, char *s)
{
    unsigned pitch = the_fb.pitch; 
    unsigned char *fb = the_fb.fb; 

    // get our font
    psf_t *font = (psf_t*)&_binary_font_psf_start;
    // draw next character if it's not zero
    while(*s) {
        // get the offset of the glyph. Need to adjust this to support unicode table
        unsigned char *glyph = (unsigned char*)&_binary_font_psf_start +
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
                // display one row
                line=offs;
                mask=1<<(font->width-1);
                for(i=0;i<font->width;i++){
                    // if bit set, we use white color, otherwise black
                    *((unsigned int*)(fb + line))=((int)*glyph) & mask?0xFFFFFF:0;
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
}

#if 0
/**
 * Display a string using proportional SSFN
 */
void lfb_proprint(int x, int y, char *s)
{
    // get our font
    sfn_t *font = (sfn_t*)&_binary_font_sfn_start;
    unsigned char *ptr, *chr, *frg;
    unsigned int c;
    unsigned long o, p;
    int i, j, k, l, m, n;

    while(*s) {
        // UTF-8 to UNICODE code point
        if((*s & 128) != 0) {
            if(!(*s & 32)) { c = ((*s & 0x1F)<<6)|(*(s+1) & 0x3F); s += 1; } else
            if(!(*s & 16)) { c = ((*s & 0xF)<<12)|((*(s+1) & 0x3F)<<6)|(*(s+2) & 0x3F); s += 2; } else
            if(!(*s & 8)) { c = ((*s & 0x7)<<18)|((*(s+1) & 0x3F)<<12)|((*(s+2) & 0x3F)<<6)|(*(s+3) & 0x3F); s += 3; }
            else c = 0;
        } else c = *s;
        s++;
        // handle carrige return
        if(c == '\r') {
            x = 0; continue;
        } else
        // new line
        if(c == '\n') {
            x = 0; y += font->height; continue;
        }
        // find glyph, look up "c" in Character Table
        for(ptr = (unsigned char*)font + font->characters_offs, chr = 0, i = 0; i < 0x110000; i++) {
            if(ptr[0] == 0xFF) { i += 65535; ptr++; }
            else if((ptr[0] & 0xC0) == 0xC0) { j = (((ptr[0] & 0x3F) << 8) | ptr[1]); i += j; ptr += 2; }
            else if((ptr[0] & 0xC0) == 0x80) { j = (ptr[0] & 0x3F); i += j; ptr++; }
            else { if((unsigned int)i == c) { chr = ptr; break; } ptr += 6 + ptr[1] * (ptr[0] & 0x40 ? 6 : 5); }
        }
        if(!chr) continue;
        // uncompress and display fragments
        ptr = chr + 6; o = (unsigned long)lfb + y * pitch + x * 4;
        for(i = n = 0; i < chr[1]; i++, ptr += chr[0] & 0x40 ? 6 : 5) {
            if(ptr[0] == 255 && ptr[1] == 255) continue;
            frg = (unsigned char*)font + (chr[0] & 0x40 ? ((ptr[5] << 24) | (ptr[4] << 16) | (ptr[3] << 8) | ptr[2]) :
                ((ptr[4] << 16) | (ptr[3] << 8) | ptr[2]));
            if((frg[0] & 0xE0) != 0x80) continue;
            o += (int)(ptr[1] - n) * pitch; n = ptr[1];
            k = ((frg[0] & 0x1F) + 1) << 3; j = frg[1] + 1; frg += 2;
            for(m = 1; j; j--, n++, o += pitch)
                for(p = o, l = 0; l < k; l++, p += 4, m <<= 1) {
                    if(m > 0x80) { frg++; m = 1; }
                    if(*frg & m) *((unsigned int*)p) = 0xFFFFFF;
                }
        }
        // add advances
        x += chr[4]+1; y += chr[5];
    }
}
#endif

#include "uvalogo.h"
#define IMG_DATA header_data      
#define IMG_HEIGHT height
#define IMG_WIDTH width

// #include "rev_uva_logo_color3-resized.h"
// #define IMG_DATA img_data      
// #define IMG_HEIGHT img_height
// #define IMG_WIDTH img_width
void fb_showpicture()
{
    int x,y;
    unsigned char *ptr=the_fb.fb;
    char *data=IMG_DATA, pixel[4];
    // fill framebuf. crop img data per the framebuf size
    unsigned int img_fb_height = the_fb.vheight < IMG_HEIGHT ? the_fb.vheight : IMG_HEIGHT; 
    unsigned int img_fb_width = the_fb.vwidth < IMG_WIDTH ? the_fb.vwidth : IMG_WIDTH; 

    // copy the image pixels to the start (top) of framebuf    
    //ptr += (vheight-img_fb_height)/2*pitch + (vwidth-img_fb_width)*2;  
    ptr += (the_fb.vwidth-img_fb_width)/2*PIXELSIZE;  // top center
    ptr += (the_fb.vheight-img_fb_height)/2*the_fb.pitch; 
    
    for(y=0;y<img_fb_height;y++) {
        for(x=0;x<img_fb_width;x++) {
            HEADER_PIXEL(data, pixel);
            // the image is in RGB. So if we have an RGB framebuffer, we can copy the pixels
            // directly, but for BGR we must swap R (pixel[0]) and B (pixel[2]) channels.
            *((unsigned int*)ptr)=the_fb.isrgb ? *((unsigned int *)&pixel) : (unsigned int)(pixel[0]<<16 | pixel[1]<<8 | pixel[2]);
            // *((unsigned int*)ptr)=(!the_fb.isrgb) ? *((unsigned int *)&pixel) : (unsigned int)(pixel[0]<<16 | pixel[1]<<8 | pixel[2]);
            ptr+=4;
        }
        ptr+=the_fb.pitch-img_fb_width*4;
    }

    x = (the_fb.vwidth-img_fb_width)/2;
    y = the_fb.vheight/2 + img_fb_height/2;
    fb_print(&x, &y, "UVA OS");
    char res[16]; 
    sprintf(res, " %dx%d", the_fb.width, the_fb.height);
    fb_print(&x, &y, res);
    __asm_flush_dcache_range(the_fb.fb, the_fb.fb + the_fb.size); 
}

/////////////////////////////
// other property tag operations. 

// 0 on success, <0 on errors
int set_powerstate(unsigned deviceid, int on) {
    int ret = 0; 
    unsigned power = on ? 
        (POWER_STATE_ON | POWER_STATE_WAIT) : (POWER_STATE_OFF | POWER_STATE_WAIT);

    acquire(&mboxlock); 

    mbox[0] = 8*4;
    mbox[1] = MBOX_REQUEST;

    mbox[2] = PROPTAG_SET_POWER_STATE; 
    mbox[3] = 8; 
    mbox[4] = 8; 
    mbox[5] = deviceid; 
    mbox[6] = power;

    mbox[7] = MBOX_TAG_LAST;   // the end of tag seq

    // make call, then check some response vals that may fail
    if(!mbox_call(MBOX_CH_PROP))
        {ret = -1; goto fail;}
    // routine checks 
    if (mbox[1] != CODE_RESPONSE_SUCCESS)
        {ret = -2; goto fail;}
    if (!(mbox[4] & VALUE_LENGTH_RESPONSE)) 	// resp bit not set
        {ret = -3; goto fail;}
    if ((mbox[4] &= ~VALUE_LENGTH_RESPONSE) == 0)  // resp val length is 0 
        {ret = -4; goto fail;}
    // resp checks
    power = mbox[6];
    if (power & POWER_STATE_NO_DEVICE)
        {ret = -5; goto fail;}
    if (on && !(power & POWER_STATE_ON))
        {ret = -6; goto fail;}
    if (!on && (power & POWER_STATE_ON))
        {ret = -6; goto fail;}

    release(&mboxlock); 
    return 0; 
fail:
    release(&mboxlock); 
    W("%s failed. %d", __func__, ret); 
    return ret; 
}

// 0 on success, <0 on errors
// resp length: 6
int get_mac_addr(unsigned char buf[MAC_SIZE]) {
    int ret = 0; 

    acquire(&mboxlock); 
    mbox[0] = 8*4;
    mbox[1] = MBOX_REQUEST;

    mbox[2] = PROPTAG_GET_MAC_ADDRESS; 
    mbox[3] = 8;                // total buf size of this tag
    mbox[4] = 0;                // req val size, to be overwritten as resp val size
    mbox[5] = 0;                // for returned mac addr
    mbox[6] = 0;                // for returned mac addr. 2 bytes padding

    mbox[7] = MBOX_TAG_LAST;   // the end of tag seq    

    // make call, then check some response vals that may fail
    if(!mbox_call(MBOX_CH_PROP))
        {ret = -1; W("mbox[1] 0x%x  mbox[5] 0x%x", mbox[1], mbox[5]); goto fail;}
    // routine checks 
    if (mbox[1] != CODE_RESPONSE_SUCCESS)
        {ret = -2; goto fail;}
    if (!(mbox[4] & VALUE_LENGTH_RESPONSE)) 	// resp bit not set
        {ret = -3; goto fail;}
    if ((mbox[4] &= ~VALUE_LENGTH_RESPONSE) == 0)  // resp val length is 0 
        {ret = -4; goto fail;}
    // resp checks
    if (mbox[4] != MAC_SIZE)      
        {ret = -5; goto fail;}
    memmove(buf, (void *)&mbox[5], MAC_SIZE); 

    release(&mboxlock); 
    return 0; 
fail:
    release(&mboxlock); 
    W("%s failed. %d", __func__, ret); 
    return ret; 
}

// return 0 on success
// resp length: 8
int get_board_serial(unsigned long *s) {
    if (!s) return -1;

    acquire(&mboxlock); 

    // get the board's unique serial number with a mailbox call
    mbox[0] = 8*4;                  // length of the message
    mbox[1] = MBOX_REQUEST;         // this is a request message
    
    mbox[2] = PROPTAG_GET_BOARD_SERIAL;   // get serial number command
    mbox[3] = 8;                    // buffer size
    mbox[4] = 8;
    mbox[5] = 0;                    // clear output buffer
    mbox[6] = 0;  // unused

    mbox[7] = MBOX_TAG_LAST;

    // send the message to the GPU and receive answer
    if (mbox_call(MBOX_CH_PROP)) {      
        release(&mboxlock);   
        memmove(s, (void*)&mbox[5], 8); 
        return 0; 
    } else {
        release(&mboxlock); 
        return -2; 
    }
}

// "p": argument to the cmd
// return: 0 on ok 
int enable_vchiq(unsigned buf /*arg*/, unsigned *resp) {

    acquire(&mboxlock); 

    mbox[0] = 8*4;                  // length of the message
    mbox[1] = MBOX_REQUEST;         // this is a request message
    
    mbox[2] = PROPTAG_VCHIQ_INIT;   
    mbox[3] = 8;                    // total buffer size
    mbox[4] = 4;                    // req/resp size
    mbox[5] = buf;                   
    mbox[6] = 0;                //unused. ok       

    mbox[7] = MBOX_TAG_LAST;

    // send the message to the GPU and receive answer
    if (mbox_call(MBOX_CH_PROP)) {      
        release(&mboxlock);   
        *resp = mbox[5]; 
        return 0; 
    } else {
        release(&mboxlock); 
        return -2; 
    }    
}

#include "fcntl.h"
int procfs_parse_fbctl(int args[PROCFS_MAX_ARGS]) {    
    if (args[0]>0 || args[1]>0 || args[2]>0 || args[3]>0) {
        fb_fini(); 
        acquire(&mboxlock); 
        the_fb.width = args[0];
        the_fb.height = args[1];
        the_fb.vwidth = args[2];
        the_fb.vheight = args[3];
        release(&mboxlock);      
        fb_init(); 
    }
    
    acquire(&mboxlock); 
    if (fb_set_voffsets(args[4], args[5]) <0)
        E("failed to set voffsets");
    release(&mboxlock);      

    return 0; 
}
