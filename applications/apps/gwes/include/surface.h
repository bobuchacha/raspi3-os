#pragma once
#include "rect.h"

struct Surface {
    U32 width;
    U32 height;
    U32 pitch;     // Bytes per row
    U32* pixels;   // ARGB8888 pixel format
};

inline void clear_surface(Surface& surface, U32 color) {
    for (U32 y = 0; y < surface.height; ++y) {
        U32* row =
            (U32*)((uint8_t*)surface.pixels + y * surface.pitch);
        for (U32 x = 0; x < surface.width; ++x) {
            row[x] = color;
        }
    }
}