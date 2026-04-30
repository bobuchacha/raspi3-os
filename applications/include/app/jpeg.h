#ifndef ROS_APP_JPEG_H
#define ROS_APP_JPEG_H

#include "types.h"
#include "user_runtime.h"

#define ROS_JPEG_CLIENT_MODULE_NAME "jpeg.dll"

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct JpegRenderTarget {
        U32 width;
        U32 height;
        U32 pitch;
        U32 pixel_format;
        void* pixels;
    } JpegRenderTarget;

#if defined(ROS_JPEG_EXPORTS) && !defined(ROS_BUILDING_JPEG_DLL)
#error "ROS_JPEG_EXPORTS is reserved for the dedicated jpeg.dll build"
#endif

#if defined(ROS_JPEG_EXPORTS)

    /*
     * Decode one JPEG image and paint it into a caller-provided surface.
     *
     * @param jpeg_bytes Pointer to the complete JPEG byte stream.
     * @param jpeg_size JPEG byte count.
     * @param target Destination surface description.
     * @param background_color Fill color used when decode fails or leaves gaps.
     * @return Zero on success, or a negative status code on failure.
     */
    long jpeg_render_to_surface(const U8* jpeg_bytes, U32 jpeg_size, const JpegRenderTarget* target, U32 background_color);

#elif !defined(ROS_JPEG_NO_IMPORTS)

    DECLARE(long, jpeg_render_to_surface, (const U8* jpeg_bytes, U32 jpeg_size, const JpegRenderTarget* target, U32 background_color), FROM, ROS_JPEG_CLIENT_MODULE_NAME, "jpeg_render_to_surface");

#endif

#ifdef __cplusplus
}
#endif

#endif