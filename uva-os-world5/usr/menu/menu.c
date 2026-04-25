#include "SDL.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// menu-sh.c
extern int sh_main(void); 
extern int run_script(const char *p);

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;

int total_row = 0; 
int n_actual_items = 0;
int n_selected = -1; // currently selected item

// default values, in pixels
int margin_px = 10; 
int menu_item_height_px = 20; 

// default value, pixel
int font_width = 8;
int font_height = 16;

// canvas, in pixels
int W=480, H=320; // gamehat screen size

#define MAX_MENU_ITEMS 20
#define MAX_LINE_LENGTH 256

#define CFG_FILENAME "/d/menu.txt"

struct MenuItem {
  char *name, *cmd;
};

/* some dimensions: 
    waveshare scr 480x320
    FL's insignia 1360x768
    nes           256x240
    sysmon          200x80
    nplayer         400x100
    doom			640x400
  sf.c will clip by the screen boundary
*/
struct MenuItem  predefined_items[] = {
//   {"(No items)", CFG_FILENAME " not found"},
    {"mario(solo", "# execute one nes \n nes1 -- 0 0"},
    {"slider", "# one slider, direct mode\n slider"}, 
    {"mario1", "# execute one nes and a sysmon\n nes1 -- 0 0 &\n sysmon 0 240"},
    {"mario2", "# execute two nes and a sysmon\n nes1 -- 0 0 &\n nes1 -- 260 0 &\n sysmon 0 240"},
    {"mario4", "ONLY ON ext scr. gamehat too small\n"
               "# execute four nes and a sysmon\n nes1 -- 0 0 &\n nes1 -- 260 0 &\n nes1 -- 520 0 &\n nes1 -- 780 0 &\n sysmon 0 500"},
    {"mario8", "ONLY ON ext scr. gamehat too small\n"
                "# execute four nes and a sysmon\n"
                "nes1 -- 0 0 &\n nes1 -- 260 0 &\n nes1 -- 520 0 &\n nes1 -- 780 0 &\n"
                "nes1 -- 0 250 &\n nes1 -- 260 250 &\n nes1 -- 520 250 &\n nes1 -- 780 250 &\n"
                "sysmon 0 650\n"
                },
    {"doom(direct", "# execute doom\n doom"},
    {"doom(indirect", "# execute doom\n doom -offset 0 0"},
    {"doom(sysmon", "# execute doom and a sysmon\n doom -offset 0 0 &\n sysmon 0 280"},
    {"doom2", "# execute two dooms and a sysmon\n doom -offset 0 0 &\n doom -offset 700 0 &\n sysmon 0 500"},
    {"sysmon", "# execute sysmon\n sysmon 0 0"},
};

struct MenuItem loaded_items[MAX_MENU_ITEMS]; 

// video.c 
#define rgba_to_pixel(r, g, b, a) \
    (((char)a << 24) | ((char)r << 16) | ((char)g << 8) | ((char)b))


// "items" the space for menuitems, allocated
// "max_items" the num of menuitems in "items"
int load_menu_items(const char *filename, struct MenuItem *items, int max_items) {
    FILE *file = fopen(filename, "r");
    if (!file)
        return -1;

    static char line[MAX_LINE_LENGTH];
    int item_count = 0;
    struct MenuItem *current_item = NULL;

    while (fgets(line, sizeof(line), file)) {
        // Remove newline character at the end, if it exists
        line[strcspn(line, "\n")] = 0;
        // Remove possible carriage return character before newline
        line[strcspn(line, "\r")] = 0;
        // char *carriage_return = strchr(line, '\r');
        // if (carriage_return) {
        //     *carriage_return = 0;
        // }

        // Ignore comment lines
        // if (line[0] == '#') {
        //     continue;
        // }

        // Start of a new menu item (@title)
        if (line[0] == '@') {
            if (item_count >= max_items) {
                fprintf(stderr, "Max items exceeded\n");
                fclose(file);
                return -1;
            }

            // Allocate space for new menu item and update the name
            current_item = &items[item_count++];
            char *name_start = line + 1;
            while (*name_start == ' ') name_start++;  // Remove leading spaces
            current_item->name = strdup(name_start);
            current_item->cmd = NULL;
        }
        // If the line is not a comment or a title, treat it as a command for the current menu item
        else if (current_item) {
            if (!current_item->cmd) {
                current_item->cmd = strdup(line);
            } else {
                // Concatenate multiple lines to cmd if necessary
                size_t new_cmd_len = strlen(current_item->cmd) + strlen(line) + 2;
                char *new_cmd = malloc(new_cmd_len);
                snprintf(new_cmd, new_cmd_len, "%s\n%s", current_item->cmd, line);
                free((void *)current_item->cmd);
                current_item->cmd = new_cmd;
            }
        }
    }

    fclose(file);
    return item_count;
}

// show menu items, vertical layout, one item per line
static void display_menu_simple(void) {
    SDL_SetRenderDrawColor(renderer, 56, 56, 56, 0);  // gray
    SDL_RenderClear(renderer);
    char *tgt = renderer->tgt[renderer->cur_id];
    int pitch = renderer->pitch;
    int xx, yy; 
    static char menu_item[MAX_LINE_LENGTH];

    for (int i = 0; i < n_actual_items; i++) {
        int clr; 
        char cursor = ' '; 
        if (i == n_selected) {
            clr = rgba_to_pixel(0,255,0,0); 
            cursor = '>';
        } else 
            clr = rgba_to_pixel(255,255,255,0);
        xx = margin_px;         // new line 
        yy = margin_px + i * menu_item_height_px;
        if (yy + menu_item_height_px > renderer->h) {
            printf("warning: not all items can be displayed. increase window H\n");
            break;
        }
        // printf("tgt %p pitch %d xx %d yy %d\n", tgt, pitch, xx, yy);
        // printf("displaying %s at %d %d\n", loaded_items[i].name, xx, yy);
        // decoration...
        snprintf(menu_item, sizeof(menu_item), "%c %s", cursor, loaded_items[i].name);
        fb_print((unsigned char*)tgt, &xx, &yy, 
            menu_item, pitch, clr);
    }

    SDL_RenderPresent(renderer);
}

int util[MAX_NCPU], ncpu, isqemu=-1; 
static char build_string[MAX_LINE_LENGTH]={0};
static char disp_string[MAX_LINE_LENGTH]={0};

// read line 3 of /proc/cpuinfo, which is the build string.
static int read_build_string(char *buf, int nbuf) {
    FILE *file = fopen("/proc/cpuinfo", "r");
    if (!file)
        return -1;

    int line_count = 0;
    while (fgets(buf, nbuf, file)) {
        line_count++;
        if (line_count == 3) {
            fclose(file);
            buf[nbuf - 1] = '\0';
            return strlen(buf);
        }
    }
    fclose(file);
    return -1;
}

// compact layout
static void display_menu_compact(void) {
    SDL_SetRenderDrawColor(renderer, 56, 56, 56, 0);  // gray
    SDL_RenderClear(renderer);
    char *tgt = renderer->tgt[renderer->cur_id];
    int pitch = renderer->pitch;
    int xx, yy; 
    static char menu_item[MAX_LINE_LENGTH];

    xx = margin_px;
    yy = margin_px;

    // printf("selected %d %s\n", n_selected , loaded_items[n_selected].name);

    // menu item 
    for (int i = 0; i < n_actual_items; i++) {
        int clr; 
        char cursor = ' '; 
        if (i == n_selected) {
            clr = rgba_to_pixel(0,255,0,0); 
            cursor = '>';
        } else 
            clr = rgba_to_pixel(255,255,255,0);
        
        // decoration...
        snprintf(menu_item, sizeof(menu_item), "%c%s ", cursor, loaded_items[i].name);

        // Determine how many characters can be displayed in the current row
        int max_chars = (renderer->w - xx) / font_width;
        if (max_chars < strlen(menu_item)) {
            if (xx<=margin_px) { // single menu item is too long to fit on the screen
                menu_item[max_chars] = '\0';  // Truncate the string to fit the screen width
            } else {
                // Start a new row
                xx = margin_px;
                yy += menu_item_height_px;
                assert(yy < renderer->h);
            }
        }
        fb_print((unsigned char*)tgt, &xx, &yy, 
            menu_item, pitch, clr);        
    }

    // "command area" showing command content 
    xx = 0;
    yy += menu_item_height_px;    
    SDL_Rect r = {xx, yy, renderer->w, renderer->h - yy};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // black bkgnd 
    SDL_RenderFillRect(renderer, &r);
    fb_print((unsigned char*)tgt, &xx, &yy, 
        "\n", pitch, rgba_to_pixel(255,255,255,0));
    fb_print((unsigned char*)tgt, &xx, &yy, 
        loaded_items[n_selected].cmd, pitch, rgba_to_pixel(255,255,255,0));
    // XXX bail out on too long cmd content?

    // at the bottom, show build string in gray
    xx = 0;
    yy = renderer->h - menu_item_height_px * 2;
    fb_print((unsigned char*)tgt, &xx, &yy, 
        build_string, pitch, rgba_to_pixel(128,128,128,0));
    // show dispinfo in gray
    xx = 0;
    yy = renderer->h - menu_item_height_px;
    fb_print((unsigned char*)tgt, &xx, &yy, 
        disp_string, pitch, rgba_to_pixel(128,128,128,0));

    // qemu needs this additional call, otherwise scr wont update 
    //  likely due to its emulation of fb voffset is not proper?
    if (isqemu)
        SDL_RenderPresent(renderer);
    SDL_RenderPresent(renderer);
}

int main(int argc, char *argv[]) {
    int x=margin_px, y=margin_px;
    int simple = 0;    
    int flags = (SDL_WINDOW_HWSURFACE|SDL_WINDOW_FULLSCREEN);

    if (sh_main() < 0) {
        printf("failed to init shell\n");
        return -1;
    }

    read_cpuinfo(util, &ncpu, &isqemu); assert(isqemu!=-1);
    printf("isqemu = %d\n", isqemu); 

    if (argc>1 && strcmp(argv[1], "--simple")==0)  // one menu item per line
        simple = 1; 

    // dispinfo -> string
    {
        int dispinfo[MAX_DISP_ARGS], nargs;
        read_dispinfo(dispinfo, &nargs); assert(nargs > 0);
        snprintf(disp_string, sizeof(disp_string), 
            "%d x %d (v %d x %d) pitch %d",
            dispinfo[WIDTH], dispinfo[HEIGHT], dispinfo[VWIDTH],
            dispinfo[VHEIGHT], dispinfo[PITCH]);
        W=dispinfo[WIDTH]; H=dispinfo[HEIGHT];
    }

    SDL_Init(0);
    window = SDL_CreateWindow("MENU", x, y, W, H, flags);
    assert(window);
    renderer = SDL_CreateRenderer(window, -1 /*index*/, 0 /*flags*/);
    assert(renderer);

    // SDL_SetRenderDrawColor(renderer, 56, 56, 56, 0);  // gray
    // SDL_RenderClear(renderer);
    // SDL_RenderPresent(renderer);

    load_font("font.psf", &font_width, &font_height);

    // truncate to fit the screen
    read_build_string(build_string, sizeof(build_string));
    build_string[renderer->w/font_width] = '\0'; // not useful?

    // SDL_RenderPresent(renderer);

    n_actual_items = load_menu_items(CFG_FILENAME, loaded_items, MAX_MENU_ITEMS);
    if (n_actual_items < 0) {
        // printf("predfine items %lu \n", sizeof(predefined_items)/sizeof(struct MenuItem));
        // printf("loaded items %lu \n", sizeof(loaded_items)/sizeof(struct MenuItem));
        assert(sizeof(predefined_items) <= sizeof(loaded_items));
        memcpy(loaded_items, predefined_items, sizeof(predefined_items));
        n_actual_items = sizeof(predefined_items) / sizeof(struct MenuItem);
        printf("warning: failed to load %s. fallback: %d predefined items\n", 
            CFG_FILENAME, n_actual_items);
    }
    if (n_actual_items>0)
        n_selected = 0;

    while (1) {
        int ret; 
        if (simple) 
            display_menu_simple();
        else
            display_menu_compact();

        SDL_Event e;
        do {
            ret = SDL_WaitEvent(&e, SDL_EV_HW);
        } while (e.type != SDL_KEYDOWN || ret != 1);

        switch (e.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_PAGEUP:
        case SDLK_LEFT:
        case SDLK_TAB:
            n_selected = (n_selected - 1 + n_actual_items) % n_actual_items;
            break;
        case SDLK_DOWN:
        case SDLK_PAGEDOWN:
        case SDLK_RIGHT:
            n_selected = (n_selected + 1) % n_actual_items;
            break;
        case SDLK_RETURN:
            if (n_selected >= 0 && n_selected < n_actual_items) {
                printf("selected %s\n", loaded_items[n_selected].name);
                run_script(loaded_items[n_selected].cmd);
            }
            break; 
        }
    }
    return -1;
}
