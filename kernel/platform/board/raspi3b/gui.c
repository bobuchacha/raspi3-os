#include "graphics.h"
#include "gui.h"
#include "log.h"
#include "memory.h"

#define GUI_BACKGROUND 0x00101820U
#define GUI_PANEL 0x0023344FU
#define GUI_PANEL_ALT 0x00182A3AU
#define GUI_TEXT 0x00F8FAFCU
#define GUI_TEXT_MUTED 0x0094A3B8U
#define GUI_POINTER 0x00F97316U
#define GUI_CELL_WIDTH 8U
#define GUI_CELL_HEIGHT 16U
#define GUI_MARGIN_X 16U
#define GUI_MARGIN_Y 48U
#define GUI_TEXT_CAPACITY 4096U
#define GUI_MAX_CHARS 128U

typedef struct __attribute__((packed)) Psf2Header {
    unsigned int magic;
    unsigned int version;
    unsigned int header_size;
    unsigned int flags;
    unsigned int glyph_count;
    unsigned int char_size;
    unsigned int height;
    unsigned int width;
} Psf2Header;

extern const unsigned char _binary_build_screenfont_font_psf_start[];

static char gui_text_buffer[GUI_TEXT_CAPACITY];
static unsigned int gui_text_length;
static unsigned int gui_pointer_x;
static unsigned int gui_pointer_y;
static Bool gui_pointer_visible;
static Bool gui_has_boot_background;

static const Psf2Header* gui_font_header(void) {
    return (const Psf2Header*)_binary_build_screenfont_font_psf_start;
}

static const unsigned char* gui_font_glyph(unsigned int ch) {
    const Psf2Header* header = gui_font_header();
    const unsigned char* glyphs = _binary_build_screenfont_font_psf_start + header->header_size;

    if (ch >= header->glyph_count) {
        ch = '?';
    }
    return glyphs + ((ULong)ch * header->char_size);
}

static void gui_draw_char(unsigned int x, unsigned int y, unsigned char ch, unsigned int fg, unsigned int bg) {
    const Psf2Header* header = gui_font_header();
    const unsigned char* glyph = gui_font_glyph(ch);

    for (unsigned int row = 0; row < header->height; row++) {
        unsigned char bits = glyph[row];
        for (unsigned int col = 0; col < header->width; col++) {
            unsigned int color = (bits & (1U << (7U - col))) ? fg : bg;
            graphics_draw_pixel(x + col, y + row, color);
        }
    }
}

static void gui_draw_background(void) {
    graphics_clear(GUI_BACKGROUND);
    graphics_fill_rect(0, 0, graphics_width(), 32U, GUI_PANEL);
    graphics_fill_rect(0, 32U, graphics_width(), 2U, GUI_PANEL_ALT);
    graphics_fill_rect(24U, 10U, 120U, 12U, GUI_TEXT);
    graphics_fill_rect(24U, 10U, 84U, 12U, GUI_POINTER);
    graphics_fill_rect(160U, 10U, 180U, 12U, GUI_TEXT_MUTED);
    gui_has_boot_background = true;
}

static void gui_redraw(void) {
    unsigned int cursor_x = GUI_MARGIN_X;
    unsigned int cursor_y = GUI_MARGIN_Y;
    unsigned int visible_width = graphics_width();
    unsigned int visible_height = graphics_height();
    const Psf2Header* header = gui_font_header();

    if (!graphics_is_ready()) {
        return;
    }

    gui_draw_background();

    for (unsigned int index = 0; index < gui_text_length; index++) {
        unsigned char ch = (unsigned char)gui_text_buffer[index];

        if (ch == '\n') {
            cursor_x = GUI_MARGIN_X;
            cursor_y += GUI_CELL_HEIGHT;
            continue;
        }
        if (ch == '\r') {
            continue;
        }
        if (cursor_x + header->width > visible_width) {
            cursor_x = GUI_MARGIN_X;
            cursor_y += GUI_CELL_HEIGHT;
        }
        if (cursor_y + header->height > visible_height) {
            break;
        }
        gui_draw_char(cursor_x, cursor_y, ch, GUI_TEXT, GUI_BACKGROUND);
        cursor_x += GUI_CELL_WIDTH;
    }

    if (gui_pointer_visible&& gui_pointer_x < visible_width&& gui_pointer_y < visible_height) {
        unsigned int dot_x = gui_pointer_x > 5U ? gui_pointer_x - 5U : 0U;
        unsigned int dot_y = gui_pointer_y > 5U ? gui_pointer_y - 5U : 0U;
        graphics_fill_rect(dot_x, dot_y, 11U, 11U, GUI_POINTER);
        graphics_fill_rect(dot_x + 4U, dot_y + 4U, 3U, 3U, GUI_TEXT);
    }
}

static void gui_compact_buffer(void) {
    unsigned int keep = GUI_TEXT_CAPACITY / 2U;
    unsigned int drop = gui_text_length - keep;

    for (unsigned int index = 0; index < keep; index++) {
        gui_text_buffer[index] = gui_text_buffer[index + drop];
    }
    gui_text_length = keep;
    gui_text_buffer[gui_text_length] = '\0';
}

static void gui_append_char(unsigned char ch) {
    if (gui_text_length + 1U >= GUI_TEXT_CAPACITY) {
        gui_compact_buffer();
    }
    gui_text_buffer[gui_text_length++] = (char)ch;
    gui_text_buffer[gui_text_length] = '\0';
}

int gui_is_ready(void) {
    return graphics_is_ready();
}

void gui_reset(void) {
    gui_text_length = 0;
    gui_text_buffer[0] = '\0';
    gui_pointer_visible = true;
    gui_pointer_x = graphics_width() / 2U;
    gui_pointer_y = graphics_height() / 2U;

    if (!graphics_is_ready()) {
        return;
    }

    if (!gui_has_boot_background) {
        gui_draw_background();
    }
    gui_redraw();
}

long gui_key(unsigned long key, unsigned long unused) {
    (void)unused;

    if (!graphics_is_ready()) {
        return -1;
    }

    if (key == 8UL || key == 127UL) {
        if (gui_text_length > 0) {
            gui_text_length--;
            gui_text_buffer[gui_text_length] = '\0';
        }
        gui_redraw();
        return 0;
    }

    if (key == '\r') {
        key = '\n';
    }

    if (key == '\n' || key == '\t' || (key >= ' ' && key <= '~')) {
        gui_append_char((unsigned char)key);
        gui_redraw();
        return 0;
    }

    return 0;
}

long gui_pointer(unsigned long x, unsigned long y) {
    if (!graphics_is_ready()) {
        return -1;
    }

    if (x == GUI_POINTER_HIDDEN && y == GUI_POINTER_HIDDEN) {
        gui_pointer_visible = false;
        gui_redraw();
        return 0;
    }

    gui_pointer_visible = true;
    gui_pointer_x = (unsigned int)x;
    gui_pointer_y = (unsigned int)y;
    gui_redraw();
    return 0;
}

long gui_usb_enabled(unsigned long unused0, unsigned long unused1) {
    (void)unused0;
    (void)unused1;
#ifdef ROS_QEMU_USB_ENABLED
    return ROS_QEMU_USB_ENABLED ? 1 : 0;
#else
    return 0;
#endif
}
