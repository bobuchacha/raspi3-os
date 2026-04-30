/*
 * startmenu.cpp
 *
 * Owner-drawn popup Start menu used by the Explorer taskbar.
 */

#define ROS_APP_USE_EXPLORER_SHELL 1
#define ROS_WINDOW_NO_IMPORTS 1
#define ROS_GDI_NO_IMPORTS 1

#include "app/app.h"
#include "explorer.h"
#include <string.h>

#define EXPLORER_STARTMENU_CLASS "explorer.startmenu"
#define EXPLORER_STARTMENU_WIDTH 260UL
#define EXPLORER_STARTMENU_HEIGHT 246UL
#define EXPLORER_STARTMENU_HEADER_HEIGHT 50UL
#define EXPLORER_STARTMENU_FOOTER_HEIGHT 24UL
#define EXPLORER_STARTMENU_RAIL_WIDTH 54UL
#define EXPLORER_STARTMENU_ITEM_HEIGHT 32UL
#define EXPLORER_STARTMENU_ITEM_COUNT 5UL
#define EXPLORER_STARTMENU_FONT_PATH "C:\\fonts\\tahoma-14.rtf"

#define STARTMENU_COLOR_FRAME 0x00587EA6UL
#define STARTMENU_COLOR_PANEL_TOP 0x00F8FBFFUL
#define STARTMENU_COLOR_PANEL_BOTTOM 0x00DDEAF9UL
#define STARTMENU_COLOR_RAIL 0x005E89BBUL
#define STARTMENU_COLOR_RAIL_GLOW 0x007FA9D6UL
#define STARTMENU_COLOR_HEADER 0x00FFFFFFUL
#define STARTMENU_COLOR_ITEM_HOVER 0x00FFF6D7UL
#define STARTMENU_COLOR_ITEM_PRESSED 0x00F2E0A4UL
#define STARTMENU_COLOR_ITEM_TEXT 0x001B3450UL
#define STARTMENU_COLOR_ITEM_SUBTEXT 0x004A6889UL

typedef struct StartMenuItem {
    const char* label;
    const char* path;
} StartMenuItem;

typedef struct StartMenuRect {
    unsigned long x;
    unsigned long y;
    unsigned long width;
    unsigned long height;
} StartMenuRect;

typedef struct StartMenuState {
    HWND hwnd;
    RosGdiFont font;
    int font_ready;
    int class_ready;
    unsigned long hover_index;
    unsigned long pressed_index;
} StartMenuState;

static const StartMenuItem g_startmenu_items[EXPLORER_STARTMENU_ITEM_COUNT] = {
    { "Shell", "/bin/shell.exe" },
    { "Widget Demo", "/bin/widgetdemo.exe" },
    { "GUI Sample", "/bin/guisample.exe" },
    { "ImageBox Demo", "/bin/imageboxdemo.exe" },
    { "TrueType Demo", "/bin/ttfdemo.exe" },
};

static StartMenuState g_startmenu = { 0 };

/*
 * Load the popup font before the Start menu becomes visible.
 *
 * The Start menu window is created with the visible style already set, so the
 * first paint can race ahead of WM_CREATE-time font loading. Warming the font
 * here keeps the first open visually complete instead of depending on a later
 * hover or reopen to repaint the text.
 *
 * @return Nothing.
 */
static void startmenu_prepare_font(void) {
    if (g_startmenu.font_ready) {
        return;
    }

    if (ExplorerGdiLoadFont(EXPLORER_STARTMENU_FONT_PATH, 14UL, &g_startmenu.font) >= 0L
        || ExplorerGdiLoadFont(0, 14UL, &g_startmenu.font) >= 0L) {
        g_startmenu.font_ready = 1;
    }
}

/*
 * Return the smaller of two unsigned values.
 *
 * @param lhs Left-hand value.
 * @param rhs Right-hand value.
 * @return Smaller input value.
 */
static unsigned long startmenu_min_unsigned(unsigned long lhs, unsigned long rhs) {
    return lhs < rhs ? lhs : rhs;
}

/*
 * Populate one simple rectangle record.
 *
 * @param rect Rectangle to overwrite.
 * @param x New X coordinate.
 * @param y New Y coordinate.
 * @param width New width.
 * @param height New height.
 * @return Nothing.
 */
static void startmenu_set_rect(StartMenuRect* rect, unsigned long x, unsigned long y, unsigned long width, unsigned long height) {
    if (rect == NULL) {
        return;
    }

    rect->x = x;
    rect->y = y;
    rect->width = width;
    rect->height = height;
}

/*
 * Return whether one point lies inside one rectangle.
 *
 * @param rect Rectangle to test.
 * @param x Pointer X coordinate.
 * @param y Pointer Y coordinate.
 * @return Non-zero when the point lies inside the rectangle.
 */
static int startmenu_rect_contains(const StartMenuRect* rect, long x, long y) {
    if ((rect == NULL) || (rect->width == 0UL) || (rect->height == 0UL)) {
        return 0;
    }

    return x >= (long)rect->x
        && y >= (long)rect->y
        && x < (long)(rect->x + rect->width)
        && y < (long)(rect->y + rect->height);
}

/*
 * Return one normalized RGB surface color for the active pixel format.
 *
 * @param pixel_format Target surface pixel format.
 * @param color RGB color in `0x00RRGGBB` form.
 * @return Encoded surface pixel value.
 */
static unsigned long startmenu_encode_color(unsigned long pixel_format, unsigned long color) {
    if (pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
        return color;
    }

    return ((color & 0x000000FFUL) << 16)
        | (color & 0x0000FF00UL)
        | ((color & 0x00FF0000UL) >> 16);
}

/*
 * Fill one rectangle on the popup surface.
 *
 * @param surface Target popup surface.
 * @param rect Rectangle to fill.
 * @param color RGB fill color.
 * @return Nothing.
 */
static void startmenu_fill_rect(const RosGdiSurface* surface, const StartMenuRect* rect, unsigned long color) {
    if ((surface == NULL) || (rect == NULL) || (rect->width == 0UL) || (rect->height == 0UL)) {
        return;
    }

    (void)ExplorerGdiFillSurfaceRect(
        surface,
        rect->x,
        rect->y,
        rect->width,
        rect->height,
        startmenu_encode_color(surface->pixel_format, color));
}

/*
 * Draw a one-pixel frame around one popup rectangle.
 *
 * @param surface Target popup surface.
 * @param rect Rectangle to frame.
 * @param color RGB frame color.
 * @return Nothing.
 */
static void startmenu_frame_rect(const RosGdiSurface* surface, const StartMenuRect* rect, unsigned long color) {
    StartMenuRect edge;

    if ((rect == NULL) || (rect->width < 2UL) || (rect->height < 2UL)) {
        return;
    }

    startmenu_set_rect(&edge, rect->x, rect->y, rect->width, 1UL);
    startmenu_fill_rect(surface, &edge, color);
    startmenu_set_rect(&edge, rect->x, rect->y + rect->height - 1UL, rect->width, 1UL);
    startmenu_fill_rect(surface, &edge, color);
    startmenu_set_rect(&edge, rect->x, rect->y, 1UL, rect->height);
    startmenu_fill_rect(surface, &edge, color);
    startmenu_set_rect(&edge, rect->x + rect->width - 1UL, rect->y, 1UL, rect->height);
    startmenu_fill_rect(surface, &edge, color);
}

/*
 * Return the item rectangle for one launcher row.
 *
 * @param index Launcher item index.
 * @param rect_out Receives the item bounds.
 * @return Non-zero when the item rectangle is valid.
 */
static int startmenu_item_rect(unsigned long index, StartMenuRect* rect_out) {
    if ((rect_out == NULL) || (index >= EXPLORER_STARTMENU_ITEM_COUNT)) {
        return 0;
    }

    startmenu_set_rect(
        rect_out,
        EXPLORER_STARTMENU_RAIL_WIDTH + 8UL,
        EXPLORER_STARTMENU_HEADER_HEIGHT + 8UL + (index * EXPLORER_STARTMENU_ITEM_HEIGHT),
        EXPLORER_STARTMENU_WIDTH - EXPLORER_STARTMENU_RAIL_WIDTH - 16UL,
        EXPLORER_STARTMENU_ITEM_HEIGHT - 2UL);
    return 1;
}

/*
 * Return the hovered launcher index for one pointer position.
 *
 * @param x Pointer X coordinate.
 * @param y Pointer Y coordinate.
 * @return Item index, or `0xFFFFFFFF` when no item is under the pointer.
 */
static unsigned long startmenu_hit_test(long x, long y) {
    unsigned long index;

    for (index = 0UL; index < EXPLORER_STARTMENU_ITEM_COUNT; ++index) {
        StartMenuRect rect;

        if (startmenu_item_rect(index, &rect) && startmenu_rect_contains(&rect, x, y)) {
            return index;
        }
    }

    return 0xFFFFFFFFUL;
}

/*
 * Draw one text run when the popup font is available.
 *
 * @param surface Target popup surface.
 * @param x Text X coordinate.
 * @param y Text Y coordinate.
 * @param text Null-terminated text.
 * @param color RGB text color.
 * @return Nothing.
 */
static void startmenu_draw_text(const RosGdiSurface* surface, unsigned long x, unsigned long y, const char* text, unsigned long color) {
    if (!g_startmenu.font_ready || surface == NULL || text == NULL || text[0] == '\0') {
        return;
    }

    (void)ExplorerGdiDrawTextSurface(surface, &g_startmenu.font, x, y, text, color);
}

/*
 * Request a full popup repaint.
 *
 * @return Nothing.
 */
static void startmenu_invalidate(void) {
    if (g_startmenu.hwnd != 0UL) {
        (void)ExplorerGdiInvalidateRect(g_startmenu.hwnd, 0UL, 0UL, EXPLORER_STARTMENU_WIDTH, EXPLORER_STARTMENU_HEIGHT);
    }
}

/*
 * Notify the taskbar that the popup closed itself.
 *
 * The taskbar keeps the authoritative Start-button pressed/visible state, so
 * the popup reports its closure through one local message whenever it tears
 * itself down after a launcher click or an internal destroy path.
 *
 * @return Nothing.
 */
static void startmenu_notify_taskbar_closed(void) {
    RosExplorerShellSharedState* state = ExplorerShellSharedState(0UL);

    if ((state != NULL) && (state->taskbar_hwnd != 0ULL)) {
        (void)ExplorerWindowPostMessage((HWND)state->taskbar_hwnd, EXPLORER_WM_STARTMENU_CLOSED, 0UL, 0UL);
    }
}

/*
 * Update the shared shell snapshot for the popup visibility state.
 *
 * @param visible Non-zero when the popup is visible.
 * @return Nothing.
 */
static void startmenu_publish_shell_state(int visible) {
    RosExplorerShellSharedState* state = ExplorerShellSharedState(0UL);

    if ((state == NULL) || (state->version != ROS_EXPLORER_SHELL_SHARED_STATE_VERSION)) {
        return;
    }

    state->start_menu_visible = visible ? 1U : 0U;
    state->start_menu_hwnd = visible ? (uint64_t)g_startmenu.hwnd : 0ULL;
    ++state->shell_generation;
}

/*
 * Destroy the popup window when it is currently alive.
 *
 * @return Nothing.
 */
static void startmenu_destroy_window(void) {
    HWND hwnd = g_startmenu.hwnd;

    if (hwnd != 0UL) {
        g_startmenu.hwnd = 0UL;
        (void)ExplorerWindowDestroyWindow(hwnd);
    }
}

/*
 * Draw the owner-drawn popup chrome and launcher rows.
 *
 * @param hwnd Popup window handle.
 * @return Nothing.
 */
static void startmenu_do_paint(HWND hwnd) {
    RosGdiSurface surface;
    StartMenuRect whole_rect;
    StartMenuRect rail_rect;
    StartMenuRect header_rect;
    StartMenuRect footer_rect;
    unsigned long index;

    if (ExplorerGdiGetWindowSurface(hwnd, &surface) < 0L) {
        return;
    }

    startmenu_set_rect(&whole_rect, 0UL, 0UL, surface.width, surface.height);
    startmenu_set_rect(&rail_rect, 0UL, 0UL, startmenu_min_unsigned(EXPLORER_STARTMENU_RAIL_WIDTH, surface.width), surface.height);
    startmenu_set_rect(&header_rect, EXPLORER_STARTMENU_RAIL_WIDTH, 0UL, surface.width - EXPLORER_STARTMENU_RAIL_WIDTH, EXPLORER_STARTMENU_HEADER_HEIGHT);
    startmenu_set_rect(&footer_rect, EXPLORER_STARTMENU_RAIL_WIDTH, surface.height - EXPLORER_STARTMENU_FOOTER_HEIGHT, surface.width - EXPLORER_STARTMENU_RAIL_WIDTH, EXPLORER_STARTMENU_FOOTER_HEIGHT);

    startmenu_fill_rect(&surface, &whole_rect, STARTMENU_COLOR_PANEL_BOTTOM);
    startmenu_fill_rect(&surface, &rail_rect, STARTMENU_COLOR_RAIL);
    startmenu_fill_rect(&surface, &header_rect, STARTMENU_COLOR_HEADER);
    startmenu_fill_rect(&surface, &footer_rect, STARTMENU_COLOR_PANEL_TOP);
    startmenu_frame_rect(&surface, &whole_rect, STARTMENU_COLOR_FRAME);

    {
        StartMenuRect glow_rect;

        startmenu_set_rect(&glow_rect, 8UL, 12UL, EXPLORER_STARTMENU_RAIL_WIDTH - 16UL, surface.height > 24UL ? (surface.height - 24UL) : 0UL);
        startmenu_fill_rect(&surface, &glow_rect, STARTMENU_COLOR_RAIL_GLOW);
    }

    startmenu_draw_text(&surface, 12UL, 18UL, "Start", 0x00FFFFFFUL);
    startmenu_draw_text(&surface, EXPLORER_STARTMENU_RAIL_WIDTH + 14UL, 14UL, "Programs", STARTMENU_COLOR_ITEM_TEXT);
    startmenu_draw_text(&surface, EXPLORER_STARTMENU_RAIL_WIDTH + 14UL, surface.height - 18UL, "Custom OS shell", STARTMENU_COLOR_ITEM_SUBTEXT);

    for (index = 0UL; index < EXPLORER_STARTMENU_ITEM_COUNT; ++index) {
        StartMenuRect item_rect;

        if (!startmenu_item_rect(index, &item_rect)) {
            continue;
        }
        if (g_startmenu.pressed_index == index) {
            startmenu_fill_rect(&surface, &item_rect, STARTMENU_COLOR_ITEM_PRESSED);
        }
        else if (g_startmenu.hover_index == index) {
            startmenu_fill_rect(&surface, &item_rect, STARTMENU_COLOR_ITEM_HOVER);
        }

        startmenu_draw_text(&surface, item_rect.x + 12UL, item_rect.y + 7UL, g_startmenu_items[index].label, STARTMENU_COLOR_ITEM_TEXT);
    }

    (void)ExplorerGdiReleaseWindowSurface(hwnd);
}

/*
 * Handle popup window messages.
 *
 * @param hwnd Popup window handle.
 * @param message Window message identifier.
 * @param wParam First message payload word.
 * @param lParam Second message payload word.
 * @return Window-procedure result.
 */
static LRESULT startmenu_wndproc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    (void)wParam;

    if (message == WM_CREATE) {
        g_startmenu.hwnd = hwnd;
        startmenu_prepare_font();
        g_startmenu.hover_index = 0xFFFFFFFFUL;
        g_startmenu.pressed_index = 0xFFFFFFFFUL;
        startmenu_publish_shell_state(1);
        startmenu_invalidate();
        return 0L;
    }

    if (message == WM_MOUSEMOVE) {
        long pointer_x = 0L;
        long pointer_y = 0L;
        unsigned long hit_index;

        WindowUnpackSignedPair(lParam, &pointer_x, &pointer_y);
        hit_index = startmenu_hit_test(pointer_x, pointer_y);
        if (hit_index != g_startmenu.hover_index) {
            g_startmenu.hover_index = hit_index;
            startmenu_invalidate();
        }
        return 0L;
    }

    if (message == WM_LBUTTONDOWN) {
        long pointer_x = 0L;
        long pointer_y = 0L;

        WindowUnpackSignedPair(lParam, &pointer_x, &pointer_y);
        g_startmenu.pressed_index = startmenu_hit_test(pointer_x, pointer_y);
        startmenu_invalidate();
        return 0L;
    }

    if (message == WM_LBUTTONUP) {
        long pointer_x = 0L;
        long pointer_y = 0L;
        unsigned long hit_index;
        unsigned long pressed_index = g_startmenu.pressed_index;

        WindowUnpackSignedPair(lParam, &pointer_x, &pointer_y);
        hit_index = startmenu_hit_test(pointer_x, pointer_y);
        g_startmenu.pressed_index = 0xFFFFFFFFUL;
        startmenu_invalidate();
        if ((hit_index == pressed_index) && (hit_index < EXPLORER_STARTMENU_ITEM_COUNT)) {
            // Hide the popup before queueing the launch so Explorer visibly
            // returns to its normal shell state even when the child spends its
            // first few scheduler slices loading assets during startup.
            StartMenu_Hide();
            (void)LaunchProgram(g_startmenu_items[hit_index].path);
        }
        return 0L;
    }

    if (message == WM_PAINT || message == WM_REPAINT) {
        startmenu_do_paint(hwnd);
        return 0L;
    }

    if (message == WM_CLOSE || message == WM_DESTROY) {
        g_startmenu.hover_index = 0xFFFFFFFFUL;
        g_startmenu.pressed_index = 0xFFFFFFFFUL;
        g_startmenu.hwnd = 0UL;
        startmenu_publish_shell_state(0);
        startmenu_notify_taskbar_closed();
        return 0L;
    }

    return 0L;
}

/*
 * Ensure the popup class is registered before creating the menu window.
 *
 * @return Non-zero when the class is ready for use.
 */
static int startmenu_ensure_class(void) {
    if (g_startmenu.class_ready) {
        return 1;
    }

    if (ExplorerWindowCreateClass(EXPLORER_STARTMENU_CLASS, startmenu_wndproc) < 0L) {
        return 0;
    }

    g_startmenu.class_ready = 1;
    return 1;
}

/*
 * Show the popup Start menu anchored to the taskbar Start button.
 *
 * The taskbar passes `x` as the left edge of the Start button and `y` as the
 * top edge of the taskbar. The popup uses that anchor to appear directly above
 * the button like the classic shell menu instead of opening as a centered
 * generic dialog.
 *
 * @param x Screen-space Start button X coordinate.
 * @param y Screen-space taskbar top Y coordinate.
 * @return Nothing.
 */
extern "C" void StartMenu_Show(long x, long y) {
    WindowCreateParams params;
    long popup_x = x;
    long popup_y = y - (long)EXPLORER_STARTMENU_HEIGHT;

    if (!startmenu_ensure_class()) {
        return;
    }

    startmenu_prepare_font();

    if (g_startmenu.hwnd != 0UL) {
        startmenu_invalidate();
        return;
    }

    if (popup_x < 0L) {
        popup_x = 0L;
    }
    if (popup_y < 0L) {
        popup_y = 0L;
    }

    params.class_name = EXPLORER_STARTMENU_CLASS;
    params.title = "Explorer Start Menu";
    params.parent = 0UL;
    params.x = popup_x;
    params.y = popup_y;
    params.width = EXPLORER_STARTMENU_WIDTH;
    params.height = EXPLORER_STARTMENU_HEIGHT;
    params.style = ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_TOPMOST | ROS_WINDOW_STYLE_SYSTEM_UI | ROS_WINDOW_STYLE_BORDER;
    g_startmenu.hwnd = ExplorerWindowCreateWindowEx(&params);
}

/*
 * Hide the popup Start menu when it is currently open.
 *
 * @return Nothing.
 */
extern "C" void StartMenu_Hide(void) {
    startmenu_destroy_window();
}

/*
 * Return the active popup Start menu window handle.
 *
 * @return Live popup window handle, or zero when hidden.
 */
extern "C" HWND StartMenu_GetWindow(void) {
    return g_startmenu.hwnd;
}
