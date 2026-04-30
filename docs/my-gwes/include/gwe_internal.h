/*
 * gwe_internal.h
 *
 * Private structures and helpers shared across implementation units.
 */
#ifndef GWE_INTERNAL_H
#define GWE_INTERNAL_H

#include "gwe_debug.h"
#include "gwe_graphics.h"
#include "gwe_input.h"
#include "gwe_message.h"
#include "gwe_module.h"
#include "gwe_window.h"

typedef struct GweMessageNodeStruct {
    GWE_MESSAGE message;
    struct GweMessageNodeStruct *next;
} GWE_MESSAGENODE;

struct GweSurfaceStruct {
    int32_t width;
    int32_t height;
    size_t stride;
    uint32_t *pixels;
};

struct GweThreadQueueStruct {
    uint32_t threadId;
    GWE_MESSAGENODE *head;
    GWE_MESSAGENODE *tail;
    size_t postedCount;
    bool quitPosted;
    struct GweThreadQueueStruct *nextQueue;
};

struct GweWindowStruct {
    uint32_t serial;
    char name[GWE_NAME_MAX];
    uint32_t ownerTid;
    GWE_RECT windowRect;
    GWE_RECT clientRect;
    GWE_REGION invalidRegion;
    PGWE_SURFACE surface;
    uint32_t flags;
    uint32_t style;
    uint32_t exStyle;
    GWE_WNDPROC proc;
    void *userData;
    uint64_t dispatchCount;
    uint64_t paintCount;
    struct GweWindowStruct *parent;
    struct GweWindowStruct *firstChild;
    struct GweWindowStruct *nextSibling;
    struct GweWindowStruct *nextWindow;
    struct GweWindowStruct *zAbove;
    struct GweWindowStruct *zBelow;
};

struct GweContextStruct {
    GWE_KERNELAPI api;
    bool hasCustomAlloc;
    int32_t desktopWidth;
    int32_t desktopHeight;
    bool compositorEnabled;
    PGWE_THREAD_QUEUE queueHead;
    PGWE_WINDOW windowHead;
    PGWE_WINDOW zTop;
    PGWE_WINDOW zBottom;
    PGWE_WINDOW desktopRoot;
    PGWE_WINDOW focusWindow;
    PGWE_WINDOW activeWindow;
    PGWE_WINDOW captureWindow;
    PGWE_SURFACE desktopSurface;
    GWE_REGION desktopDamage;
    uint64_t presentCount;
    uint32_t nextWindowSerial;
};

void *gwe_alloc(PGWE_CONTEXT context, size_t size);
void gwe_free(PGWE_CONTEXT context, void *memory);
void gwe_copy_name(char *destination, const char *source);
uint64_t gwe_now(PGWE_CONTEXT context);
void gwe_trace(PGWE_CONTEXT context, int level, const char *message);

void gwe_region_clear(GWE_REGION *region);
void gwe_region_set_rect(GWE_REGION *region, GWE_RECT rect);
void gwe_region_union_rect(GWE_REGION *region, GWE_RECT rect);
bool gwe_rect_contains_point(GWE_RECT rect, GWE_POINT point);
bool gwe_rect_intersect(GWE_RECT left, GWE_RECT right, GWE_RECT *outRect);
GWE_RECT gwe_rect_translate(GWE_RECT rect, int32_t dx, int32_t dy);

PGWE_THREAD_QUEUE gwe_lookup_queue(PGWE_CONTEXT context, uint32_t threadId, bool createIfMissing);
PGWE_WINDOW gwe_hit_test_topmost(PGWE_CONTEXT context, GWE_POINT point);
size_t gwe_count_windows(PCGWE_CONTEXT context);
bool gwe_thread_owns_windows(PCGWE_CONTEXT context, uint32_t threadId);

void gwe_attach_child(PGWE_WINDOW parent, PGWE_WINDOW child);
void gwe_detach_child(PGWE_WINDOW parent, PGWE_WINDOW child);
void gwe_insert_z_top(PGWE_CONTEXT context, PGWE_WINDOW window);
void gwe_remove_z(PGWE_CONTEXT context, PGWE_WINDOW window);

PGWE_SURFACE gwe_create_surface(PGWE_CONTEXT context, int32_t width, int32_t height);
void gwe_destroy_surface(PGWE_CONTEXT context, PGWE_SURFACE surface);

GWE_RESULT gwe_queue_message(PGWE_CONTEXT context, PGWE_THREAD_QUEUE queue, const GWE_MESSAGE *message);
int64_t gwe_call_window_proc(PGWE_CONTEXT context, PGWE_WINDOW window, PCGWE_MESSAGE message);
void gwe_drop_messages_for_window(PGWE_CONTEXT context, PGWE_WINDOW window);

#endif /* GWE_INTERNAL_H */
