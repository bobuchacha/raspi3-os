/*
 * gwe_types.h
 *
 * Shared base types for the graphics/windowing/events module.
 */
#ifndef GWE_TYPES_H
#define GWE_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GWE_NAME_MAX 64u

typedef enum GweResultEnum {
    GWE_OK = 0,
    GWE_E_INVALID_ARG,
    GWE_E_STATE,
    GWE_E_OOM,
    GWE_E_NOT_FOUND,
    GWE_E_EXISTS,
    GWE_E_EMPTY,
    GWE_E_TIMEOUT,
    GWE_E_BUFFER_TOO_SMALL,
    GWE_E_NOT_IMPLEMENTED
} GWE_RESULT;

typedef enum GweMessageTypeEnum {
    GWE_MSG_NULL = 0,
    GWE_MSG_CREATE = 1,
    GWE_MSG_DESTROY,
    GWE_MSG_PAINT,
    GWE_MSG_POINTER_DOWN,
    GWE_MSG_POINTER_MOVE,
    GWE_MSG_POINTER_UP,
    GWE_MSG_KEY_DOWN,
    GWE_MSG_KEY_UP,
    GWE_MSG_QUIT,
    GWE_MSG_USER = 0x1000
} GWE_MESSAGE_TYPE;

typedef enum GwePointerKindEnum {
    GWE_POINTER_DOWN = 0,
    GWE_POINTER_MOVE,
    GWE_POINTER_UP
} GWE_POINTER_KIND;

typedef enum GweWindowFlagsEnum {
    GWE_WINDOW_VISIBLE = 1u << 0,
    GWE_WINDOW_ENABLED = 1u << 1,
    GWE_WINDOW_TOPLEVEL = 1u << 2,
    GWE_WINDOW_MODAL = 1u << 3,
    GWE_WINDOW_NEEDS_PAINT = 1u << 4,
    GWE_WINDOW_DESTROY_PENDING = 1u << 5
} GWE_WINDOW_FLAGS;

typedef struct GwePointStruct {
    int32_t x;
    int32_t y;
} GWE_POINT;

typedef struct GweRectStruct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} GWE_RECT;

typedef struct GweRegionStruct {
    bool valid;
    GWE_RECT bounds;
} GWE_REGION;

typedef struct GweContextStruct GWE_CONTEXT;
typedef struct GweWindowStruct GWE_WINDOW;
typedef struct GweSurfaceStruct GWE_SURFACE;
typedef struct GweThreadQueueStruct GWE_THREAD_QUEUE;

typedef GWE_CONTEXT *PGWE_CONTEXT;
typedef GWE_WINDOW *PGWE_WINDOW;
typedef GWE_SURFACE *PGWE_SURFACE;
typedef GWE_THREAD_QUEUE *PGWE_THREAD_QUEUE;

typedef const GWE_CONTEXT *PCGWE_CONTEXT;
typedef const GWE_WINDOW *PCGWE_WINDOW;
typedef const GWE_SURFACE *PCGWE_SURFACE;
typedef const GWE_THREAD_QUEUE *PCGWE_THREAD_QUEUE;

typedef struct GweMessageStruct {
    GWE_MESSAGE_TYPE type;
    PGWE_WINDOW targetWindow;
    uint64_t wparam;
    int64_t lparam;
    uint64_t timeMs;
    GWE_POINT screenPoint;
    GWE_POINT clientPoint;
    uintptr_t internalCookie;
} GWE_MESSAGE;

typedef GWE_MESSAGE *PGWE_MESSAGE;
typedef const GWE_MESSAGE *PCGWE_MESSAGE;

typedef int64_t (*GWE_WNDPROC)(PGWE_CONTEXT context,
                               PGWE_WINDOW window,
                               PCGWE_MESSAGE message,
                               void *userData);

typedef struct GweWindowCreateInfoStruct {
    uint32_t ownerTid;
    PGWE_WINDOW parent;
    GWE_RECT rect;
    uint32_t flags;
    uint32_t style;
    uint32_t exStyle;
    GWE_WNDPROC proc;
    void *userData;
    const char *name;
} GWE_WINDOW_CREATEINFO;

typedef struct GwePaintContextStruct {
    PGWE_WINDOW window;
    PGWE_SURFACE surface;
    GWE_REGION clip;
    uint32_t *pixels;
    size_t stride;
    bool active;
} GWE_PAINT_CONTEXT;

typedef GWE_PAINT_CONTEXT *PGWE_PAINT_CONTEXT;

typedef struct GweThreadQueueInfoStruct {
    uint32_t threadId;
    size_t postedCount;
    bool quitPosted;
} GWE_THREADQUEUEINFO;

typedef struct GweWindowInfoStruct {
    uint32_t serial;
    char name[GWE_NAME_MAX];
    uint32_t ownerTid;
    GWE_RECT windowRect;
    GWE_RECT clientRect;
    GWE_REGION invalidRegion;
    uint32_t flags;
    uint32_t style;
    uint32_t exStyle;
    uint64_t dispatchCount;
    uint64_t paintCount;
    bool hasSurface;
} GWE_WINDOWINFO;

typedef struct GweDesktopInfoStruct {
    int32_t width;
    int32_t height;
    size_t windowCount;
    PGWE_WINDOW focusWindow;
    PGWE_WINDOW activeWindow;
    PGWE_WINDOW captureWindow;
    GWE_REGION damage;
    uint64_t presentCount;
    bool compositorEnabled;
} GWE_DESKTOPINFO;

#ifdef __cplusplus
}
#endif

#endif /* GWE_TYPES_H */
