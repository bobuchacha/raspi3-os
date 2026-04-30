#ifndef ROS_APP_PNG_H
#define ROS_APP_PNG_H

#include "types.h"
#include "user_runtime.h"

#define ROS_PNG_CLIENT_MODULE_NAME "png.dll"

/*
 * One caller-provided destination canvas for PNG rendering.
 *
 * The library treats `buffer` as a tightly packed 32-bit ARGB8888 pixel array.
 * The `x` and `y` fields place the PNG inside that canvas, while `width` and
 * `height` describe the destination canvas size in pixels.
 */
typedef struct RosPngMemory {
    U32 x;
    U32 y;
    U32 width;
    U32 height;
    void* buffer;
} RosPngMemory;

#if defined(ROS_PNG_EXPORTS) && !defined(ROS_BUILDING_PNG_DLL)
#error "ROS_PNG_EXPORTS is reserved for the dedicated png.dll build"
#endif

#if defined(ROS_PNG_EXPORTS)

/*
 * Read one PNG file and report its intrinsic dimensions.
 *
 * @param path Absolute or relative VFS path to the PNG file.
 * @param width Receives the decoded image width.
 * @param height Receives the decoded image height.
 * @return Zero on success, or a negative status code on failure.
 */
long PngGetInfo(const char* path, U32* width, U32* height);

/*
 * Parse one PNG byte stream and report its intrinsic dimensions.
 *
 * This lets UI code consume bytes that were staged asynchronously by another
 * loader thread without reopening the source file.
 *
 * @param bytes Pointer to the complete PNG byte stream.
 * @param size Byte count stored in `bytes`.
 * @param width Receives the decoded image width.
 * @param height Receives the decoded image height.
 * @return Zero on success, or a negative status code on failure.
 */
long PngGetInfoFromMemory(const void* bytes, U32 size, U32* width, U32* height);

/*
 * Read one PNG file and render it into a caller-provided memory canvas.
 *
 * The destination buffer must contain at least `width * height` pixels stored
 * as tightly packed 32-bit ARGB8888 values. The PNG is drawn at the canvas
 * offset stored in `x` and `y`.
 *
 * @param path Absolute or relative VFS path to the PNG file.
 * @param memory Destination canvas description and pixel buffer.
 * @return Zero on success, or a negative status code on failure.
 */
long PngRenderFileToMemory(const char* path, RosPngMemory* memory);

/*
 * Render one PNG byte stream into a caller-provided memory canvas.
 *
 * The destination buffer must contain at least `width * height` pixels stored
 * as tightly packed 32-bit ARGB8888 values. The PNG is drawn at the canvas
 * offset stored in `x` and `y`.
 *
 * @param bytes Pointer to the complete PNG byte stream.
 * @param size Byte count stored in `bytes`.
 * @param memory Destination canvas description and pixel buffer.
 * @return Zero on success, or a negative status code on failure.
 */
long PngRenderMemoryToMemory(const void* bytes, U32 size, RosPngMemory* memory);

#elif !defined(ROS_PNG_NO_IMPORTS)

DECLARE(long, PngGetInfo, (const char* path, U32* width, U32* height), FROM, ROS_PNG_CLIENT_MODULE_NAME, "PngGetInfo");
DECLARE(long, PngGetInfoFromMemory, (const void* bytes, U32 size, U32* width, U32* height), FROM, ROS_PNG_CLIENT_MODULE_NAME, "PngGetInfoFromMemory");
DECLARE(long, PngRenderFileToMemory, (const char* path, RosPngMemory* memory), FROM, ROS_PNG_CLIENT_MODULE_NAME, "PngRenderFileToMemory");
DECLARE(long, PngRenderMemoryToMemory, (const void* bytes, U32 size, RosPngMemory* memory), FROM, ROS_PNG_CLIENT_MODULE_NAME, "PngRenderMemoryToMemory");

#endif

#endif
