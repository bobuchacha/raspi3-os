#include "user_runtime.h"
#include "app/window.h"
#include "app/gdi.h"

#include <stdarg.h>
#include <stdio.h>

#define GUISAMPLE_SELF_MESSAGE (WM_USER + 1UL)

static HWND g_guisample_main_window = 0UL;
static unsigned long g_guisample_tick_limit = 5UL;
static unsigned long g_guisample_timer_count = 0UL;
static RosGdiFont g_guisample_font;
static int g_guisample_font_ready = 0;

/*
 * Write one formatted sample log line.
 *
 * @param fmt Printf-style format string.
 * @return Nothing.
 */
static void guisample_log(const char* fmt, ...) {
    char buffer[192];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    writeLine(buffer);
}

/*
 * Compare one task-argument string against a literal.
 *
 * @param text Runtime task-argument text.
 * @param expected Literal to compare against.
 * @return Non-zero when the strings match.
 */
static int guisample_text_equals(const char* text, const char* expected) {
    unsigned long index = 0UL;

    if (text == expected) {
        return 1;
    }
    if (!text || !expected) {
        return 0;
    }

    while (text[index] != '\0' && expected[index] != '\0') {
        if (text[index] != expected[index]) {
            return 0;
        }
        ++index;
    }

    return text[index] == expected[index];
}

/*
 * Parse a simple `loop` task argument to disable the auto-quit timer limit.
 *
 * @return Nothing.
 */
static void guisample_parse_args(void) {
    char args[128] = { 0 };

    if (getTaskArgs(args, sizeof(args)) < 0) {
        return;
    }
    if (guisample_text_equals(args, "loop")) {
        g_guisample_tick_limit = 0UL;
    }
}

/*
 * Load the default raster GUI font once so the sample exercises the staged GDI
 * text path.
 *
 * @return Nothing.
 */
static void guisample_prepare_font(void) {
    long status;

    if (g_guisample_font_ready) {
        return;
    }

    status = GdiLoadFont(0, 16UL, &g_guisample_font);
    if (status >= 0L) {
        g_guisample_font_ready = 1;
        return;
    }

    guisample_log("guisample.exe: default font unavailable, using rectangle-only sample");
}

/*
 * Print one readable message trace line for the current WndProc dispatch.
 *
 * @param hwnd Target window handle.
 * @param message Message identifier.
 * @param wParam First payload word.
 * @param lParam Second payload word.
 * @return Nothing.
 */
static void guisample_trace_message(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    guisample_log(
        "guisample.exe: WndProc hwnd=%lu msg=%s(%lu) wParam=%lu lParam=%lu",
        (unsigned long)hwnd,
        WindowMessageName(message),
        message,
        wParam,
        lParam);
}

/*
 * Sample window procedure used to prove that local posts and GWES timer pulses
 * both flow through the same message loop.
 *
 * @param hwnd Target window handle.
 * @param message Message identifier.
 * @param wParam First payload word.
 * @param lParam Second payload word.
 * @return Always zero for the current sample.
 */
static LRESULT guisample_wndproc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    guisample_trace_message(hwnd, message, wParam, lParam);

    if (message == WM_CREATE) {
        RosGdiSurface surface;

        if (GdiGetWindowSurface(hwnd, &surface) == 0) {
            (void)GdiFillSurfaceRect(&surface, 20UL, 56UL, 220UL, 18UL, 0x003B77B5UL);
            (void)GdiFillSurfaceRect(&surface, 20UL, 80UL, 220UL, 72UL, 0x00131D2CUL);
            (void)GdiFillSurfaceRect(&surface, 28UL, 92UL, 84UL, 18UL, 0x00F97316UL);
            if (g_guisample_font_ready) {
                (void)GdiDrawTextSurface(&surface, &g_guisample_font, 28UL, 58UL, "GDI font sample", 0x00FFFFFFUL);
                (void)GdiDrawTextSurface(&surface, &g_guisample_font, 28UL, 96UL, "Loaded Tahoma raster .rtf or System UI fallback", 0x00E5EEF9UL);
            }
            (void)GdiInvalidateRect(hwnd, 20UL, 56UL, 220UL, 96UL);
        }
    }
    if (message == WM_TIMER) {
        unsigned long bar_x = 28UL + ((g_guisample_timer_count * 32UL) % 160UL);

        (void)GdiFillRect(hwnd, bar_x, 116UL, 56UL, 18UL, 0x0022C55EUL);
        ++g_guisample_timer_count;
        if (g_guisample_tick_limit != 0UL && g_guisample_timer_count >= g_guisample_tick_limit) {
            (void)PostQuitMessage(0xDEADBEEF);
        }
    }
    else if (message == WM_CLOSE) {
        (void)PostQuitMessage(0xDEADBEEF);
    }

    return 0;
}

int AppMain(void) {
    MSG message;
    long get_result;

    guisample_parse_args();
    guisample_prepare_font();
    if (CreateWindowClass("guisample.main", guisample_wndproc) < 0) {
        writeLine("guisample.exe: failed to register window class");
        return 1;
    }

    g_guisample_main_window = CreateWindow("guisample.main", "guisample");
    if (g_guisample_main_window == 0UL) {
        writeLine("guisample.exe: failed to create window");
        return 1;
    }

    guisample_log("guisample.exe: created hwnd=%lu", (unsigned long)g_guisample_main_window);
    (void)PostMessage(g_guisample_main_window, GUISAMPLE_SELF_MESSAGE, 123UL, 456UL);

    while (GetMessage(&message)) {
        (void)TranslateMessage(&message);
        (void)DispatchMessage(&message);
    }

    writeLine("guisample.exe: message loop exited");
    if (g_guisample_font_ready) {
        (void)GdiUnloadFont(&g_guisample_font);
        g_guisample_font_ready = 0;
    }
    return 0xBEEFDEAD;
}