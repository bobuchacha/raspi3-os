//
// Created by Jeff on 4/30/2024.
//

#include "terminal.h"
#include "../hardware/framebuffer/lfb.h"
#include "../hardware/uart/uart.h"
#include "../lib/sprintf.h"

extern volatile unsigned char _binary_screenfont_font_psf_start;
extern volatile unsigned char _binary_screenfont_font_sfn_start;

static int currentX = 0, currentY = 0;
static sfn_t *font;
unsigned int screen_width, screen_height, screen_pitch, screen_isrgb;
unsigned char *framebuffer;
unsigned int line_height;
static char printf_buffer[4096];
static int t_background = 0x000000;
static int t_foreground = 0xFFFFFF;

void terminal_init(){
    lfb_init();
    currentX = 0;
    currentY = 0;
    font = (sfn_t*)&_binary_screenfont_font_sfn_start;
    framebuffer = lfb_get_framebuffer_address();
    screen_width = lfb_get_width();
    screen_height = lfb_get_height();
    screen_pitch  = lfb_get_pitch();
    screen_isrgb = lfb_isrgb();
    line_height = font->height;
}

void _newline(){
    currentY += line_height;
}

void terminal_print_line(char* line){
    terminal_draw_string(0, currentY, line);
    _newline();
}

/**
 * scroll up number of lines
 * @param line
 */
void terminal_scroll_up(int line){
    lfb_scrollup (line * line_height);
    currentY -= line * line_height;     // set cursor to scroll as well
}

void terminal_draw_string(int x, int y, char *s)
{
    // get our font

    unsigned char *ptr, *chr, *frg;
    unsigned int c;
    unsigned long o, p;
    int i, j, k, l, m, n;

    unsigned char *lfb = lfb_get_framebuffer_address();

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
            x = 0;
            _newline();
            continue;
        } else
            // new line
        if(c == '\n') {
            x = 0;
            _newline();
            y += font->height;
            continue;
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
        ptr = chr + 6;
        o = (unsigned long)framebuffer + y * screen_pitch + x * 4;

        // draw background
        unsigned long o2;
        for (int y2=0; y2 < font->height; y2++){
            o2 = o + y2 * screen_pitch;
            for (int x2=0; x2 < font->width; x2++){
                *((unsigned int *) o2) = t_background;
                o2 += 4;
            }
        }

        // draw character
        for(i = n = 0; i < chr[1]; i++, ptr += chr[0] & 0x40 ? 6 : 5) {
            if(ptr[0] == 255 && ptr[1] == 255) continue;
            frg = (unsigned char*)font + (chr[0] & 0x40 ? ((ptr[5] << 24) | (ptr[4] << 16) | (ptr[3] << 8) | ptr[2]) :
                                          ((ptr[4] << 16) | (ptr[3] << 8) | ptr[2]));
            if((frg[0] & 0xE0) != 0x80) continue;

            o += (int)(ptr[1] - n) * screen_pitch;
            n = ptr[1];
            k = ((frg[0] & 0x1F) + 1) << 3;
            j = frg[1] + 1; frg += 2;

            for(m = 1; j;
                    j--,
                    n++,
                    o += screen_pitch) {

                for (p = o, l = 0; l < k;
                        l++,
                        p += 4,
                        m <<= 1) {

                    if (m > 0x80) {
                        frg++;
                        m = 1;
                    }
                    if (*frg & m) *((unsigned int *) p) = t_foreground;
                }
            }
        }

        // add advances
        x += chr[4]+1; y += chr[5];
    }
}


void terminal_set_foreground(int color){
    t_foreground = color;
}
void terminal_set_background(int color){
    t_background = color;
}

/**
 * printf to terminal
 * @param fmt
 * @param ...
 */
void terminal_printf(char *fmt, ...) {
    __builtin_va_list   args;
    __builtin_va_start(args, fmt);

    char *s = printf_buffer;

    // use sprintf to format our string
    vsprintf(s,fmt,args);

    // print out as usual
    terminal_print_line(s);

}