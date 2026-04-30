/*
 * gwe_context.c
 *
 * Module lifetime, shared allocation helpers, region helpers, desktop state,
 * and thread-queue registration.
 */
#include "../include/gwe_internal.h"

#include <stdlib.h>
#include <string.h>

static void
gwe_destroy_all_windows(PGWE_CONTEXT context) {
    while (context->windowHead) {
        PGWE_WINDOW window = context->windowHead;
        context->windowHead = window->nextWindow;
        gwe_destroy_surface(context, window->surface);
        gwe_free(context, window);
    }
}

static void
gwe_destroy_all_queues(PGWE_CONTEXT context) {
    while (context->queueHead) {
        PGWE_THREAD_QUEUE queue = context->queueHead;
        context->queueHead = queue->nextQueue;

        while (queue->head) {
            GWE_MESSAGENODE *node = queue->head;
            queue->head = node->next;
            gwe_free(context, node);
        }
        gwe_free(context, queue);
    }
}

void *
gwe_alloc(PGWE_CONTEXT context, size_t size) {
    if (!context || !size) {
        return NULL;
    }
    if (context->hasCustomAlloc && context->api.heapAlloc) {
        return context->api.heapAlloc(size);
    }
    return calloc(1u, size);
}

void
gwe_free(PGWE_CONTEXT context, void *memory) {
    if (!memory) {
        return;
    }
    if (context && context->hasCustomAlloc && context->api.heapFree) {
        context->api.heapFree(memory);
        return;
    }
    free(memory);
}

void
gwe_copy_name(char *destination, const char *source) {
    size_t index;

    if (!destination) {
        return;
    }
    if (!source) {
        destination[0] = '\0';
        return;
    }

    for (index = 0u; index + 1u < GWE_NAME_MAX && source[index] != '\0'; ++index) {
        destination[index] = source[index];
    }
    destination[index] = '\0';
}

uint64_t
gwe_now(PGWE_CONTEXT context) {
    if (context && context->api.getTickCount) {
        return context->api.getTickCount();
    }
    return 0u;
}

void
gwe_trace(PGWE_CONTEXT context, int level, const char *message) {
    if (context && context->api.logLine && message) {
        context->api.logLine(level, message);
    }
}

void
gwe_region_clear(GWE_REGION *region) {
    if (!region) {
        return;
    }
    region->valid = false;
    memset(&region->bounds, 0, sizeof(region->bounds));
}

void
gwe_region_set_rect(GWE_REGION *region, GWE_RECT rect) {
    if (!region) {
        return;
    }
    if (rect.w <= 0 || rect.h <= 0) {
        gwe_region_clear(region);
        return;
    }
    region->valid = true;
    region->bounds = rect;
}

void
gwe_region_union_rect(GWE_REGION *region, GWE_RECT rect) {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;

    if (!region || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    if (!region->valid) {
        gwe_region_set_rect(region, rect);
        return;
    }

    left = (region->bounds.x < rect.x) ? region->bounds.x : rect.x;
    top = (region->bounds.y < rect.y) ? region->bounds.y : rect.y;
    right = (region->bounds.x + region->bounds.w > rect.x + rect.w)
                ? (region->bounds.x + region->bounds.w)
                : (rect.x + rect.w);
    bottom = (region->bounds.y + region->bounds.h > rect.y + rect.h)
                 ? (region->bounds.y + region->bounds.h)
                 : (rect.y + rect.h);

    region->bounds.x = left;
    region->bounds.y = top;
    region->bounds.w = right - left;
    region->bounds.h = bottom - top;
}

bool
gwe_rect_contains_point(GWE_RECT rect, GWE_POINT point) {
    return rect.w > 0 && rect.h > 0 &&
           point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.w && point.y < rect.y + rect.h;
}

bool
gwe_rect_intersect(GWE_RECT left, GWE_RECT right, GWE_RECT *outRect) {
    GWE_RECT result;
    int32_t x1 = (left.x > right.x) ? left.x : right.x;
    int32_t y1 = (left.y > right.y) ? left.y : right.y;
    int32_t x2 = (left.x + left.w < right.x + right.w) ? (left.x + left.w) : (right.x + right.w);
    int32_t y2 = (left.y + left.h < right.y + right.h) ? (left.y + left.h) : (right.y + right.h);

    if (x2 <= x1 || y2 <= y1) {
        return false;
    }

    result.x = x1;
    result.y = y1;
    result.w = x2 - x1;
    result.h = y2 - y1;

    if (outRect) {
        *outRect = result;
    }
    return true;
}

GWE_RECT
gwe_rect_translate(GWE_RECT rect, int32_t dx, int32_t dy) {
    rect.x += dx;
    rect.y += dy;
    return rect;
}

PGWE_THREAD_QUEUE
gwe_lookup_queue(PGWE_CONTEXT context, uint32_t threadId, bool createIfMissing) {
    PGWE_THREAD_QUEUE queue;

    if (!context || !threadId) {
        return NULL;
    }

    for (queue = context->queueHead; queue; queue = queue->nextQueue) {
        if (queue->threadId == threadId) {
            return queue;
        }
    }

    if (!createIfMissing) {
        return NULL;
    }

    queue = (PGWE_THREAD_QUEUE)gwe_alloc(context, sizeof(*queue));
    if (!queue) {
        return NULL;
    }

    queue->threadId = threadId;
    queue->nextQueue = context->queueHead;
    context->queueHead = queue;
    return queue;
}

PGWE_WINDOW
gwe_hit_test_topmost(PGWE_CONTEXT context, GWE_POINT point) {
    PGWE_WINDOW window;

    if (!context) {
        return NULL;
    }

    for (window = context->zTop; window; window = window->zBelow) {
        if ((window->flags & GWE_WINDOW_VISIBLE) && gwe_rect_contains_point(window->windowRect, point)) {
            return window;
        }
    }
    return NULL;
}

size_t
gwe_count_windows(PCGWE_CONTEXT context) {
    size_t count = 0u;
    PCGWE_WINDOW window;

    if (!context) {
        return 0u;
    }

    for (window = context->windowHead; window; window = window->nextWindow) {
        count += 1u;
    }
    return count;
}

bool
gwe_thread_owns_windows(PCGWE_CONTEXT context, uint32_t threadId) {
    PCGWE_WINDOW window;

    if (!context || !threadId) {
        return false;
    }

    for (window = context->windowHead; window; window = window->nextWindow) {
        if (window->ownerTid == threadId) {
            return true;
        }
    }
    return false;
}

void
gwe_attach_child(PGWE_WINDOW parent, PGWE_WINDOW child) {
    if (!parent || !child) {
        return;
    }
    child->nextSibling = parent->firstChild;
    parent->firstChild = child;
    child->parent = parent;
}

void
gwe_detach_child(PGWE_WINDOW parent, PGWE_WINDOW child) {
    PGWE_WINDOW *link;

    if (!parent || !child) {
        return;
    }

    for (link = &parent->firstChild; *link; link = &(*link)->nextSibling) {
        if (*link == child) {
            *link = child->nextSibling;
            child->nextSibling = NULL;
            child->parent = NULL;
            return;
        }
    }
}

void
gwe_insert_z_top(PGWE_CONTEXT context, PGWE_WINDOW window) {
    if (!context || !window) {
        return;
    }

    window->zAbove = NULL;
    window->zBelow = context->zTop;
    if (context->zTop) {
        context->zTop->zAbove = window;
    } else {
        context->zBottom = window;
    }
    context->zTop = window;
}

void
gwe_remove_z(PGWE_CONTEXT context, PGWE_WINDOW window) {
    if (!context || !window) {
        return;
    }

    if (window->zAbove) {
        window->zAbove->zBelow = window->zBelow;
    } else if (context->zTop == window) {
        context->zTop = window->zBelow;
    }

    if (window->zBelow) {
        window->zBelow->zAbove = window->zAbove;
    } else if (context->zBottom == window) {
        context->zBottom = window->zAbove;
    }

    window->zAbove = NULL;
    window->zBelow = NULL;
}

GWE_RESULT
gwe_init(PCGWE_KERNELAPI kernelApi, const GWE_INITINFO *initInfo, PGWE_CONTEXT *outContext) {
    PGWE_CONTEXT context;
    PGWE_WINDOW root;
    GWE_INITINFO effectiveInit;

    if (!outContext) {
        return GWE_E_INVALID_ARG;
    }

    effectiveInit.desktopWidth = initInfo ? initInfo->desktopWidth : 800;
    effectiveInit.desktopHeight = initInfo ? initInfo->desktopHeight : 600;
    effectiveInit.compositorEnabled = initInfo ? initInfo->compositorEnabled : true;
    if (effectiveInit.desktopWidth <= 0 || effectiveInit.desktopHeight <= 0) {
        return GWE_E_INVALID_ARG;
    }

    context = (PGWE_CONTEXT)calloc(1u, sizeof(*context));
    if (!context) {
        return GWE_E_OOM;
    }

    if (kernelApi) {
        context->api = *kernelApi;
        context->hasCustomAlloc = (kernelApi->heapAlloc != NULL && kernelApi->heapFree != NULL);
    }

    context->desktopWidth = effectiveInit.desktopWidth;
    context->desktopHeight = effectiveInit.desktopHeight;
    context->compositorEnabled = effectiveInit.compositorEnabled;
    context->nextWindowSerial = 1u;

    context->desktopSurface = gwe_create_surface(context, context->desktopWidth, context->desktopHeight);
    if (!context->desktopSurface) {
        free(context);
        return GWE_E_OOM;
    }

    root = (PGWE_WINDOW)gwe_alloc(context, sizeof(*root));
    if (!root) {
        gwe_destroy_surface(context, context->desktopSurface);
        free(context);
        return GWE_E_OOM;
    }

    root->serial = context->nextWindowSerial++;
    gwe_copy_name(root->name, "Desktop");
    root->flags = GWE_WINDOW_VISIBLE | GWE_WINDOW_ENABLED | GWE_WINDOW_TOPLEVEL;
    root->windowRect.x = 0;
    root->windowRect.y = 0;
    root->windowRect.w = context->desktopWidth;
    root->windowRect.h = context->desktopHeight;
    root->clientRect = root->windowRect;
    context->desktopRoot = root;
    gwe_region_clear(&context->desktopDamage);

    *outContext = context;
    return GWE_OK;
}

GWE_RESULT
gwe_shutdown(PGWE_CONTEXT context) {
    if (!context) {
        return GWE_E_INVALID_ARG;
    }

    gwe_destroy_all_queues(context);
    gwe_destroy_all_windows(context);
    gwe_free(context, context->desktopRoot);
    gwe_destroy_surface(context, context->desktopSurface);
    free(context);
    return GWE_OK;
}


GWE_RESULT
gwe_register_thread(PGWE_CONTEXT context, uint32_t threadId) {
    if (!context || !threadId) {
        return GWE_E_INVALID_ARG;
    }
    if (gwe_lookup_queue(context, threadId, false)) {
        return GWE_E_EXISTS;
    }
    if (!gwe_lookup_queue(context, threadId, true)) {
        return GWE_E_OOM;
    }
    return GWE_OK;
}

GWE_RESULT
gwe_unregister_thread(PGWE_CONTEXT context, uint32_t threadId) {
    PGWE_THREAD_QUEUE *link;

    if (!context || !threadId) {
        return GWE_E_INVALID_ARG;
    }
    if (gwe_thread_owns_windows(context, threadId)) {
        return GWE_E_STATE;
    }

    for (link = &context->queueHead; *link; link = &(*link)->nextQueue) {
        if ((*link)->threadId == threadId) {
            PGWE_THREAD_QUEUE queue = *link;
            *link = queue->nextQueue;
            while (queue->head) {
                GWE_MESSAGENODE *node = queue->head;
                queue->head = node->next;
                gwe_free(context, node);
            }
            gwe_free(context, queue);
            return GWE_OK;
        }
    }
    return GWE_E_NOT_FOUND;
}

GWE_RESULT
gwe_query_thread_queue(PGWE_CONTEXT context, uint32_t threadId, GWE_THREADQUEUEINFO *outInfo) {
    PGWE_THREAD_QUEUE queue;

    if (!context || !threadId || !outInfo) {
        return GWE_E_INVALID_ARG;
    }

    queue = gwe_lookup_queue(context, threadId, false);
    if (!queue) {
        return GWE_E_NOT_FOUND;
    }

    outInfo->threadId = queue->threadId;
    outInfo->postedCount = queue->postedCount;
    outInfo->quitPosted = queue->quitPosted;
    return GWE_OK;
}

GWE_RESULT
gwe_query_desktop(PGWE_CONTEXT context, GWE_DESKTOPINFO *outInfo) {
    if (!context || !outInfo) {
        return GWE_E_INVALID_ARG;
    }

    outInfo->width = context->desktopWidth;
    outInfo->height = context->desktopHeight;
    outInfo->windowCount = gwe_count_windows(context);
    outInfo->focusWindow = context->focusWindow;
    outInfo->activeWindow = context->activeWindow;
    outInfo->captureWindow = context->captureWindow;
    outInfo->damage = context->desktopDamage;
    outInfo->presentCount = context->presentCount;
    outInfo->compositorEnabled = context->compositorEnabled;
    return GWE_OK;
}