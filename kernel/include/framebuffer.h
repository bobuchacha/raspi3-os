#ifndef KERNEL_INCLUDE_FRAMEBUFFER_H
#define KERNEL_INCLUDE_FRAMEBUFFER_H

#include "types.h"

enum : U32 {
    FramebufferIoctlGetGeometry = 1U,
};

enum : U32 {
    FramebufferPixelFormatXrgb8888 = 0x34325258U,
};

typedef struct FramebufferGeometry {
    U32 width;
    U32 height;
    U32 pitch;
    U32 bytes_per_pixel;
    U32 pixel_format;
    U64 size_bytes;
} FramebufferGeometry;

#endif // KERNEL_INCLUDE_FRAMEBUFFER_H