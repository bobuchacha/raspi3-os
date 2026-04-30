/*
 * gwe_graphics.h
 *
 * Public invalidation, paint, and composition API.
 */
#ifndef GWE_GRAPHICS_H
#define GWE_GRAPHICS_H

#include "gwe_api.h"

#ifdef __cplusplus
extern "C" {
#endif

GWE_RESULT gwe_invalidate_rect(PGWE_CONTEXT context, PGWE_WINDOW window, const GWE_RECT *rect);
GWE_RESULT gwe_begin_paint(PGWE_CONTEXT context, PGWE_WINDOW window, PGWE_PAINT_CONTEXT outPaint);
GWE_RESULT gwe_end_paint(PGWE_CONTEXT context, PGWE_PAINT_CONTEXT paint);
GWE_RESULT gwe_fill_paint_rect(PGWE_PAINT_CONTEXT paint, const GWE_RECT *rect, uint32_t color);
GWE_RESULT gwe_read_window_pixel(PGWE_WINDOW window, int32_t x, int32_t y, uint32_t *outColor);
GWE_RESULT gwe_read_desktop_pixel(PGWE_CONTEXT context, int32_t x, int32_t y, uint32_t *outColor);
GWE_RESULT gwe_compose(PGWE_CONTEXT context, size_t *outComposedCount);

#ifdef __cplusplus
}
#endif

#endif /* GWE_GRAPHICS_H */
