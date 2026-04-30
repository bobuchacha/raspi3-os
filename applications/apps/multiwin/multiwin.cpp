#define ROS_APP_USE_WINDOW 1
#define ROS_APP_USE_WIDGETS 1
#include "app/app.h"

#include <stdarg.h>
#include <stdio.h>

#define MULTIWIN_WINDOW_CLASS "multiwin.window"
#define MULTIWIN_TAHOMA_18_FONT_PATH "C:\\fonts\\tahoma-12.rtf"
#define MULTIWIN_FONT_HEIGHT 12UL

#define MULTIWIN_HEADER_X 18UL
#define MULTIWIN_HEADER_Y 18UL
#define MULTIWIN_HEADER_HEIGHT 44UL
#define MULTIWIN_BUTTON_X 20L
#define MULTIWIN_BUTTON_Y 84L
#define MULTIWIN_BUTTON_WIDTH 116UL
#define MULTIWIN_BUTTON_HEIGHT 24UL
#define MULTIWIN_LABEL_X 20L
#define MULTIWIN_LABEL_Y 126L
#define MULTIWIN_LABEL_WIDTH 240UL
#define MULTIWIN_LABEL_HEIGHT 36UL
#define MULTIWIN_TIMER_MSEC 250UL

#define MULTIWIN_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

typedef struct MultiwinWindowState {
    const char* title;
    const char* style_name;
    const char* button_text;
    const char* cursor_path;
    long x;
    long y;
    unsigned long width;
    unsigned long height;
    unsigned long style;
    unsigned long accent_color;
    HWND hwnd;
    HWND button;
    HWND label;
    unsigned long timer_id;
    unsigned long timer_count;
} MultiwinWindowState;

static const char* g_multiwin_font_description = "Font: pending";

/*
 * Keep the demo layout declarative so the app reads like a window-style matrix
 * instead of a series of hand-written create calls.
 */
static MultiwinWindowState g_multiwin_windows[] = {
    {
        "multiwin decorated",
        "VISIBLE | DECORATED",
        "Decorated",
        ROS_WINDOW_CURSOR_DEFAULT_PATH,
        56L,
        56L,
        300UL,
        190UL,
        ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_DECORATED,
        0x004A89C7UL,
        0UL,
        0UL,
        0UL,
        0UL,
        0UL
    },
    {
        "multiwin plain",
        "VISIBLE",
        "Plain",
        ROS_WINDOW_CURSOR_CROSSHAIR_PATH,
        392L,
        92L,
        280UL,
        166UL,
        ROS_WINDOW_STYLE_VISIBLE,
        0x00C97A2BUL,
        0UL,
        0UL,
        0UL,
        0UL,
        0UL
    },
    {
        "multiwin decorated alt",
        "VISIBLE | DECORATED",
        "Decorated",
        ROS_WINDOW_CURSOR_BUSY_PATH,
        168L,
        306L,
        320UL,
        196UL,
        ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_DECORATED,
        0x00639C54UL,
        0UL,
        0UL,
        0UL,
        0UL,
        0UL
    }
};

/*
 * Emit one formatted debug line so font-selection and create failures remain
 * visible even when the GUI is not yet on screen.
 *
 * @param fmt Printf-style message template.
 * @return Nothing.
 */
static void multiwin_logf(const char* fmt, ...) {
    char buffer[192];
    va_list args;

    if (!fmt) {
        return;
    }

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    writeLine(buffer);
}

/*
 * Resolve one top-level demo record by the server-assigned `HWND`.
 *
 * @param hwnd Window handle delivered through the window message queue.
 * @return Matching demo state, or null when the handle does not belong to one
 * of the tracked top-level windows.
 */
static MultiwinWindowState* multiwin_find_window(HWND hwnd) {
    unsigned long index;

    for (index = 0UL; index < (unsigned long)MULTIWIN_ARRAY_COUNT(g_multiwin_windows); ++index) {
        if (g_multiwin_windows[index].hwnd == hwnd) {
            return &g_multiwin_windows[index];
        }
    }

    return 0;
}

/*
 * Load the requested Tahoma 18 raster font before any child widgets are
 * created. The widget toolkit exposes one process-wide text face, so this
 * guarantees the counter labels use the requested asset.
 *
 * @return Nothing.
 */
static void multiwin_prepare_font(void) {
    long status = WidgetSetFont(MULTIWIN_TAHOMA_18_FONT_PATH, MULTIWIN_FONT_HEIGHT);

    if (status >= 0L) {
        g_multiwin_font_description = "Font: Tahoma raster 18px";
        multiwin_logf("multiwin.exe: loaded %s", MULTIWIN_TAHOMA_18_FONT_PATH);
        return;
    }

    multiwin_logf("multiwin.exe: explicit Tahoma load failed status=%ld", status);
    status = WidgetSetFont(0, MULTIWIN_FONT_HEIGHT);
    if (status >= 0L) {
        g_multiwin_font_description = "Font: default fallback 18px";
        multiwin_logf("multiwin.exe: using default widget font policy");
        return;
    }

    g_multiwin_font_description = "Font: built-in fallback";
    multiwin_logf("multiwin.exe: widget font fallback failed status=%ld", status);
}

/*
 * Update the per-window counter label after every `WM_TIMER` pulse.
 *
 * @param state Target demo-window record.
 * @return Nothing.
 */
static void multiwin_refresh_label(MultiwinWindowState* state) {
    char line[96];

    if (!state || state->label == 0UL) {
        return;
    }

    snprintf(line, sizeof(line), "WM_TIMER count: %lu", state->timer_count);
    (void)WidgetSetText(state->label, line);
}

/*
 * Publish the configured cursor policy for one top-level demo window.
 *
 * Using different cursor assets here exercises the new GWES per-window cursor
 * path without depending on user interaction inside custom controls.
 *
 * @param state Window record whose cursor should be configured.
 * @return Nothing.
 */
static void multiwin_apply_cursor(MultiwinWindowState* state) {
    long status;

    if (!state || state->hwnd == 0UL) {
        return;
    }

    status = SetWindowCursor(state->hwnd, state->cursor_path);
    if (status < 0L) {
        multiwin_logf(
            "multiwin.exe: cursor apply failed hwnd=%lu path=%s status=%ld",
            (unsigned long)state->hwnd,
            state->cursor_path ? state->cursor_path : "<default>",
            status);
    }
}

/*
 * Paint one top-level demo window. The content area explains which style bits
 * were used and keeps the rest of the chrome intentionally simple.
 *
 * @param state Target demo-window record.
 * @return Zero on success, or a negative status code on failure.
 */
static long multiwin_paint_window(MultiwinWindowState* state) {
    RosWidgetPaintContext context;
    unsigned long header_width;
    char style_line[96];

    if (!state) {
        return -1L;
    }
    if (WidgetBeginPaint(state->hwnd, &context) < 0L) {
        return -1L;
    }

    (void)WidgetPaintDialogBackground(&context);

    header_width = context.surface.width > (MULTIWIN_HEADER_X * 2UL)
        ? (context.surface.width - (MULTIWIN_HEADER_X * 2UL))
        : context.surface.width;
    (void)WidgetFillVerticalGradient(
        &context,
        MULTIWIN_HEADER_X,
        MULTIWIN_HEADER_Y,
        header_width,
        MULTIWIN_HEADER_HEIGHT,
        0x00FFFFFFUL,
        state->accent_color);
    (void)WidgetFrameRect(
        &context,
        MULTIWIN_HEADER_X,
        MULTIWIN_HEADER_Y,
        header_width,
        MULTIWIN_HEADER_HEIGHT,
        0x00616F82UL);

    (void)WidgetDrawText(&context, 28UL, 28UL, state->title, 0x00203144UL);
    snprintf(style_line, sizeof(style_line), "Style: %s (0x%lx)", state->style_name, state->style);
    (void)WidgetDrawText(&context, 28UL, 46UL, style_line, 0x00344657UL);
    (void)WidgetDrawText(&context, 20UL, 154UL, g_multiwin_font_description, 0x00374858UL);
    (void)WidgetDrawText(&context, 20UL, 172UL, "Each top-level window receives its own WM_TIMER pulses.", 0x00374858UL);

    if ((state->style & ROS_WINDOW_STYLE_DECORATED) == 0UL) {
        (void)WidgetFrameRect(
            &context,
            8UL,
            8UL,
            context.surface.width > 16UL ? (context.surface.width - 16UL) : context.surface.width,
            context.surface.height > 16UL ? (context.surface.height - 16UL) : context.surface.height,
            state->accent_color);
    }

    return WidgetEndPaint(&context);
}

/*
 * Build the child controls for one top-level window once GWES delivers
 * `WM_CREATE`.
 *
 * @param state Target demo-window record.
 * @return Non-zero on success, zero when a required child window could not be
 * created.
 */
static int multiwin_create_children(MultiwinWindowState* state) {
    if (!state) {
        return 0;
    }

    state->button = WidgetCreateButton(state->hwnd, state->button_text, MULTIWIN_BUTTON_X, MULTIWIN_BUTTON_Y, MULTIWIN_BUTTON_WIDTH, MULTIWIN_BUTTON_HEIGHT);
    state->label = WidgetCreateLabel(state->hwnd, "WM_TIMER count: 0", MULTIWIN_LABEL_X, MULTIWIN_LABEL_Y, MULTIWIN_LABEL_WIDTH, MULTIWIN_LABEL_HEIGHT);
    return state->button != 0UL && state->label != 0UL;
}

/*
 * Route all top-level window messages for the multi-window demo.
 *
 * @param hwnd Target top-level window handle.
 * @param message Window message identifier.
 * @param wParam First payload word.
 * @param lParam Second payload word.
 * @return Always zero for this sample.
 */
static LRESULT multiwin_window_proc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    MultiwinWindowState* state = multiwin_find_window(hwnd);

    (void)wParam;
    (void)lParam;

    if (message == WM_CREATE) {
        if (!state) {
            multiwin_logf("multiwin.exe: create delivered for unknown hwnd=%lu", (unsigned long)hwnd);
            return 0L;
        }

        if (!multiwin_create_children(state)) {
            multiwin_logf("multiwin.exe: child creation failed hwnd=%lu", (unsigned long)hwnd);
            (void)PostQuitMessage(1L);
            return 0L;
        }

        multiwin_refresh_label(state);
    }
    else if (message == WM_PAINT) {
        if (state) {
            (void)multiwin_paint_window(state);
        }
    }
    else if (message == WM_TIMER) {
        if (state) {
            ++state->timer_count;
            multiwin_refresh_label(state);
            (void)WidgetInvalidate(hwnd);
        }
    }
    else if (message == WM_CLOSE) {
        multiwin_logf("multiwin.exe: close requested hwnd=%lu", (unsigned long)hwnd);
        (void)PostQuitMessage(0L);
    }

    return 0L;
}

/*
 * Create every top-level demo window declared in `g_multiwin_windows`.
 *
 * @return Zero on success, or one when any top-level create call fails.
 */
static int multiwin_create_demo_windows(void) {
    unsigned long index;

    for (index = 0UL; index < (unsigned long)MULTIWIN_ARRAY_COUNT(g_multiwin_windows); ++index) {
        MultiwinWindowState* state = &g_multiwin_windows[index];

        state->hwnd = WidgetCreateWindow(
            MULTIWIN_WINDOW_CLASS,
            state->title,
            0UL,
            state->x,
            state->y,
            state->width,
            state->height,
            state->style);
        if (state->hwnd == 0UL) {
            multiwin_logf("multiwin.exe: failed to create top-level window \"%s\"", state->title);
            return 1;
        }

        multiwin_logf(
            "multiwin.exe: created hwnd=%lu title=\"%s\" style=0x%lx",
            (unsigned long)state->hwnd,
            state->title,
            state->style);
        state->timer_id = SetTimer(state->hwnd, 0UL, MULTIWIN_TIMER_MSEC);
        if (state->timer_id == 0UL) {
            multiwin_logf("multiwin.exe: failed to register timer hwnd=%lu", (unsigned long)state->hwnd);
        }
        multiwin_apply_cursor(state);
    }

    return 0;
}

int main(void) {
    MSG message;
    long get_result;

    multiwin_prepare_font();
    if (WidgetRegisterClass(MULTIWIN_WINDOW_CLASS, ROS_WIDGET_KIND_DIALOG, multiwin_window_proc) < 0L) {
        writeLine("multiwin.exe: failed to register window class");
        return 1;
    }
    if (multiwin_create_demo_windows() != 0) {
        return 1;
    }

    for (;;) {
        get_result = GetMessage(&message);
        if (get_result <= 0L) {
            break;
        }
        (void)TranslateMessage(&message);
        (void)DispatchMessage(&message);
    }

    for (unsigned long index = 0UL; index < (unsigned long)MULTIWIN_ARRAY_COUNT(g_multiwin_windows); ++index) {
        MultiwinWindowState* state = &g_multiwin_windows[index];

        if (state->hwnd != 0UL && state->timer_id != 0UL) {
            (void)KillTimer(state->hwnd, state->timer_id);
            state->timer_id = 0UL;
        }
    }

    writeLine("multiwin.exe: message loop exited");
    return 0;
}
