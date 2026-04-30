#define ROS_APP_USE_WINDOW 1
#define ROS_APP_USE_GDI 1
#include "app/app.h"

#include <stdarg.h>
#include <stdio.h>

#define TTFDEMO_WINDOW_CLASS "ttfdemo.main"
#define TTFDEMO_WINDOW_TITLE "ttfdemo"
#define TTFDEMO_TTF_PATH "C:\\fonts\\tahoma.ttf"
#define TTFDEMO_TTF_HEIGHT 24UL
#define TTFDEMO_FALLBACK_HEIGHT 20UL
#define TTFDEMO_WINDOW_X 128L
#define TTFDEMO_WINDOW_Y 96L
#define TTFDEMO_WINDOW_WIDTH 720UL
#define TTFDEMO_WINDOW_HEIGHT 320UL
#define TTFDEMO_WM_FONTS_READY (WM_USER + 1UL)

static HWND g_ttfdemo_main_window = 0UL;
static RosGdiFont g_ttfdemo_ttf_font;
static RosGdiFont g_ttfdemo_fallback_font;
static int g_ttfdemo_ttf_ready = 0;
static int g_ttfdemo_fallback_ready = 0;
static int g_ttfdemo_background_load_started = 0;
static int g_ttfdemo_background_load_running = 0;
static int g_ttfdemo_background_load_complete = 0;
static long g_ttfdemo_ttf_load_status = -1L;
static long g_ttfdemo_fallback_load_status = -1L;

static void ttfdemo_log(const char* fmt, ...) {
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
 * Notify the UI thread that font state changed and the window should repaint.
 *
 * The font loader now runs on a background helper thread so the demo no longer
 * monopolizes startup time before it reaches its message loop. Posting one
 * custom message keeps all painting on the owner thread.
 *
 * @return Nothing.
 */
static void ttfdemo_post_fonts_ready(void) {
    if (g_ttfdemo_main_window != 0UL) {
        (void)PostMessage(g_ttfdemo_main_window, TTFDEMO_WM_FONTS_READY, 0UL, 0UL);
    }
}

static void ttfdemo_prepare_fonts(void) {
    unsigned long width = 0UL;
    unsigned long height = 0UL;

    g_ttfdemo_ttf_load_status = GdiLoadFont(TTFDEMO_TTF_PATH, TTFDEMO_TTF_HEIGHT, &g_ttfdemo_ttf_font);
    if (g_ttfdemo_ttf_load_status >= 0L) {
        g_ttfdemo_ttf_ready = 1;
        if (GdiMeasureText(&g_ttfdemo_ttf_font, "TTF demo: The quick brown fox 1234567890", &width, &height) >= 0L) {
            ttfdemo_log("ttfdemo.exe: loaded %s metrics=%lux%lu", TTFDEMO_TTF_PATH, width, height);
        }
        else {
            ttfdemo_log("ttfdemo.exe: loaded %s", TTFDEMO_TTF_PATH);
        }
    }
    else {
        ttfdemo_log("ttfdemo.exe: GdiLoadFont failed path=%s status=%ld", TTFDEMO_TTF_PATH, g_ttfdemo_ttf_load_status);
    }

    g_ttfdemo_fallback_load_status = GdiLoadFont(0, TTFDEMO_FALLBACK_HEIGHT, &g_ttfdemo_fallback_font);
    if (g_ttfdemo_fallback_load_status >= 0L) {
        g_ttfdemo_fallback_ready = 1;
        if (GdiMeasureText(&g_ttfdemo_fallback_font, "Fallback sample text", &width, &height) >= 0L) {
            ttfdemo_log("ttfdemo.exe: loaded fallback font metrics=%lux%lu", width, height);
        }
        else {
            ttfdemo_log("ttfdemo.exe: loaded fallback font");
        }
    }
    else {
        ttfdemo_log("ttfdemo.exe: fallback font load failed status=%ld", g_ttfdemo_fallback_load_status);
    }

    ttfdemo_log("ttfdemo.exe: font preparation complete. Load status: ttf=%ld fallback=%ld", g_ttfdemo_ttf_load_status, g_ttfdemo_fallback_load_status);
}

/*
 * Complete one async font-load attempt and wake the UI thread.
 *
 * The actual font objects are created on the window.dll asset worker thread,
 * but the repaint still belongs on the window owner thread.
 *
 * @return Nothing.
 */
static void ttfdemo_finish_background_load(void) {
    g_ttfdemo_background_load_running = 0;
    g_ttfdemo_background_load_complete = 1;
    ttfdemo_post_fonts_ready();
}

/*
 * Load and prepare fonts from one window.dll asset callback.
 *
 * The current public GDI API still opens fonts by path, so the async asset
 * completion acts as the shared non-blocking scheduler and the callback then
 * performs the actual `GdiLoadFont` work on that same background worker.
 *
 * @param bytes Heap-backed staged asset bytes returned by window.dll.
 * @return Nothing.
 */
static void ttfdemo_prepare_fonts_from_callback(const void* bytes) {
    if (bytes != 0) {
        FreeAssetBuffer((void*)bytes);
    }

    ttfdemo_prepare_fonts();
    ttfdemo_finish_background_load();
}

/*
 * Handle one successful async staging of the demo TTF file.
 *
 * @param request_id Stable request identifier returned by `LoadFileAssetAsync`.
 * @param bytes Heap-backed file bytes returned by window.dll.
 * @param size Byte count stored in `bytes`.
 * @param path Source font path.
 * @param resource_name Unused embedded-resource name for file-backed loads.
 * @param context Unused callback context.
 * @return Nothing.
 */
static void ttfdemo_ttf_asset_success(
    unsigned long request_id,
    const void* bytes,
    unsigned long size,
    const char* path,
    const char* resource_name,
    void* context) {
    (void)request_id;
    (void)size;
    (void)path;
    (void)resource_name;
    (void)context;
    ttfdemo_prepare_fonts_from_callback(bytes);
}

/*
 * Handle one failed async staging attempt for the demo TTF file.
 *
 * The explicit TTF may be missing, but the fallback UI font should still load
 * off the UI thread so the demo keeps the same non-blocking startup behavior.
 *
 * @param request_id Stable request identifier returned by `LoadFileAssetAsync`.
 * @param status Negative failure status from the staging attempt.
 * @param path Source font path.
 * @param resource_name Unused embedded-resource name for file-backed loads.
 * @param context Unused callback context.
 * @return Nothing.
 */
static void ttfdemo_ttf_asset_error(
    unsigned long request_id,
    long status,
    const char* path,
    const char* resource_name,
    void* context) {
    (void)request_id;
    (void)resource_name;
    (void)context;
    if (status == ROS_WINDOW_ASSET_STATUS_TIMEOUT) {
        ttfdemo_log("ttfdemo.exe: async font stage timed out path=%s timeout=%lu", path ? path : "<null>", ROS_WINDOW_ASSET_TIMEOUT_MSEC);
    }
    ttfdemo_log("ttfdemo.exe: async font stage failed path=%s status=%ld", path ? path : "<null>", status);
    ttfdemo_prepare_fonts_from_callback(0);
}

/*
 * Start the non-blocking font staging path once.
 *
 * If the async request cannot even be queued, the demo falls back to the old
 * synchronous path so functionality still works on older images.
 *
 * @return Nothing.
 */
static void ttfdemo_start_background_loader(void) {
    long request_id;

    if (g_ttfdemo_background_load_started) {
        return;
    }

    g_ttfdemo_background_load_started = 1;
    g_ttfdemo_background_load_running = 1;
    request_id = LoadFileAssetAsync(
        TTFDEMO_TTF_PATH,
        ttfdemo_ttf_asset_success,
        ttfdemo_ttf_asset_error,
        0);
    if (request_id >= 0L) {
        ttfdemo_log("ttfdemo.exe: queued async font stage request=%ld", request_id);
        return;
    }

    ttfdemo_log("ttfdemo.exe: async font queue failed status=%ld", request_id);
    ttfdemo_prepare_fonts();
    ttfdemo_finish_background_load();
}

static void ttfdemo_draw_text_block(
    RosGdiSurface* surface,
    const RosGdiFont* font,
    unsigned long x,
    unsigned long y,
    unsigned long width,
    unsigned long height,
    unsigned long background_color,
    unsigned long frame_color,
    unsigned long foreground_color,
    const char* title,
    const char* body
) {
    if (!surface) {
        return;
    }

    (void)GdiFillSurfaceRect(surface, x, y, width, height, background_color);
    (void)GdiFillSurfaceRect(surface, x, y, width, 1UL, frame_color);
    (void)GdiFillSurfaceRect(surface, x, y + height - 1UL, width, 1UL, frame_color);
    (void)GdiFillSurfaceRect(surface, x, y, 1UL, height, frame_color);
    (void)GdiFillSurfaceRect(surface, x + width - 1UL, y, 1UL, height, frame_color);

    if (font) {
        long __r1 = GdiDrawTextSurface(surface, font, x + 18UL, y + 18UL, title, foreground_color);
        if (__r1 < 0L) {
            ttfdemo_log("ttfdemo.exe: GdiDrawTextSurface(title) failed status=%ld", __r1);
        }
        long __r2 = GdiDrawTextSurface(surface, font, x + 18UL, y + 58UL, body, foreground_color);
        if (__r2 < 0L) {
            ttfdemo_log("ttfdemo.exe: GdiDrawTextSurface(body) failed status=%ld", __r2);
        }
    }
}

static long ttfdemo_paint_window(HWND hwnd) {
    RosGdiSurface surface;
    char status_line[160];
    long status = GdiGetWindowSurface(hwnd, &surface);
    long release_status;

    if (status < 0L) {
        ttfdemo_log("ttfdemo.exe: GdiGetWindowSurface failed status=%ld", status);
        return status;
    }

    (void)GdiFillSurfaceRect(&surface, 0UL, 0UL, surface.width, surface.height, 0x00131B24UL);
    (void)GdiFillSurfaceRect(&surface, 0UL, 0UL, surface.width, 48UL, 0x001E293BUL);

    ttfdemo_log("ttfdemo.exe: painting hwnd=%lu surface=%lux%lu pitch=%lu", (unsigned long)hwnd, surface.width, surface.height, surface.pitch);
    ttfdemo_log("ttfdemo.exe: font load status ttf=%ld fallback=%ld", g_ttfdemo_ttf_load_status, g_ttfdemo_fallback_load_status);

    if (g_ttfdemo_ttf_ready) {
        ttfdemo_log("ttfdemo.exe: drawing TTF text block");
        ttfdemo_draw_text_block(
            &surface,
            &g_ttfdemo_ttf_font,
            24UL,
            72UL,
            surface.width > 48UL ? (surface.width - 48UL) : surface.width,
            104UL,
            0x0024334AUL,
            0x005B7FB2UL,
            0x00F8FAFCL,
            "Explicit TTF load",
            "Tahoma.ttf: The quick brown fox jumps over 13 lazy dogs.");
    }
    else if (g_ttfdemo_fallback_ready) {
        ttfdemo_log("ttfdemo.exe: drawing fallback text block due to TTF load failure");
        ttfdemo_draw_text_block(
            &surface,
            &g_ttfdemo_fallback_font,
            36UL,
            72UL,
            surface.width > 48UL ? (surface.width - 48UL) : surface.width,
            104UL,
            0x00FF00FFUL,
            0x00991B1BUL,
            0x00FDE8E8UL,
            "Explicit TTF load failed",
            "Fallback font rendered this message instead.");
    }

    if (g_ttfdemo_fallback_ready) {
        ttfdemo_log("ttfdemo.exe: drawing default GDI fallback text block");
        ttfdemo_draw_text_block(
            &surface,
            &g_ttfdemo_fallback_font,
            36UL,
            192UL,
            surface.width > 48UL ? (surface.width - 48UL) : surface.width,
            88UL,
            0x001D3524UL,
            0x004D8B5FUL,
            0x00E8FFF0UL,
            "Default GDI fallback",
            "This line verifies the non-TTF fallback path in the same window.");
    }

    snprintf(
        status_line,
        sizeof(status_line),
        "ttf=%ld fallback=%ld hwnd=%lu surface=%lux%lu pitch=%lu",
        g_ttfdemo_ttf_load_status,
        g_ttfdemo_fallback_load_status,
        (unsigned long)hwnd,
        surface.width,
        surface.height,
        surface.pitch);
    if (g_ttfdemo_fallback_ready) {
        ttfdemo_log("ttfdemo.exe: drawing status line");
        long __r = GdiDrawTextSurface(&surface, &g_ttfdemo_fallback_font, 24UL, 16UL, status_line, 0x00D6E7FFUL);
        if (__r < 0L) {
            ttfdemo_log("ttfdemo.exe: GdiDrawTextSurface(status_line) failed status=%ld", __r);
        }
    }

    status = GdiInvalidateRect(hwnd, 0UL, 0UL, surface.width, surface.height);
    if (status < 0L) {
        ttfdemo_log("ttfdemo.exe: GdiInvalidateRect failed status=%ld", status);
    }
    release_status = GdiReleaseWindowSurface(hwnd);
    if (release_status < 0L && status >= 0L) {
        ttfdemo_log("ttfdemo.exe: GdiReleaseWindowSurface failed status=%ld", release_status);
        status = release_status;
    }
    return status;
}

static LRESULT ttfdemo_wndproc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    (void)wParam;
    (void)lParam;

    if (message == WM_CREATE || message == WM_PAINT || message == WM_SIZE) {
        (void)ttfdemo_paint_window(hwnd);
        return 0L;
    }
    if (message == TTFDEMO_WM_FONTS_READY) {
        (void)ttfdemo_paint_window(hwnd);
        return 0L;
    }
    if (message == WM_CLOSE) {
        (void)PostQuitMessage(0L);
        return 0L;
    }

    return 0L;
}

int main(void) {
    MSG message;
    WindowCreateParams params;

    if (CreateWindowClass(TTFDEMO_WINDOW_CLASS, ttfdemo_wndproc) < 0L) {
        ttfdemo_log("ttfdemo.exe: failed to register class");
        return 1;
    }

    params.class_name = TTFDEMO_WINDOW_CLASS;
    params.title = TTFDEMO_WINDOW_TITLE;
    params.parent = 0UL;
    params.x = TTFDEMO_WINDOW_X;
    params.y = TTFDEMO_WINDOW_Y;
    params.width = TTFDEMO_WINDOW_WIDTH;
    params.height = TTFDEMO_WINDOW_HEIGHT;
    params.style = ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_DECORATED;

    g_ttfdemo_main_window = CreateWindowEx(&params);
    if (g_ttfdemo_main_window == 0UL) {
        ttfdemo_log("ttfdemo.exe: failed to create window");
        return 1;
    }

    ttfdemo_start_background_loader();
    ttfdemo_log("ttfdemo.exe: created hwnd=%lu", (unsigned long)g_ttfdemo_main_window);
    while (GetMessage(&message) > 0L) {
        (void)TranslateMessage(&message);
        (void)DispatchMessage(&message);
    }

    while (g_ttfdemo_background_load_running) {
        (void)sleepMs(1UL);
    }

    if (g_ttfdemo_ttf_ready) {
        (void)GdiUnloadFont(&g_ttfdemo_ttf_font);
        g_ttfdemo_ttf_ready = 0;
    }
    if (g_ttfdemo_fallback_ready) {
        (void)GdiUnloadFont(&g_ttfdemo_fallback_font);
        g_ttfdemo_fallback_ready = 0;
    }
    writeLine("ttfdemo.exe: message loop exited");
    return 0;
}
