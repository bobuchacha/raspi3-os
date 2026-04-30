#pragma once
#include "surface.h"
#include "rect.h"

#define ROS_WINDOW_CLASS_NAME_MAX 32UL
#define ROS_WINDOW_TITLE_MAX 64UL
#define ROS_WINDOW_CURSOR_PATH_MAX 32UL

#define ROS_WINDOW_CURSOR_DEFAULT_PATH "C:\\cursors\\arrow.cur32"
#define ROS_WINDOW_CURSOR_CROSSHAIR_PATH "C:\\cursors\\crosshair.cur32"
#define ROS_WINDOW_CURSOR_BUSY_PATH "C:\\cursors\\busy.cur32"

#define ROS_WINDOW_STYLE_VISIBLE (1UL << 0)
#define ROS_WINDOW_STYLE_CHILD (1UL << 1)
#define ROS_WINDOW_STYLE_DECORATED (1UL << 2)
#define ROS_WINDOW_STYLE_DISABLED (1UL << 3)
#define ROS_WINDOW_STYLE_BORDER (1UL << 4)
#define ROS_WINDOW_STYLE_TOPMOST (1UL << 5)
#define ROS_WINDOW_STYLE_SYSTEM_UI (1UL << 6)
#define ROS_WINDOW_STYLE_FULLSCREEN (1UL << 7)

typedef unsigned long HWND;
typedef long LRESULT;
typedef LRESULT(*WNDPROC)(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam);

static inline unsigned long WindowPackSignedPair(long first, long second) {
    return (((unsigned long)(unsigned int)first) << 32) | (unsigned long)(unsigned int)second;
}

static inline void WindowUnpackSignedPair(unsigned long packed, long* first, long* second) {
    if (first != 0) {
        *first = (long)(int)((packed >> 32) & 0xFFFFFFFFUL);
    }
    if (second != 0) {
        *second = (long)(int)(packed & 0xFFFFFFFFUL);
    }
}

#ifdef __cplusplus

enum class WindowFlags : U32 {
    None = 0,
    Visible = 1 << 0,
    Opaque = 1 << 1,
    Decorated = 1 << 2,
    Child = 1 << 3,
    Topmost = 1 << 4,
    SystemUi = 1 << 5,
    Fullscreen = 1 << 6
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
#define WINDOW_FLAG_TOPMOST (1U << 4)
#define WINDOW_FLAG_SYSTEM_UI (1U << 5)
#define WINDOW_FLAG_FULLSCREEN (1U << 6)

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