#ifndef ROS_APP_GDI_H
#define ROS_APP_GDI_H

#include "user_runtime.h"
#include "kernel_gui__.h"
#include "app/gdi_handle.h"
#include "app/gdi_bitmap.h"
#include "app/gdi_icon.h"
#include "app/gdi_image.h"

#define ROS_GDI_CLIENT_MODULE_NAME "gdi.dll"

typedef unsigned long HWND;

typedef struct RosGdiSurface {
    HWND hwnd;
    unsigned long width;
    unsigned long height;
    unsigned long pitch;
    unsigned long pixel_format;
    void* pixels;
} RosGdiSurface;

typedef struct RosGdiFont {
    unsigned long pixel_height;
    unsigned long line_height;
    long ascent;
    long descent;
    long line_gap;
    void* handle;
} RosGdiFont;

#if defined(ROS_GDI_EXPORTS) && !defined(ROS_BUILDING_GDI_DLL)
#error "ROS_GDI_EXPORTS is reserved for the dedicated gdi.dll wrapper build"
#endif

#if defined(ROS_GDI_EXPORTS)

/*
 * Acquire one mapped surface view for a GWES-owned window.
 *
 * The returned pixel pointer refers to a shared backing surface that is mapped
 * into the caller and into GWES at the same time. GDI backends render into that
 * buffer directly; callers then invalidate the changed region so GWES knows
 * what to composite.
 *
 * @param hwnd Target window handle previously returned by `CreateWindow`.
 * @param surface Receives the mapped surface description.
 * @return Zero on success, or a negative status code on failure.
 */
long GdiGetWindowSurface(HWND hwnd, RosGdiSurface* surface);

/*
 * Release one previously acquired surface view owned by the current process.
 *
 * @param hwnd Target window handle.
 * @return Zero on success, or a negative status code on failure.
 */
long GdiReleaseWindowSurface(HWND hwnd);

/*
 * Mark one window-surface rectangle dirty so GWES will composite it.
 *
 * @param hwnd Target window handle.
 * @param x Dirty region X coordinate relative to the surface.
 * @param y Dirty region Y coordinate relative to the surface.
 * @param width Dirty region width.
 * @param height Dirty region height.
 * @return Zero on success, or a negative status code on failure.
 */
long GdiInvalidateRect(HWND hwnd, unsigned long x, unsigned long y, unsigned long width, unsigned long height);

/*
 * Fill one rectangle through the active GDI backend on an acquired surface.
 *
 * @param surface Caller-visible surface view previously returned by GDI.
 * @param x Rectangle X coordinate relative to the surface.
 * @param y Rectangle Y coordinate relative to the surface.
 * @param width Rectangle width.
 * @param height Rectangle height.
 * @param color Solid fill color encoded in the surface pixel format.
 * @return Zero on success, or a negative status code on failure.
 */
long GdiFillSurfaceRect(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color);

/*
 * Convenience helper that acquires the target surface, draws the rectangle
 * through the software backend, and invalidates the updated region.
 *
 * @param hwnd Target window handle previously returned by `CreateWindow`.
 * @param x Rectangle X coordinate relative to the window surface.
 * @param y Rectangle Y coordinate relative to the window surface.
 * @param width Rectangle width.
 * @param height Rectangle height.
 * @param color Solid fill color encoded in the window surface pixel format.
 * @return Zero on success, or a negative status code on failure.
 */
long GdiFillRect(HWND hwnd, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color);

/*
 * Load one font file from the VFS into a process-local GDI font handle.
 *
 * Passing a null or empty path makes GDI choose the nearest staged raster
 * `C:\fonts\tahoma-*.rtf` asset for the requested height. When those staged
 * rasters are unavailable, GDI tries the fixed-size `C:\fonts\system_ui.rtf`
 * fallback, then `C:\fonts\system_ui.font`, and finally the compiled-in
 * System UI raster font so GUI text rendering still works without an external
 * dependency.
 *
 * @param path Absolute VFS path to a preferred `.rtf` file. `.font`
 * descriptors remain valid for the mini-font fallback, and explicit `.ttf`
 * loads still exist as a legacy compatibility path.
 * @param pixel_height Requested rendered text height in pixels.
 * @param font Receives the loaded font handle and metrics.
 * @return Zero on success, or a negative status code on failure.
 */
long GdiLoadFont(const char* path, unsigned long pixel_height, RosGdiFont* font);

/*
 * Destroy one loaded font handle and release all glyph caches.
 *
 * @param font Caller-owned font wrapper previously initialized by GDI.
 * @return Zero on success, or a negative status code on failure.
 */
long GdiUnloadFont(RosGdiFont* font);

/*
 * Measure one multi-line string using the supplied font metrics.
 *
 * @param font Loaded font handle.
 * @param text Null-terminated string to measure.
 * @param width Receives the maximum line width in pixels.
 * @param height Receives the total text block height in pixels.
 * @return Zero on success, or a negative status code on failure.
 */
long GdiMeasureText(const RosGdiFont* font, const char* text, unsigned long* width, unsigned long* height);

/*
 * Draw one multi-line text block directly into an acquired surface with
 * explicit foreground and optional background fill colors.
 *
 * @param surface Caller-visible surface previously returned by GDI.
 * @param font Loaded font handle.
 * @param x Text-box X coordinate.
 * @param y Text-box Y coordinate.
 * @param text Null-terminated string to render.
 * @param foreground_color RGB text color.
 * @param opaque_background Non-zero when GDI should fill the text bounds first.
 * @param background_color RGB fill color used when `opaque_background` is non-zero.
 * @return Zero on success, or a negative status code on failure.
 */
long GdiDrawTextSurfaceEx(const RosGdiSurface* surface, const RosGdiFont* font, unsigned long x, unsigned long y, const char* text, unsigned long foreground_color, int opaque_background, unsigned long background_color);

/*
 * Draw one multi-line text block directly into an acquired surface.
 *
 * The origin is the top-left of the text box, not the baseline, so callers can
 * center and align text without having to apply font ascent manually.
 *
 * @param surface Caller-visible surface previously returned by GDI.
 * @param font Loaded font handle.
 * @param x Text-box X coordinate.
 * @param y Text-box Y coordinate.
 * @param text Null-terminated string to render.
 * @param color RGB text color.
 * @return Zero on success, or a negative status code on failure.
 */
long GdiDrawTextSurface(const RosGdiSurface* surface, const RosGdiFont* font, unsigned long x, unsigned long y, const char* text, unsigned long color);

/*
 * Acquire one window surface, draw text with explicit foreground and optional
 * background fill colors, and invalidate the updated bounds.
 *
 * @param hwnd Target window handle previously returned by `CreateWindow`.
 * @param font Loaded font handle.
 * @param x Text-box X coordinate relative to the window surface.
 * @param y Text-box Y coordinate relative to the window surface.
 * @param text Null-terminated string to render.
 * @param foreground_color RGB text color.
 * @param opaque_background Non-zero when GDI should fill the text bounds first.
 * @param background_color RGB fill color used when `opaque_background` is non-zero.
 * @return Zero on success, or a negative status code on failure.
 */
long GdiDrawTextEx(HWND hwnd, const RosGdiFont* font, unsigned long x, unsigned long y, const char* text, unsigned long foreground_color, int opaque_background, unsigned long background_color);

#elif !defined(ROS_GDI_NO_IMPORTS)

DECLARE(long, GdiGetWindowSurface, (HWND hwnd, RosGdiSurface* surface), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiGetWindowSurface");
DECLARE(long, GdiReleaseWindowSurface, (HWND hwnd), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiReleaseWindowSurface");
DECLARE(long, GdiInvalidateRect, (HWND hwnd, unsigned long x, unsigned long y, unsigned long width, unsigned long height), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiInvalidateRect");
DECLARE(long, GdiFillSurfaceRect, (const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiFillSurfaceRect");
DECLARE(long, GdiFillRect, (HWND hwnd, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiFillRect");
DECLARE(long, GdiLoadFont, (const char* path, unsigned long pixel_height, RosGdiFont* font), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiLoadFont");
DECLARE(long, GdiUnloadFont, (RosGdiFont* font), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiUnloadFont");
DECLARE(long, GdiMeasureText, (const RosGdiFont* font, const char* text, unsigned long* width, unsigned long* height), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiMeasureText");
DECLARE(long, GdiDrawTextSurfaceEx, (const RosGdiSurface* surface, const RosGdiFont* font, unsigned long x, unsigned long y, const char* text, unsigned long foreground_color, int opaque_background, unsigned long background_color), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiDrawTextSurfaceEx");
DECLARE(long, GdiDrawTextSurface, (const RosGdiSurface* surface, const RosGdiFont* font, unsigned long x, unsigned long y, const char* text, unsigned long color), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiDrawTextSurface");
DECLARE(long, GdiDrawTextEx, (HWND hwnd, const RosGdiFont* font, unsigned long x, unsigned long y, const char* text, unsigned long foreground_color, int opaque_background, unsigned long background_color), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiDrawTextEx");

#endif

#endif
