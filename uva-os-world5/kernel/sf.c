// #define K2_DEBUG_VERBOSE
// #define K2_DEBUG_INFO
#define K2_DEBUG_WARN

// #define PROFILE_SF 1 // profile timing of composition

/* 
    In-kernel surface flinger (SF). 
   each task configures desired surface size/loc via /proc/sfctl, 
   and writes their pixels to /dev/sf; 
   the SF maintains per-task buffers and composite them to the actual hw
   framebuffer (fb). 

   each pid: can only have at most 1 surface. 

   This helps demonstrate the idea of multitasking OS
*/

#include "plat.h"
#include "mmu.h"
#include "utils.h"
#include "spinlock.h"
#include "fb.h"
#include "kb.h"
#include "list.h"
#include "fcntl.h"
#include "sched.h"
#include "file.h"

struct region {
    int x,y,w,h;
}; 

int do_regions_intersect(struct region *r1, struct region *r2) {
    int r1_x2 = r1->x + r1->w;
    int r1_y2 = r1->y + r1->h;
    int r2_x2 = r2->x + r2->w;
    int r2_y2 = r2->y + r2->h;

    if (r1->x >= r2_x2 || r2->x >= r1_x2)
        return 0;
    if (r1->y >= r2_y2 || r2->y >= r1_y2)
        return 0;
    return 1;
}

struct region regions_union(struct region *r1, struct region *r2) {
    int r1_x2 = r1->x + r1->w;
    int r1_y2 = r1->y + r1->h;
    int r2_x2 = r2->x + r2->w;
    int r2_y2 = r2->y + r2->h;
    struct region r; 
    r.x = MIN(r1->x, r2->x); 
    r.y = MIN(r1->y, r2->y); 
    r.w = MAX(r1_x2, r2_x2) - r.x; 
    r.h = MAX(r1_y2, r2_y2) - r.y;
    return r; 
}

struct sf_struct {
    unsigned char *buf; // kernel va
    struct region r; // with regard to (0,0) of the hw fb
    int transparency;  // 0-100
    int dirty;  // redraw this surface? 
    int pid; // owner 
    int floating; // always on top?
    struct kb_struct kb; // kb events dispatched to this surface
    slist_t list;
    slist_t list1;  // to link sf on a tmplist, cf sf_composite()
}; 

#ifdef PLAT_RPI3
static int VW=0, VH=0; // 0 means same as detected scr dim 
#else
static int VW=1360, VH=768; 
#endif

#define BK_COLOR  0x00222222   

static struct spinlock sflock = {.locked=0, .cpu=0, .name="sflock"};
static slist_t sflist = SLIST_OBJECT_INIT(sflist);  
static struct region bk_dirty;

extern int procfs_parse_fbctl(int args[PROCFS_MAX_ARGS]); // mbox.c
extern int sys_getpid(void);  // sys.c

static int reset_fb() {
    V("%s +++++ ", __func__);
    int args[PROCFS_MAX_ARGS] = {
        VW, VH,
        VW, VH,
        0,0
    }; 
    int ret = procfs_parse_fbctl(args);

    acquire(&mboxlock);
    VW=the_fb.vwidth; VH=the_fb.vheight;
    release(&mboxlock);

    bk_dirty.x=bk_dirty.y=0;
    bk_dirty.w=VW; bk_dirty.h=VH; 

    V("%s done ", __func__);
    return ret; 
}

static struct sf_struct *find_sf(int pid) {
    slist_t *node = 0;
    struct sf_struct *sf = 0; 

    slist_for_each(node, &sflist) {
        sf = slist_entry(node, struct sf_struct, list); BUG_ON(!sf);
        if (sf->pid == pid)
            return sf; 
    }
    return 0; 
}

static void dirty_all_sf(void) {
    slist_t *node = 0;
    struct sf_struct *sf; 
    slist_for_each(node, &sflist) {
        sf = slist_entry(node, struct sf_struct, list); BUG_ON(!sf);
        sf->dirty=1; 
    }
}

extern struct kb_struct the_kb; // kb.c
int kb_task_quit = 0; // protected by the_kb.lock
int kb_task_id = -1;
static void kb_task(int arg); 

static int sf_create(int pid, int x, int y, int w, int h, int zorder, int trans) {
    struct sf_struct *s = malloc(sizeof(struct sf_struct));     
    int ret=0;

    if (!s) {E("failed");return -1;}
    s->buf = malloc(w*h*PIXELSIZE); if (!s->buf) {free(s);E("failed");return -2;}

    s->r.x=x; s->r.y=y; s->r.w=w; s->r.h=h; s->pid=pid; 
    s->dirty=1; s->transparency=trans; s->floating=0; 
    s->kb.r=s->kb.w=0;

    acquire(&sflock);
    if (find_sf(pid)) { E("pid already exists"); ret = -3; goto fail; }

    if (zorder == ZORDER_BOTTOM) {
        slist_insert(&sflist, &s->list);         
    } else if (zorder == ZORDER_TOP) {
        slist_append(&sflist, &s->list);
    } else if (zorder == ZORDER_FLOATING) {
        s->floating = 1; slist_append(&sflist, &s->list);
    } else {E("unknown zorder"); ret = -4; goto fail;}

    dirty_all_sf();

    if (slist_len(&sflist) == 1) {
        reset_fb();
        int res = copy_process(PF_KTHREAD, (unsigned long)&kb_task, 0, "[kb]"); 
        if (res<0) {ret = -5; goto fail; }
        kb_task_id = res;
    }

    wakeup(&sflist);
    release(&sflock);
    I("cr ok. pid %d", pid); 
    return 0; 

fail:
    free(s->buf); free(s); 
    release(&sflock); 
    return ret; 
}

int sf_free(int pid)  {
    struct sf_struct *sf = 0;

    acquire(&sflock);

    sf = find_sf(pid); 
    if (!sf) {
        release(&sflock);
        return -1;     
    }

    slist_remove(&sflist, &sf->list);            
    if (slist_len(&sflist) == 0) {
        I("freed");fb_fini();
        if (sf->buf) free(sf->buf);
        free(sf); 
        
        release(&sflock);

        acquire(&the_kb.lock);
        kb_task_quit = 1; 
        wakeup(&the_kb.r);
        release(&the_kb.lock);
        I("wait for kb task to finish...");
        int wpid=wait(0); BUG_ON(wpid<0);
    }  else {
        bk_dirty = regions_union(&bk_dirty, &sf->r); 
        if (sf->buf) free(sf->buf); 
        free(sf); 
        wakeup(&sflist);
        release(&sflock);
    }
    return 0;
}

int sf_size(int pid) {
    int ret = -1; 
    struct sf_struct *sf = 0;

    acquire(&sflock);
    sf = find_sf(pid); 
    if (!sf) 
        goto out; 
    ret = sf->r.w * sf->r.h * PIXELSIZE;
out:
    release(&sflock);
    return ret; 
}

static int sf_config(int pid, int x, int y, int w, int h, int zorder) {
    struct sf_struct *sf = 0;

    acquire(&sflock);
    sf = find_sf(pid); 
    if (sf) {
        struct region old = sf->r;
        if (sf->r.w!=w || sf->r.h!=h) {
            BUG(); 
        }
        sf->r.x=x; sf->r.y=y; sf->r.w=w; sf->r.h=h;
        bk_dirty = regions_union(&bk_dirty, &old);
        sf->dirty = 1;
        if (zorder != ZORDER_UNCHANGED) { 
            slist_remove(&sflist, &sf->list); 
            if (zorder == ZORDER_TOP)
                slist_append(&sflist, &sf->list); 
            else if (zorder == ZORDER_BOTTOM) 
                slist_insert(&sflist, &sf->list);
            else if (zorder == ZORDER_FLOATING) {
                sf->floating = 1;
                slist_append(&sflist, &sf->list);
            }
            else BUG();
        }
    }
    wakeup(&sflist);
    release(&sflock);
    if (!sf) return -1;
    return 0; 
}

// move the bottom surface to the top, which also moves focus to it 
// call must NOT hold sflock
//quest: desktop
static int sf_cycle_focus(void) {
    struct sf_struct *bot = 0;
    int ret; 
    
    V("%s", __func__); 
    acquire(&sflock);
    if (slist_len(&sflist) <= 1) {ret=0; goto out;}

    bot = slist_first_entry(&sflist, struct sf_struct, list); 
    slist_remove(&sflist, &bot->list);
    slist_append(&sflist, &bot->list);

    dirty_all_sf();
    bk_dirty.x = 0;
    bk_dirty.y = 0;
    bk_dirty.w = VW;
    bk_dirty.h = VH;

    wakeup(&sflist); 
    ret=0; 
out: 
    release(&sflock);
    return ret; 
}

#define STEPSIZE 5
//quest: desktop, optional feature
static int sf_move(int dir) {
    int ret=0; 
    struct sf_struct *top = 0;
    
    V("%s", __func__); 

    acquire(&sflock);
    if (slist_len(&sflist)==0) {ret=-1; goto out;}

    top = slist_tail_entry(&sflist, struct sf_struct, list); 
    struct region *r = &top->r, r0 = top->r; 
    switch(dir) {        
        case 0: // R
            r->x = MIN(VW - r->w, (int)(r->x) + STEPSIZE);
            bk_dirty.x = r0.x;
            bk_dirty.w = ABS(r->x - r0.x);
            bk_dirty.y = r->y;
            bk_dirty.h = r->h; 
            break; 
        case 1: // L
            r->x = MAX(0, (int)(r->x) - STEPSIZE);
            bk_dirty.x = r->x + r->w;
            bk_dirty.w = ABS(r->x - r0.x);
            bk_dirty.y = r->y;
            bk_dirty.h = r->h;
            break;
        case 2: // Dn
            r->y = MIN(VH - r->h, (int)(r->y) + STEPSIZE); 
            bk_dirty.x = r->x;
            bk_dirty.w = r->w; 
            bk_dirty.y = r0.y;
            bk_dirty.h = ABS(r->y - r0.y);
            break; 
        case 3: // Up
            r->y = MAX(0, (int)(r->y) - STEPSIZE);
            bk_dirty.x = r->x;
            bk_dirty.w = r->w;
            bk_dirty.y = r->y + r->h;
            bk_dirty.h = ABS(r->y - r0.y);
            break;
        default: 
            ret = -1; 
            break; 
    }
    if (ret!=-1) {
        top->dirty = 1; 
        wakeup(&sflist);
    }
out: 
    release(&sflock);
    return ret; 
}

static void sf_dump(void) {
    slist_t *node = 0; 
    struct sf_struct *sf; 

    acquire(&sflock);
    printf("pid x y w h\n"); 
    slist_for_each(node, &sflist) {
        sf = slist_entry(node, struct sf_struct, list);        
        printf("%d %d %d %d %d\n", sf->pid, sf->r.x, sf->r.y, sf->r.w, sf->r.h);
    }
    release(&sflock);
}

void test_sf() {
    int ret; 
    
    sf_dump();

    ret=sf_create(1, 0,0, 320,240, ZORDER_TOP,100); BUG_ON(ret!=0);
    ret=sf_create(2, 0,0, 320,240, ZORDER_TOP,100); BUG_ON(ret!=0);
    ret=sf_create(3, 0,0, 320,240, ZORDER_TOP,100); BUG_ON(ret!=0);
    ret=sf_create(3, 0,0, 320,240, ZORDER_TOP,100); BUG_ON(ret==0);

    sf_dump();

    ret=sf_config(2,100,100,320,240,ZORDER_BOTTOM); BUG_ON(ret!=0);
    ret=sf_config(10,0,0,640,480,ZORDER_BOTTOM); BUG_ON(ret==0);
    sf_dump();

    ret=sf_free(2); BUG_ON(ret!=0);
    ret=sf_free(2); BUG_ON(ret==0);

    sf_dump();

    ret=sf_free(1); BUG_ON(ret!=0);
    ret=sf_free(3); BUG_ON(ret!=0);

    sf_dump();
}

/**********************
    the surface flinger
**********************/

#define B_THICKNESS     3
#define B_COLOR  0x00ff0000
static int draw_boundary(int x, int y, int w, int h, unsigned int clr) {
    unsigned char *t0, *b0;
    unsigned int *t, *b;
    BUG_ON(h<=B_THICKNESS || w<=B_THICKNESS);
    V("%s: %d %d %d %d", __func__, x,y,w,h);

    h = MIN(h, the_fb.height - y); 
    w = MIN(w, the_fb.width - x);     

    for (int j=0; j<B_THICKNESS; j++) {
        t0 = the_fb.fb + (y+j)*the_fb.pitch + x*PIXELSIZE;
        b0 = the_fb.fb + (y+h+j-B_THICKNESS)*the_fb.pitch + x*PIXELSIZE;
        t = (unsigned int *)t0; b = (unsigned int *)b0;
        for (int i=0; i<w; i++)
            t[i] = b[i] = clr;
    }

    for (int yy=y; yy<y+h; yy++) {
        t0 = the_fb.fb + yy*the_fb.pitch + x*PIXELSIZE;
        b0 = the_fb.fb + yy*the_fb.pitch + (x+w-B_THICKNESS)*PIXELSIZE;
        t = (unsigned int *)t0; b = (unsigned int *)b0;
        for (int i=0; i<B_THICKNESS;i++)
            t[i] = b[i] = clr;
    }

    return 0; 
}

extern int sys_uptime(); 
#define MAX_SF  20
//quest: desktop
static int sf_composite(void) {
    unsigned char *p0, *p1, cnt=0;
    slist_t *node = 0; 
    struct sf_struct *sf;
    struct region redrawn_regions[MAX_SF]; int n_redrawn = 0; 

    acquire(&mboxlock);
    if (!the_fb.fb) {release(&mboxlock); return 0;}
    
    V("%s starts >>>>>>> ", __func__);  __attribute__ ((unused)) int t00 = sys_uptime(); 
    if (bk_dirty.w || bk_dirty.h) {
        int y0=MAX(bk_dirty.y, 0), y1=MIN(bk_dirty.y+bk_dirty.h, VH);
        int x0=MAX(bk_dirty.x, 0), x1=MIN(bk_dirty.x+bk_dirty.w, VW);
        I("%s: draw bkgnd %d %d %d %d", __func__, x0,y0,x1,y1); 
        for (int i=y0; i<y1;i++) {
            unsigned int *p0 = (unsigned int *)(the_fb.fb + the_fb.pitch*i); 
            for (int j=x0; j<x1;j++)
                p0[j]=BK_COLOR;
        }
        redrawn_regions[n_redrawn++] = bk_dirty; 
        bk_dirty.x=bk_dirty.y=bk_dirty.w=bk_dirty.h=0;
    }

    slist_t tmplist = SLIST_OBJECT_INIT(tmplist), *ll=0; 
    struct region bbb = {.x=0, .y=0, .w=0, .h=0};
    for (int pass=0;pass<2;pass++) {
        if (pass==0) ll=&sflist; else ll=&tmplist; 
        slist_for_each(node, ll) { 
            if (pass==0) {
                sf = slist_entry(node, struct sf_struct, list);
                if (!node->next)
                    bbb = sf->r; 
                if (sf->floating) {
                    slist_append(&tmplist, &sf->list1);  
                    continue;
                }
            } else {
                sf = slist_entry(node, struct sf_struct, list1);
            }

            if (!sf->dirty) { 
                for (int i = 0; i < n_redrawn; i++) {
                    if (do_regions_intersect(&sf->r, &redrawn_regions[i])) {
                        sf->dirty = 1;
                        break;
                    }
                }
            }
            if (!sf->dirty)
                continue;
                            
            V("%s draw pass%d: pid %d; x %d y %d w %d h %d trans %d", __func__, 
                pass, sf->pid,
                sf->r.x, sf->r.y, sf->r.w, sf->r.h, sf->transparency); 

            p0 = the_fb.fb + sf->r.y * the_fb.pitch + sf->r.x*PIXELSIZE;
            p1 = sf->buf; 
            int hh = MIN(sf->r.h, the_fb.height - sf->r.y); 
            int ww = MIN(sf->r.w, the_fb.width - sf->r.x); 

            if (sf->r.x < 0 || sf->r.y < 0) {
                W("surface has negative location; clipping not implemented for negative coords");
                sf->dirty = 0;
                continue;
            }

            for (int j=0;j<hh;j++) {
                if (sf->transparency!=100) {
                    __asm_invalidate_dcache_range(p0, p0 + ww * PIXELSIZE);
                    int t1=sf->transparency, t0=100-t1;
                    for (int k=0;k<ww;k++) {
                        unsigned int *px0 = (unsigned int*)p0; 
                        unsigned int *px1 = (unsigned int*)p1; 

                        unsigned int dst = px0[k];
                        unsigned int src = px1[k];

                        unsigned int rb = (((src & 0x00ff00ff) * t1) + ((dst & 0x00ff00ff) * t0)) / 100;
                        unsigned int g  = (((src & 0x0000ff00) * t1) + ((dst & 0x0000ff00) * t0)) / 100;

                        px0[k] = (rb & 0x00ff00ff) | (g & 0x0000ff00);
                    }
                } else if ((unsigned long)p0%8==0 && (unsigned long)p1%8==0 && (sf->r.w*PIXELSIZE)%8==0)
                    memcpy_aligned(p0, p1, ww*PIXELSIZE);
                else
                    memcpy(p0, p1, ww*PIXELSIZE);
                p0 += the_fb.pitch; p1 += sf->r.w*PIXELSIZE;
            }

            if (n_redrawn < MAX_SF)
                redrawn_regions[n_redrawn++] = sf->r;
            cnt++; 
            sf->dirty = 0;
        }
    }

    if (cnt) {
        draw_boundary(bbb.x,bbb.y,bbb.w,bbb.h, B_COLOR);
        __asm_flush_dcache_range(the_fb.fb, the_fb.fb+the_fb.size); 

#if PROFILE_SF
        static unsigned total=0, total_self=0, cnt=0, last=0;
        unsigned t0 = sys_uptime(); 
        total += (t0 - last); cnt ++; 
        total_self += (t0 - t00); 
        if (total > 1000) {
            printf("%s: avg interval %d ms. self %d ms\n", 
                __func__, total/cnt, total_self/cnt);
            cnt = total = total_self = 0; 
        }
        last = t0; 
#endif
    }
    release(&mboxlock);

    V("%s done. %d ms", __func__, sys_uptime()-t00);
    return cnt; 
}

//quest: desktop
static void sf_task(int arg) {
    __attribute__ ((unused)) int n;
    I("%s starts", __func__);

    acquire(&sflock); 
    while (1)  { 
        n = sf_composite();

        while (n == 0) {
            sleep(&sflist, &sflock);
            n = sf_composite();
        }
    }
    release(&sflock);
}
    
/**********************
    devfs, procfs interfaces                    
**********************/

int procfs_parse_fbctl0(int args[PROCFS_MAX_ARGS]) {  
    int cmd = args[0], ret = 0, pid = sys_getpid(); 
    switch(cmd) 
    {
    case FB0_CMD_INIT:
        I("FB0_CMD_INIT called"); 
        ret = sf_create(pid, args[1], args[2], args[3], args[4], args[5], args[6]);
        break; 
    case FB0_CMD_FINI:
        ret = sf_free(pid); 
        break; 
    case FB0_CMD_CONFIG:
        ret = sf_config(pid, args[1], args[2], args[3], args[4], args[5]);
        break; 
    case FB0_CMD_TEST:
        acquire(&sflock); 
        ret = sf_composite();
        release(&sflock); 
        break; 
    default:
        W("unknown cmd %d", cmd); 
        break;
    }
    return ret; 
}

//quest: desktop
int devfb0_write(int user_src, uint64 src, int off, int n, void *content) {
    int ret = 0, len, pid = sys_getpid(); 
    slist_t *node = 0;
    struct sf_struct *sf=0;

    acquire(&sflock);
    slist_for_each(node, &sflist) {
        struct sf_struct *sff = slist_entry(node, struct sf_struct, list); BUG_ON(!sff);
        if (sff->pid == pid)
            sf=sff;
        if (sf) sff->dirty=1; 
    }
    if (!sf) {BUG(); ret=-1; goto out;} 
    BUG_ON(!sf->buf); 
    
    int max_size = sf->r.w * sf->r.h * PIXELSIZE;
    if (off < 0 || off >= max_size) {
        ret = -1;
        goto out;
    }

    len = MIN(n, max_size - off);
    if (len <= 0) {
        ret = 0;
        goto out;
    }

    if (either_copyin(sf->buf + off, user_src, src, len) == -1) {
        ret = -1;
        goto out;
    }

    sf->dirty = 1;
    ret = len;
    wakeup(&sflist);
out:     
    release(&sflock); 
    return ret; 
}

extern int sys_exit(int c); 
//quest: desktop
static void kb_task(int arg) {
    struct kbevent ev;
    struct sf_struct *top=0; 

    I("%s starts", __func__);

    while (1)  {
        acquire(&the_kb.lock);

        while (the_kb.r == the_kb.w && !kb_task_quit) {         
            sleep(&the_kb.r, &the_kb.lock);
        }
        if (kb_task_quit) {
            release(&the_kb.lock);I("%s quits", __func__);sys_exit(0);
        }
        
        ev = the_kb.buf[the_kb.r % INPUT_BUF_SIZE];
        the_kb.r++;
        release(&the_kb.lock);

        if ((ev.mod & KEY_MOD_LCTRL) && (ev.type==KEYUP)) {
            if (ev.scancode == KEY_TAB) {
                sf_cycle_focus();
                continue;
            }
            // Arrow order assumed by assignment comment: R/L/Dn/Up = 0/1/2/3.
            // If these names do not exist in kb.h, replace with the actual scancode constants.
#ifdef KEY_RIGHT
            if (ev.scancode == KEY_RIGHT) { sf_move(0); continue; }
#endif
#ifdef KEY_LEFT
            if (ev.scancode == KEY_LEFT) { sf_move(1); continue; }
#endif
#ifdef KEY_DOWN
            if (ev.scancode == KEY_DOWN) { sf_move(2); continue; }
#endif
#ifdef KEY_UP
            if (ev.scancode == KEY_UP) { sf_move(3); continue; }
#endif
        }
        
        acquire(&sflock); 
        if (slist_len(&sflist)>0) {
            top = slist_tail_entry(&sflist, struct sf_struct, list); 
            V("ev: %s mod %04x scan %04x dispatch to: pid %d", 
                ev.type?"KEYUP":"KEYDOWN", ev.mod, ev.scancode, top->pid); 

            if (((top->kb.w + 1) % INPUT_BUF_SIZE) != top->kb.r) {
                top->kb.buf[top->kb.w % INPUT_BUF_SIZE] = ev;
                top->kb.w = (top->kb.w + 1) % INPUT_BUF_SIZE;
                wakeup(&top->kb.r);
            } else {
                W("surface kb buffer full, dropping event");
            }
        } else {
            I("ev: %s mod %04x scan %04x (no surface to dispatch)", 
                ev.type?"KEYUP":"KEYDOWN", ev.mod, ev.scancode); 
        }
        release(&sflock);
    }
}

//quest: desktop. ref to kb_read() in kb.c
int kb0_read(int user_dst, uint64 dst, int off, int n, char blocking, void *content) {
    uint target = n; 
    struct kbevent ev;
#define TXTSIZE 20     
    char ev_txt[TXTSIZE]; 
    struct sf_struct *sf; 
    int pid = sys_getpid(); 

    V("called user_dst %d", user_dst);

    acquire(&sflock);

    if (!(sf=find_sf(pid))) {
        release(&sflock);
        return -1; 
    }
    struct kb_struct *kb = &sf->kb;

    while (n > 0) {
        if (!blocking && (kb->r == kb->w)) break;

        while (kb->r == kb->w) {
            sleep(&kb->r, &sflock);
        }

        if (n < TXTSIZE) break;

        ev = kb->buf[kb->r % INPUT_BUF_SIZE];
        kb->r = (kb->r + 1) % INPUT_BUF_SIZE;

        int len = snprintf(ev_txt, TXTSIZE, "%s 0x%02x\n", 
            ev.type == KEYDOWN ? "kd":"ku", ev.scancode); 
        BUG_ON(len < 0 || len >= TXTSIZE);

        if (n < len) break;
        
        if (either_copyout(user_dst, dst, ev_txt, len) == -1)
            break;

        dst+=len; n-=len;        

        break; 
    }
    release(&sflock);
    return target - n;
}

int start_sf(void) {
    devsw[KEYBOARD0].read = kb0_read;
    devsw[KEYBOARD0].write = 0;

    devsw[FRAMEBUFFER0].read = 0;
    devsw[FRAMEBUFFER0].write = devfb0_write;

    int res = copy_process(PF_KTHREAD, (unsigned long)&sf_task, 0, "[sf]");
    BUG_ON(res < 0);
    return 0; 
}
