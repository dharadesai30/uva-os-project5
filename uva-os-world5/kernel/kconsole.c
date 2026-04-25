
/*
    A simple kernel console that prints out to the screen
    so that we can see kernel messages even without UART attached.
    This can be useful for demo or debugging with HAT on (therefore
    UART is not available).

    Two possible designs: 
    1. Render the whole text buffer for each new line.
    2. Use a circular framebuffer (2x the screen height) and update the
       framebuffer virtual offset as it scrolls. Copy the bottom half to the top
       half when the offset reaches the end of the top half, and wrap around.

    We use method 1 as of now because it's simpler. For each rendering cycle, we
    only have to write to memory and then flush. Method 2 might not be faster,
    as it needs to send mailbox messages for each scroll.

    Afterthought: it's more complex than I thought. Because of scrolling, we 
    are "recording" the printed text in a buffer, and for scrolling, "replaying" the
    text buffer to the framebuffer. 
    Then we have to handle ANSI color codes, which complicates the buffer 
    (x cursor offsets no longer map to text buffer offsets).

    Maybe the 2nd method would be simpler?
*/

#include "utils.h"
#include "fb.h"

int kconsole_printf = 0;  // default off, as kernel boots & before 1st fb_init() 

// Cursor position
static int cursor_x = 0;
static int cursor_y = 0;

// console "canvas" size in pixels,
// same as the minimum scr res (for qemu). rpi3 hw should be larger..
static int SCREEN_WIDTH = 320;
static int SCREEN_HEIGHT = 240; 

// Text buffer to store console contents (assuming 16x8 font)
static int MAX_ROWS;
static int MAX_COLS;

static char **text_buffer; // 2D array of characters

static int buffer_row_count = 0;
static int buffer_start_row = 0;
static int buffer_col = 0; // track column offset in text_buffer, 
// b/c we have ANSI color code, text buffer offset cannot be simply computed
// out of x_cursor

// Get the PSF font
static psf_t *font = (psf_t*)&_binary_font_psf_start;

void kconsole_putc(char ch); 
void kconsole_puts(const char *str);

// Current text color
static unsigned int current_color = 0xFFFFFF; // Default to white

// Function to parse and apply ANSI color codes
static void parse_ansi_code(const char *code) {
    if (strncmp(code, "[0;31m", 10) == 0) {
        current_color = 0xFF0000; // Red
    } else if (strncmp(code, "[0;32m", 10) == 0) {
        current_color = 0x00FF00; // Green
    } else if (strncmp(code, "[0;33m", 10) == 0) {
        current_color = 0xFFFF00; // Yellow
    }else if (strncmp(code, "[0;34m", 10) == 0) {
        current_color = 0x0000FF; // Blue
    } else if (strncmp(code, "[0m", 10) == 0) {
        current_color = 0xFFFFFF; // Reset to white
    }
}

// Function to scroll the console by re-rendering the entire text buffer
void console_scroll() {
    // Scroll the text buffer by moving rows up
    // buffer_start_row = (buffer_start_row + 1) % MAX_ROWS;
    // if (buffer_row_count < MAX_ROWS) {
    //     buffer_row_count--;
    // }

    // Clear the framebuffer
    unsigned char *fb = the_fb.fb;
    memset(fb, 0, SCREEN_HEIGHT * the_fb.pitch);    

    current_color = 0xFFFFFF; // Reset color to white

    // Re-render the entire text buffer without buffering again
    cursor_x = 0;
    cursor_y = 0;

    for (int i = 0; i < buffer_row_count; i++) {
        int row_index = (buffer_start_row + i) % MAX_ROWS;
        const char *str = text_buffer[row_index];

        // Render each character directly without storing in the buffer
        while (*str) {
            char ch = *str++;
            // Framebuffer parameters
            unsigned pitch = the_fb.pitch;
            unsigned char *fb = the_fb.fb;

            // Handle special characters
            if (ch == '\r') {
                cursor_x = 0;
                continue;
            }
            if (ch == '\n') {
                BUG(); // we don't store '\n' in text buffer
                cursor_x = 0;
                cursor_y += font->height;
                continue;
            }

            // Handle ANSI escape sequences
            static char ansi_buffer[16];
            static int ansi_index = 0;
            int display_char = 1;   // render "ch"? 
            if (ch == '\033') {
                ansi_index = 1;
                ansi_buffer[0] = ch;
                display_char = 0; 
            } else if (ansi_index > 0) {
                ansi_buffer[ansi_index++] = ch;
                if (ch == 'm' || ansi_index >= sizeof(ansi_buffer) - 1) {
                    ansi_buffer[ansi_index] = '\0';
                    parse_ansi_code(ansi_buffer + 1); // Skip the '\033'
                    ansi_index = 0;
                }
                display_char = 0;
            }

            // if (!display_char)
            //     V("ch %d display_char %d ansi_index %d cursor_x %d cursor_y %d", ch, display_char, ansi_index, cursor_x, cursor_y);

            if (!display_char) continue; 

            // Get the glyph offset for the character, adjust for numglyph limit
            unsigned char *glyph = (unsigned char*)&_binary_font_psf_start +
                                   font->headersize + ((unsigned char)ch < font->numglyph ? ch : 0) * font->bytesperglyph;

            // Calculate the offset in the framebuffer
            int offs = (cursor_y * pitch) + (cursor_x * 4);

            // Draw the character
            int i, j, line, mask, bytesperline = (font->width + 7) / 8;

            for (j = 0; j < font->height; j++) {
                line = offs;
                mask = 1 << (font->width - 1);
                for (i = 0; i < font->width; i++) {
                    // If bit is set, use current color, otherwise black (0x000000)
                    *((unsigned int *)(fb + line)) = ((*glyph) & mask) ? current_color : 0x000000;
                    mask >>= 1;
                    line += 4; // Move to the next pixel
                }
                // Move to the next row of the character
                glyph += bytesperline;
                offs += pitch;
            }

            // Update cursor position
            cursor_x += (font->width + 1);

            // Handle wrapping to the next line if needed
            // if (cursor_x >= SCREEN_WIDTH) {
            //     cursor_x = 0;
            //     cursor_y += font->height;
            // }
        }
        // current text row ends. change line on console
        cursor_x = 0;
        cursor_y += font->height;
    }

    __asm_flush_dcache_range(the_fb.fb, the_fb.fb + SCREEN_HEIGHT * the_fb.pitch);
}

// Function to print a character to the framebuffer
void kconsole_putc(char ch) {
    // Framebuffer parameters
    unsigned pitch = the_fb.pitch;
    unsigned char *fb = the_fb.fb;

    // Get the PSF font
    psf_t *font = (psf_t*)&_binary_font_psf_start;

    // Handle special characters first
    if (ch == '\r') {
        // cursor_x = 0;
        // buffer_col = 0;
        // just ignore. b/c kernel dbg msg always do \r\n
        return;
    }

    if (ch == '\n') {
        // Null-terminate the current line in the text buffer. not storing '\n' in buffer
        text_buffer[(buffer_start_row + buffer_row_count) % MAX_ROWS][buffer_col] = '\0';
        cursor_x = 0;
        cursor_y += font->height;
        buffer_col = 0;
        if (buffer_row_count < MAX_ROWS) {
            buffer_row_count++;
        } else {
            buffer_start_row = (buffer_start_row + 1) % MAX_ROWS;
        }
        if (cursor_y >= SCREEN_HEIGHT) {
            console_scroll(); // Handle scrolling if reaching the bottom of the screen
            cursor_y = SCREEN_HEIGHT - font->height;
        } else 
            __asm_flush_dcache_range(the_fb.fb, the_fb.fb + SCREEN_HEIGHT * the_fb.pitch); // scroll already flushed cache 
        return;
    }

    // Handle ANSI escape sequences
    static char ansi_buffer[16];
    static int ansi_index = 0;
    int display_char = 1;   // render "ch"? (we will store it anyways)
    if (ch == '\033') {
        ansi_index = 1;
        ansi_buffer[0] = ch;
        display_char = 0; 
    } else if (ansi_index > 0) {
        ansi_buffer[ansi_index++] = ch;
        if (ch == 'm' || ansi_index >= sizeof(ansi_buffer) - 1) {
            ansi_buffer[ansi_index] = '\0';
            parse_ansi_code(ansi_buffer + 1); // Skip the '\033'
            ansi_index = 0;
        }
        display_char = 0;
    }

    // Store character in the text buffer
    if (cursor_x / (font->width + 1) < MAX_COLS) {
        text_buffer[(buffer_start_row + buffer_row_count) % MAX_ROWS][buffer_col++] = ch;
    }

    // W("ch %d display_char %d ansi_index %d cursor_x %d cursor_y %d", ch, display_char, ansi_index, cursor_x, cursor_y);
    
    if (!display_char)
        return;
    
    // Get the glyph offset for the character, adjust for numglyph limit
    unsigned char *glyph = (unsigned char*)&_binary_font_psf_start +
                           font->headersize + ((unsigned char)ch < font->numglyph ? ch : 0) * font->bytesperglyph;

    // Calculate the offset in the framebuffer
    int offs = (cursor_y * pitch) + (cursor_x * 4);

    // Draw the character
    int i, j, line, mask, bytesperline = (font->width + 7) / 8;

    for (j = 0; j < font->height; j++) {
        line = offs;
        mask = 1 << (font->width - 1);
        for (i = 0; i < font->width; i++) {
            // If bit is set, use current color, otherwise black (0x000000)
            *((unsigned int *)(fb + line)) = ((*glyph) & mask) ? current_color : 0x000000;
            mask >>= 1;
            line += 4; // Move to the next pixel
        }
        // Move to the next row of the character
        glyph += bytesperline;
        offs += pitch;
    }

    // Update cursor position
    cursor_x += (font->width + 1);

    // Handle wrapping to the next line if needed
    if (cursor_x + font->width >= SCREEN_WIDTH) { // no space for one char
        // Null-terminate the current line in the text buffer
        text_buffer[(buffer_start_row + buffer_row_count) % MAX_ROWS][buffer_col] = '\0';
        cursor_x = 0;
        cursor_y += font->height;
        buffer_col = 0;
        if (buffer_row_count < MAX_ROWS) {
            buffer_row_count++;
        } else {
            buffer_start_row = (buffer_start_row + 1) % MAX_ROWS;
        }
        if (cursor_y >= SCREEN_HEIGHT) {
            console_scroll(); // Handle scrolling
            cursor_y = SCREEN_HEIGHT - font->height;
        }
    }
}

// Main function to test the console output
void kconsole_puts(const char *str) {
    // sanity check ...
    BUG_ON(the_fb.width < SCREEN_WIDTH || the_fb.height < SCREEN_HEIGHT);

    while (*str) {
        kconsole_putc(*str++);
    }
    // a big one. so that we dont have to flush for each char rendered
    __asm_flush_dcache_range(the_fb.fb, the_fb.fb + SCREEN_HEIGHT * the_fb.pitch);
}

// must be called after fb_init()
void kconsole_init() {
    // actual screen size
    SCREEN_HEIGHT = the_fb.height;
    SCREEN_WIDTH = the_fb.width;

    MAX_ROWS = SCREEN_HEIGHT / font->height; 
    MAX_COLS = SCREEN_WIDTH / font->width;

    text_buffer = malloc(MAX_ROWS * sizeof(char *)); BUG_ON(!text_buffer);
    for (int i = 0; i < MAX_ROWS; i++) {
        text_buffer[i] = malloc(MAX_COLS * sizeof(char)); BUG_ON(!text_buffer[i]);
        memset(text_buffer[i], 0, MAX_COLS);
    }
}

void kconsole_test() {
    // kconsole_puts("Hello, Kernel Console!\nThis is a simple kernel console.\n");
    kconsole_puts("This is a very very very very very very very very very very very very very long line");
    kconsole_puts("This is a very very very very very very very very very very very very very long line");
    kconsole_puts("This is a very very very very very very very very very very very very very long line");
    kconsole_puts("This is a very very very very very very very very very very very very very long line");
    kconsole_puts("This is a very very very very very very very very very very very very very long line");
    kconsole_puts("This is a very very very very very very very very very very very very very long line");
    kconsole_puts("This is a very very very very very very very very very very very very very long line");
    kconsole_puts("This is a very very very very very very very very very very very very very long line");
    kconsole_puts("This is a very very very very very very very very very very very very very long line");
    kconsole_puts("This is a very very very very very very very very very very very very very long line\n");

    // kconsole_puts("Hello, Kernel Console!\nThis is a simple kernel console.\033[0;31m\nThis text is red.\033[0m\nThis text is white again.");

    // scrolling...
    // for (int i = 0; i < 20; i++) {
    // for (int i = 0; i < 5; i++) {
    for (int i = 0; i < 2; i++) {
        char message[32];
        if (i%2)
            snprintf(message, sizeof(message), "\033[0;31mMessage number %d\033[0m\n", i + 1);        
        else 
            snprintf(message, sizeof(message), "Message number %d\n", i + 1);
        // V("buffer_start_row %d buffer_row_count %d", buffer_start_row, buffer_row_count); 
        kconsole_puts(message);
        ms_delay(100); // Delay 100 ms
    }    
}