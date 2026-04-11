#pragma once

#include "types.h"

struct JpegRenderTarget {
    U32 width;
    U32 height;
    U32 pitch;
    U32 pixel_format;
    void* pixels;
};

long jpeg_render_to_surface(const U8* jpeg_bytes, U32 jpeg_size, const JpegRenderTarget* target, U32 background_color);
