#ifndef ROS_GRAPHICS_H
#define ROS_GRAPHICS_H

#include "ros.h"

int graphics_init(void);
int graphics_is_ready(void);
unsigned int graphics_width(void);
unsigned int graphics_height(void);
unsigned int graphics_pitch(void);
unsigned int graphics_is_rgb(void);
Address graphics_framebuffer(void);
void graphics_draw_pixel(unsigned int x, unsigned int y, unsigned int color);
void graphics_fill_rect(unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned int color);
void graphics_clear(unsigned int color);

#endif