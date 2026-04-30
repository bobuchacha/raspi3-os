#ifndef ROS_GDI_PRIVATE_H
#define ROS_GDI_PRIVATE_H

#include "app/user_ipc.h"
#include "app/kernel_gui.h"

/*
 * Clear one caller-owned byte range without depending on hosted libc.
 *
 * Keeping this helper in a private header lets the GDI core and font support
 * compile as separate translation units without text-including source files.
 *
 * @param destination Buffer to clear.
 * @param size Number of bytes to clear.
 * @return Nothing.
 */
static inline void gdi_zero_memory(void* destination, unsigned long size) {
    unsigned char* bytes = (unsigned char*)destination;
    unsigned long index;

    if (!bytes) {
        return;
    }

    for (index = 0UL; index < size; ++index) {
        bytes[index] = 0U;
    }
}

/*
 * Translate one surface color into the active shared-surface pixel format.
 *
 * The GWES compositor now reads the top byte as per-pixel alpha, so normal
 * GDI drawing must emit fully opaque pixels unless a caller bypasses the GDI
 * helpers and writes custom alpha directly.
 *
 * @param pixel_format Target `ROS_KERNEL_GUI_PIXEL_FORMAT_*` value.
 * @param color Caller-supplied RGB color value.
 * @return Encoded 32-bit surface pixel.
 */
static inline unsigned long gdi_encode_color(unsigned long pixel_format, unsigned long color) {
    const unsigned long rgb = color & 0x00FFFFFFUL;

    if (pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
        return 0xFF000000UL | rgb;
    }

    return 0xFF000000UL
        | ((rgb & 0x000000FFUL) << 16)
        | (rgb & 0x0000FF00UL)
        | ((rgb & 0x00FF0000UL) >> 16);
}

/*
 * Fill one rectangle through the software backend.
 *
 * @param surface Caller-visible surface wrapper.
 * @param x Rectangle X coordinate.
 * @param y Rectangle Y coordinate.
 * @param width Rectangle width.
 * @param height Rectangle height.
 * @param color Fill color in RGB order.
 * @return Zero on success, or a negative status code on failure.
 */
long gdi_software_fill_rect(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color);

#endif