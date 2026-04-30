/*
 * gwe_graphics.c
 *
 * Backing surface allocation, invalidation, paint lifecycle, and simple
 * software composition into the desktop surface.
 */
#include "../include/gwe_internal.h"

#include <string.h>

static void
gwe_clear_desktop(PGWE_CONTEXT context, uint32_t color) {
    size_t index;
    size_t total;

    if (!context || !context->desktopSurface || !context->desktopSurface->pixels) {
        return;
    }

    total = context->desktopSurface->stride * (size_t)context->desktopSurface->height;
    for (index = 0u; index < total; ++index) {
        context->desktopSurface->pixels[index] = color;
    }
}

PGWE_SURFACE
gwe_create_surface(PGWE_CONTEXT context, int32_t width, int32_t height) {
    PGWE_SURFACE surface;
    size_t pixelCount;

    if (!context || width <= 0 || height <= 0) {
        return NULL;
    }

    surface = (PGWE_SURFACE)gwe_alloc(context, sizeof(*surface));
    if (!surface) {
        return NULL;
    }

    pixelCount = (size_t)width * (size_t)height;
    surface->pixels = (uint32_t *)gwe_alloc(context, pixelCount * sizeof(uint32_t));
    if (!surface->pixels) {
        gwe_free(context, surface);
        return NULL;
    }

    surface->width = width;
    surface->height = height;
    surface->stride = (size_t)width;
    memset(surface->pixels, 0, pixelCount * sizeof(uint32_t));
    return surface;
}

void
gwe_destroy_surface(PGWE_CONTEXT context, PGWE_SURFACE surface) {
    if (!surface) {
        return;
    }
    gwe_free(context, surface->pixels);
    gwe_free(context, surface);
}

GWE_RESULT
gwe_invalidate_rect(PGWE_CONTEXT context, PGWE_WINDOW window, const GWE_RECT *rect) {
    GWE_RECT localRect;
    GWE_RECT desktopRect;

    if (!context || !window) {
        return GWE_E_INVALID_ARG;
    }

    if (rect) {
        if (rect->w <= 0 || rect->h <= 0) {
            return GWE_E_INVALID_ARG;
        }
        localRect = *rect;
    } else {
        localRect = window->clientRect;
    }

    gwe_region_union_rect(&window->invalidRegion, localRect);
    desktopRect = gwe_rect_translate(localRect, window->windowRect.x, window->windowRect.y);
    gwe_region_union_rect(&context->desktopDamage, desktopRect);

    if (!(window->flags & GWE_WINDOW_NEEDS_PAINT)) {
        window->flags |= GWE_WINDOW_NEEDS_PAINT;
        return gwe_post_message(context, window, GWE_MSG_PAINT, 0u, 0);
    }
    return GWE_OK;
}

GWE_RESULT
gwe_begin_paint(PGWE_CONTEXT context, PGWE_WINDOW window, PGWE_PAINT_CONTEXT outPaint) {
    if (!context || !window || !outPaint || !window->surface) {
        return GWE_E_INVALID_ARG;
    }
    if (!window->invalidRegion.valid) {
        return GWE_E_STATE;
    }

    memset(outPaint, 0, sizeof(*outPaint));
    outPaint->window = window;
    outPaint->surface = window->surface;
    outPaint->clip = window->invalidRegion;
    outPaint->pixels = window->surface->pixels;
    outPaint->stride = window->surface->stride;
    outPaint->active = true;
    return GWE_OK;
}

GWE_RESULT
gwe_end_paint(PGWE_CONTEXT context, PGWE_PAINT_CONTEXT paint) {
    (void)context;

    if (!paint || !paint->active || !paint->window) {
        return GWE_E_INVALID_ARG;
    }

    gwe_region_clear(&paint->window->invalidRegion);
    paint->window->flags &= ~GWE_WINDOW_NEEDS_PAINT;
    paint->active = false;
    return GWE_OK;
}

GWE_RESULT
gwe_fill_paint_rect(PGWE_PAINT_CONTEXT paint, const GWE_RECT *rect, uint32_t color) {
    GWE_RECT clipRect;
    GWE_RECT targetRect;
    GWE_RECT surfaceRect;
    GWE_RECT fillRect;
    int32_t y;

    if (!paint || !paint->active || !paint->surface || !paint->pixels) {
        return GWE_E_INVALID_ARG;
    }

    clipRect = paint->clip.bounds;
    if (rect) {
        targetRect = *rect;
    } else {
        targetRect = clipRect;
    }

    surfaceRect.x = 0;
    surfaceRect.y = 0;
    surfaceRect.w = paint->surface->width;
    surfaceRect.h = paint->surface->height;

    if (!gwe_rect_intersect(targetRect, clipRect, &fillRect) ||
        !gwe_rect_intersect(fillRect, surfaceRect, &fillRect)) {
        return GWE_OK;
    }

    for (y = fillRect.y; y < fillRect.y + fillRect.h; ++y) {
        int32_t x;
        uint32_t *row = paint->pixels + ((size_t)y * paint->stride);
        for (x = fillRect.x; x < fillRect.x + fillRect.w; ++x) {
            row[x] = color;
        }
    }
    return GWE_OK;
}

GWE_RESULT
gwe_read_window_pixel(PGWE_WINDOW window, int32_t x, int32_t y, uint32_t *outColor) {
    if (!window || !window->surface || !outColor || x < 0 || y < 0 ||
        x >= window->surface->width || y >= window->surface->height) {
        return GWE_E_INVALID_ARG;
    }

    *outColor = window->surface->pixels[(size_t)y * window->surface->stride + (size_t)x];
    return GWE_OK;
}

GWE_RESULT
gwe_read_desktop_pixel(PGWE_CONTEXT context, int32_t x, int32_t y, uint32_t *outColor) {
    if (!context || !context->desktopSurface || !outColor || x < 0 || y < 0 ||
        x >= context->desktopSurface->width || y >= context->desktopSurface->height) {
        return GWE_E_INVALID_ARG;
    }

    *outColor = context->desktopSurface->pixels[(size_t)y * context->desktopSurface->stride + (size_t)x];
    return GWE_OK;
}

GWE_RESULT
gwe_compose(PGWE_CONTEXT context, size_t *outComposedCount) {
    PGWE_WINDOW window;
    size_t composedCount = 0u;

    if (!context || !context->desktopSurface) {
        return GWE_E_INVALID_ARG;
    }
    if (!context->desktopDamage.valid) {
        if (outComposedCount) {
            *outComposedCount = 0u;
        }
        return GWE_OK;
    }

    gwe_clear_desktop(context, 0xff101010u);

    for (window = context->zBottom; window; window = window->zAbove) {
        GWE_RECT damageWindowIntersect;

        if (!(window->flags & GWE_WINDOW_VISIBLE) || !window->surface) {
            continue;
        }

        if (!gwe_rect_intersect(context->desktopDamage.bounds, window->windowRect, &damageWindowIntersect)) {
            continue;
        }

        {
            int32_t y;
            for (y = 0; y < damageWindowIntersect.h; ++y) {
                int32_t x;
                size_t dstY = (size_t)(damageWindowIntersect.y + y);
                size_t srcY = (size_t)(damageWindowIntersect.y - window->windowRect.y + y);
                uint32_t *dstRow = context->desktopSurface->pixels + dstY * context->desktopSurface->stride;
                uint32_t *srcRow = window->surface->pixels + srcY * window->surface->stride;

                for (x = 0; x < damageWindowIntersect.w; ++x) {
                    size_t dstX = (size_t)(damageWindowIntersect.x + x);
                    size_t srcX = (size_t)(damageWindowIntersect.x - window->windowRect.x + x);
                    dstRow[dstX] = srcRow[srcX];
                }
            }
        }

        composedCount += 1u;
    }

    context->presentCount += 1u;
    gwe_region_clear(&context->desktopDamage);
    if (outComposedCount) {
        *outComposedCount = composedCount;
    }
    return GWE_OK;
}
