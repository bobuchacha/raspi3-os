#pragma once
#include "surface.h"
#include "rect.h"

#define ROS_WINDOW_SERVER_ONLY 1
#include "app/window.h"

#ifdef __cplusplus

enum class WindowFlags : U32 {
    None = 0,
    Visible = 1 << 0,
    Opaque = 1 << 1,
    Decorated = 1 << 2,
    Child = 1 << 3
};

inline WindowFlags operator|(WindowFlags a, WindowFlags b) {
    return static_cast<WindowFlags>(
        static_cast<U32>(a) |
        static_cast<U32>(b));
}

inline WindowFlags& operator|=(WindowFlags& lhs, WindowFlags rhs) {
    lhs = lhs | rhs;
    return lhs;
}

inline bool has_window_flag(WindowFlags flags, WindowFlags test) {
    return (static_cast<U32>(flags) & static_cast<U32>(test)) != 0U;
}

#else

typedef U32 WindowFlags;

#define WINDOW_FLAG_NONE 0U
#define WINDOW_FLAG_VISIBLE (1U << 0)
#define WINDOW_FLAG_OPAQUE (1U << 1)
#define WINDOW_FLAG_DECORATED (1U << 2)
#define WINDOW_FLAG_CHILD (1U << 3)

static inline int has_window_flag(WindowFlags flags, WindowFlags test) {
    return (flags & test) != 0U;
}

#endif

struct Window {
    int id;
    Rect frame;               // Outer frame in desktop coordinates.
    Rect surface_frame;       // Client surface position in desktop coordinates.
    Surface surface;          // Shared client backing surface.
    WindowFlags flags;
    unsigned long parent_id;
    U32 client_offset_x;
    U32 client_offset_y;
    char class_name[ROS_WINDOW_CLASS_NAME_MAX];
    char title[ROS_WINDOW_TITLE_MAX];
};