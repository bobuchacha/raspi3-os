#pragma once

#define ROS_GDI_NO_IMPORTS 1
#include "app/gdi.h"
#undef ROS_GDI_NO_IMPORTS

#ifdef __cplusplus
extern "C" {
#endif

    long gwes_gdi_load_font(const char* path, unsigned long pixel_height, RosGdiFont* font);
    long gwes_gdi_unload_font(RosGdiFont* font);
    long gwes_gdi_measure_text(const RosGdiFont* font, const char* text, unsigned long* width, unsigned long* height);
    long gwes_gdi_draw_text_surface(const RosGdiSurface* surface, const RosGdiFont* font, unsigned long x, unsigned long y, const char* text, unsigned long color);

#ifdef __cplusplus
}
#endif
