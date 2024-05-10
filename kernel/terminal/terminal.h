//
// Created by Jeff on 4/30/2024.
//

#ifndef RASPO3B_OS_TERMINAL_H
#define RASPO3B_OS_TERMINAL_H

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

void terminal_init();
void terminal_print_line(char* line);
void terminal_scroll_up(int line);
void terminal_draw_string(int x, int y, char *s);
void terminal_printf(char *fmt, ...);
void terminal_set_foreground(int color);
void terminal_set_background(int color);

#endif //RASPO3B_OS_TERMINAL_H
