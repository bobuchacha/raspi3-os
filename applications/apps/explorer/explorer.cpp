/*
 * explorer.cpp
 *
 * Single-threaded controller for the rewritten explorer process.
 *
 * window.dll still keeps HWND registrations, queued messages, and reply
 * bookkeeping in one process-local state block without per-thread ownership.
 * Running one background controller loop beside the UI message pump therefore
 * lets two threads compete over the same window client state and can collapse
 * back into null dispatch targets. Keeping main() as the one effective UI
 * owner is the simplest reliable contract until window.dll grows real
 * multi-threaded ownership semantics.
 */

#define ROS_APP_USE_EXPLORER_SHELL 1
#define ROS_APP_USE_WINDOW 1
#define ROS_WINDOW_NO_IMPORTS 1

#include "explorer.h"
#include "app/app.h"
#include <stdio.h>
#include <string.h>

#define EXPLORER_PATH_MAX 192U
#define EXPLORER_LAUNCH_ERROR_TITLE "Launch Failed"

typedef long(*ExplorerCreateWindowClassFn)(const char* class_name, WNDPROC proc);
typedef HWND(*ExplorerCreateWindowExFn)(const WindowCreateParams* params);
typedef long(*ExplorerDestroyWindowFn)(HWND hwnd);
typedef long(*ExplorerPostMessageFn)(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam);
typedef long(*ExplorerGetMessageFn)(MSG* message);
typedef long(*ExplorerTranslateMessageFn)(const MSG* message);
typedef LRESULT(*ExplorerDispatchMessageFn)(const MSG* message);
typedef long(*ExplorerPostSetForegroundWindowFn)(HWND hwnd);
typedef unsigned long(*ExplorerSetTimerFn)(HWND hwnd, unsigned long timer_id, unsigned long interval_msec);
typedef HMENU(*ExplorerCreateMenuFn)(void);
typedef long(*ExplorerDestroyMenuFn)(HMENU menu);
typedef long(*ExplorerAppendMenuItemFn)(HMENU menu, unsigned long command_id, unsigned long flags, const char* text, unsigned long hotkey);
typedef long(*ExplorerAppendSubMenuFn)(HMENU menu, HMENU submenu, unsigned long flags, const char* text, unsigned long hotkey);
typedef long(*ExplorerAppendMenuSeparatorFn)(HMENU menu);
typedef long(*ExplorerTrackPopupMenuFn)(HMENU menu, unsigned long flags, long x, long y, HWND owner);
typedef long(*ExplorerMessageBoxErrorFn)(const char* title, const char* message);
typedef long(*ExplorerGdiGetWindowSurfaceFn)(HWND hwnd, RosGdiSurface* surface);
typedef long(*ExplorerGdiReleaseWindowSurfaceFn)(HWND hwnd);
typedef long(*ExplorerGdiInvalidateRectFn)(HWND hwnd, unsigned long x, unsigned long y, unsigned long width, unsigned long height);
typedef long(*ExplorerGdiFillSurfaceRectFn)(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color);
typedef long(*ExplorerGdiLoadFontFn)(const char* path, unsigned long pixel_height, RosGdiFont* font);
typedef long(*ExplorerGdiUnloadFontFn)(RosGdiFont* font);
typedef long(*ExplorerGdiMeasureTextFn)(const RosGdiFont* font, const char* text, unsigned long* width, unsigned long* height);
typedef long(*ExplorerGdiDrawTextSurfaceFn)(const RosGdiSurface* surface, const RosGdiFont* font, unsigned long x, unsigned long y, const char* text, unsigned long color);
typedef long(*ExplorerPngGetInfoFn)(const char* path, U32* width, U32* height);
typedef long(*ExplorerPngRenderFileToMemoryFn)(const char* path, RosPngMemory* memory);

typedef struct ExplorerWindowApi {
    HMODULE module;
    ExplorerCreateWindowClassFn create_window_class;
    ExplorerCreateWindowExFn create_window_ex;
    ExplorerDestroyWindowFn destroy_window;
    ExplorerPostMessageFn post_message;
    ExplorerGetMessageFn get_message;
    ExplorerTranslateMessageFn translate_message;
    ExplorerDispatchMessageFn dispatch_message;
    ExplorerPostSetForegroundWindowFn post_set_foreground_window;
    ExplorerSetTimerFn set_timer;
    ExplorerCreateMenuFn create_menu;
    ExplorerDestroyMenuFn destroy_menu;
    ExplorerAppendMenuItemFn append_menu_item;
    ExplorerAppendSubMenuFn append_submenu;
    ExplorerAppendMenuSeparatorFn append_menu_separator;
    ExplorerTrackPopupMenuFn track_popup_menu;
    ExplorerMessageBoxErrorFn message_box_error;
    int ready;
} ExplorerWindowApi;

typedef struct ExplorerGdiApi {
    HMODULE module;
    ExplorerGdiGetWindowSurfaceFn get_window_surface;
    ExplorerGdiReleaseWindowSurfaceFn release_window_surface;
    ExplorerGdiInvalidateRectFn invalidate_rect;
    ExplorerGdiFillSurfaceRectFn fill_surface_rect;
    ExplorerGdiLoadFontFn load_font;
    ExplorerGdiUnloadFontFn unload_font;
    ExplorerGdiMeasureTextFn measure_text;
    ExplorerGdiDrawTextSurfaceFn draw_text_surface;
    int ready;
} ExplorerGdiApi;

typedef struct ExplorerPngApi {
    HMODULE module;
    ExplorerPngGetInfoFn get_info;
    ExplorerPngRenderFileToMemoryFn render_file_to_memory;
    int ready;
} ExplorerPngApi;

static ExplorerWindowApi g_window_api;
static ExplorerGdiApi g_gdi_api;
static ExplorerPngApi g_png_api;
static void explorer_basename(const char* path, char* out, unsigned long cap);
static int explorer_bind_gdi_api(void);
static int explorer_bind_png_api(void);
static void explorer_default_launch_error_callback(const char* path, long pid, long status, const char* name, const char* args, void* context);

/*
 * Reset the explorer controller and dynamic API binding state.
 *
 * The loader may relaunch explorer after a shell crash, so the rewrite keeps
 * one explicit reset path instead of depending on zero-initialized globals to
 * describe the process lifetime contract. Resetting both structures together
 * avoids stale queue contents or half-bound function pointers surviving across
 * future refactors that call the lifecycle hooks more than once.
 *
 * @return Nothing.
 */
extern "C" void ExplorerInit(void) {
    memset(&g_window_api, 0, sizeof(g_window_api));
    memset(&g_gdi_api, 0, sizeof(g_gdi_api));
    memset(&g_png_api, 0, sizeof(g_png_api));
}

/*
 * Tear down explorer-owned process-local state.
 *
 * The current userspace runtime does not require an explicit `window.dll`
 * unload step, but clearing the cached binding state keeps the shutdown path
 * honest and prevents later maintenance from assuming explorer state outlives
 * the process lifecycle.
 *
 * @return Nothing.
 */
extern "C" void ExplorerShutdown(void) {
    memset(&g_window_api, 0, sizeof(g_window_api));
    memset(&g_gdi_api, 0, sizeof(g_gdi_api));
    memset(&g_png_api, 0, sizeof(g_png_api));
}

/*
 * Show one default explorer launch failure dialog.
 *
 * Explorer menus launch through the async runtime path, so failures otherwise
 * look like a no-op to the user. The default error callback keeps the message
 * terse but includes the failing path and final status so loader problems are
 * visible without requiring a serial log.
 *
 * @param path Null-terminated executable path that was queued.
 * @param pid Spawned process identifier, or a negative value on failure.
 * @param status Kernel launch status for the completed request.
 * @param name Visible process name supplied to the loader.
 * @param args Launch arguments supplied to the loader.
 * @param context Unused caller context.
 * @return Nothing.
 */
static void explorer_default_launch_error_callback(
    const char* path,
    long pid,
    long status,
    const char* name,
    const char* args,
    void* context) {
    char message[320];

    (void)name;
    (void)args;
    (void)context;

    snprintf(
        message,
        sizeof(message),
        "Explorer could not launch %s.\n\nstatus=%ld\npid=%ld",
        (path != NULL && path[0] != '\0') ? path : "<unknown program>",
        status,
        pid);
    if (ExplorerWindowMessageBoxError(EXPLORER_LAUNCH_ERROR_TITLE, message) <= 0L) {
        writeLog(message);
    }
}

/*
 * Queue one program launch from the explorer process without blocking the UI.
 *
 * Explorer still keeps all menu activation on the UI thread, but the loader may
 * take longer than one click handler should. Routing through the async runtime
 * helper keeps the shell responsive while the caller can still observe success
 * and failure distinctly.
 *
 * @param path Null-terminated executable path to spawn.
 * @return Non-zero on success, or zero when validation or queueing fails.
 */
extern "C" int LaunchProgramAsyncCallback(
    const char* path,
    ExplorerLaunchSuccessCallback success_callback,
    ExplorerLaunchErrorCallback error_callback,
    void* context) {
    char name[64];
    long request_id;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    explorer_basename(path, name, sizeof(name));
    request_id = spawnTaskAsyncCallbacks(
        path,
        name[0] ? name : path,
        "",
        success_callback,
        error_callback != NULL ? error_callback : explorer_default_launch_error_callback,
        context);
    if (request_id < 0L) {
        explorer_default_launch_error_callback(path, -1L, request_id, name[0] ? name : path, "", context);
        return 0;
    }

    return 1;
}

/*
 * Queue one program launch using Explorer's default failure UI.
 *
 * The simple menu code only cares whether the request was accepted locally, so
 * it delegates the eventual failure reporting to the default async error path.
 *
 * @param path Null-terminated executable path to spawn.
 * @return Non-zero on success, or zero when validation or queueing fails.
 */
extern "C" int LaunchProgram(const char* path) {
    return LaunchProgramAsyncCallback(path, NULL, NULL, NULL);
}

/*
 * Park the current thread forever.
 *
 * The current EL0 thread model does not provide a safe user-thread return
 * trampoline, so helper threads must not return from their entrypoint.
 * Parking keeps the process alive without executing past the entry function.
 *
 * @return Nothing.
 */
static void explorer_thread_park_forever(void) {
    for (;;) {
        (void)sleepMs(1000UL);
    }
}

/*
 * Resolve the subset of window.dll APIs used by the explorer rewrite.
 *
 * Static import synthesis for this fresh explorer rewrite currently produces a
 * bad client slot path, so explorer binds window.dll explicitly at runtime and
 * uses stable exported entry points from the DLL itself.
 *
 * @return Non-zero when every required API was resolved.
 */
static int explorer_bind_window_api(void) {
    if (g_window_api.ready) {
        return 1;
    }

    g_window_api.module = loadLibrary(ROS_WINDOW_CLIENT_MODULE_NAME);
    if (g_window_api.module == 0UL) {
        writeLog("explorer: failed to load window.dll");
        return 0;
    }

    g_window_api.create_window_class = (ExplorerCreateWindowClassFn)getProcAddress(g_window_api.module, "CreateWindowClass");
    g_window_api.create_window_ex = (ExplorerCreateWindowExFn)getProcAddress(g_window_api.module, "CreateWindowEx");
    g_window_api.destroy_window = (ExplorerDestroyWindowFn)getProcAddress(g_window_api.module, "DestroyWindow");
    g_window_api.post_message = (ExplorerPostMessageFn)getProcAddress(g_window_api.module, "PostMessage");
    g_window_api.get_message = (ExplorerGetMessageFn)getProcAddress(g_window_api.module, "GetMessage");
    g_window_api.translate_message = (ExplorerTranslateMessageFn)getProcAddress(g_window_api.module, "TranslateMessage");
    g_window_api.dispatch_message = (ExplorerDispatchMessageFn)getProcAddress(g_window_api.module, "DispatchMessage");
    g_window_api.post_set_foreground_window = (ExplorerPostSetForegroundWindowFn)getProcAddress(g_window_api.module, "PostSetForegroundWindow");
    g_window_api.set_timer = (ExplorerSetTimerFn)getProcAddress(g_window_api.module, "SetTimer");
    g_window_api.create_menu = (ExplorerCreateMenuFn)getProcAddress(g_window_api.module, "CreateMenu");
    g_window_api.destroy_menu = (ExplorerDestroyMenuFn)getProcAddress(g_window_api.module, "DestroyMenu");
    g_window_api.append_menu_item = (ExplorerAppendMenuItemFn)getProcAddress(g_window_api.module, "AppendMenuItem");
    g_window_api.append_submenu = (ExplorerAppendSubMenuFn)getProcAddress(g_window_api.module, "AppendSubMenu");
    g_window_api.append_menu_separator = (ExplorerAppendMenuSeparatorFn)getProcAddress(g_window_api.module, "AppendMenuSeparator");
    g_window_api.track_popup_menu = (ExplorerTrackPopupMenuFn)getProcAddress(g_window_api.module, "TrackPopupMenu");
    g_window_api.message_box_error = (ExplorerMessageBoxErrorFn)getProcAddress(g_window_api.module, "MessageBoxError");

    if (g_window_api.create_window_class == NULL
        || g_window_api.create_window_ex == NULL
        || g_window_api.destroy_window == NULL
        || g_window_api.post_message == NULL
        || g_window_api.get_message == NULL
        || g_window_api.translate_message == NULL
        || g_window_api.dispatch_message == NULL
        || g_window_api.post_set_foreground_window == NULL
        || g_window_api.set_timer == NULL
        || g_window_api.create_menu == NULL
        || g_window_api.destroy_menu == NULL
        || g_window_api.append_menu_item == NULL
        || g_window_api.append_submenu == NULL
        || g_window_api.append_menu_separator == NULL
        || g_window_api.track_popup_menu == NULL
        || g_window_api.message_box_error == NULL) {
        writeLog("explorer: failed to resolve required window.dll exports");
        return 0;
    }

    g_window_api.ready = 1;
    return 1;
}

/*
 * Resolve the subset of gdi.dll APIs used by the explorer rewrite.
 *
 * The fresh explorer rewrite already proved that one static client import path
 * can collapse into a null dispatch target at runtime. Binding the tiny GDI
 * surface subset explicitly keeps explorer on the same known-good loader path
 * as window.dll and avoids crashing the shell on the first child paint.
 *
 * @return Non-zero when every required API was resolved.
 */
static int explorer_bind_gdi_api(void) {
    if (g_gdi_api.ready) {
        return 1;
    }

    g_gdi_api.module = loadLibrary(ROS_GDI_CLIENT_MODULE_NAME);
    if (g_gdi_api.module == 0UL) {
        writeLog("explorer: failed to load gdi.dll");
        return 0;
    }

    g_gdi_api.get_window_surface = (ExplorerGdiGetWindowSurfaceFn)getProcAddress(g_gdi_api.module, "GdiGetWindowSurface");
    g_gdi_api.release_window_surface = (ExplorerGdiReleaseWindowSurfaceFn)getProcAddress(g_gdi_api.module, "GdiReleaseWindowSurface");
    g_gdi_api.invalidate_rect = (ExplorerGdiInvalidateRectFn)getProcAddress(g_gdi_api.module, "GdiInvalidateRect");
    g_gdi_api.fill_surface_rect = (ExplorerGdiFillSurfaceRectFn)getProcAddress(g_gdi_api.module, "GdiFillSurfaceRect");
    g_gdi_api.load_font = (ExplorerGdiLoadFontFn)getProcAddress(g_gdi_api.module, "GdiLoadFont");
    g_gdi_api.unload_font = (ExplorerGdiUnloadFontFn)getProcAddress(g_gdi_api.module, "GdiUnloadFont");
    g_gdi_api.measure_text = (ExplorerGdiMeasureTextFn)getProcAddress(g_gdi_api.module, "GdiMeasureText");
    g_gdi_api.draw_text_surface = (ExplorerGdiDrawTextSurfaceFn)getProcAddress(g_gdi_api.module, "GdiDrawTextSurface");

    if (g_gdi_api.get_window_surface == NULL
        || g_gdi_api.release_window_surface == NULL
        || g_gdi_api.invalidate_rect == NULL
        || g_gdi_api.fill_surface_rect == NULL
        || g_gdi_api.load_font == NULL
        || g_gdi_api.unload_font == NULL
        || g_gdi_api.measure_text == NULL
        || g_gdi_api.draw_text_surface == NULL) {
        writeLog("explorer: failed to resolve required gdi.dll exports");
        return 0;
    }

    g_gdi_api.ready = 1;
    return 1;
}

/*
 * Resolve the subset of png.dll APIs used by the explorer rewrite.
 *
 * The taskbar decodes one staged PNG icon into process-local scratch memory,
 * so explorer keeps PNG on the same explicit runtime-binding path as the other
 * shell-facing DLLs.
 *
 * @return Non-zero when every required API was resolved.
 */
static int explorer_bind_png_api(void) {
    if (g_png_api.ready) {
        return 1;
    }

    g_png_api.module = loadLibrary(ROS_PNG_CLIENT_MODULE_NAME);
    if (g_png_api.module == 0UL) {
        writeLog("explorer: failed to load png.dll");
        return 0;
    }

    g_png_api.get_info = (ExplorerPngGetInfoFn)getProcAddress(g_png_api.module, "PngGetInfo");
    g_png_api.render_file_to_memory = (ExplorerPngRenderFileToMemoryFn)getProcAddress(g_png_api.module, "PngRenderFileToMemory");
    if (g_png_api.get_info == NULL || g_png_api.render_file_to_memory == NULL) {
        writeLog("explorer: failed to resolve required png.dll exports");
        return 0;
    }

    g_png_api.ready = 1;
    return 1;
}

extern "C" long ExplorerWindowCreateClass(const char* class_name, WNDPROC proc) {
    if (!explorer_bind_window_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_window_api.create_window_class(class_name, proc);
}

extern "C" HWND ExplorerWindowCreateWindowEx(const WindowCreateParams* params) {
    if (!explorer_bind_window_api()) {
        return 0UL;
    }
    return g_window_api.create_window_ex(params);
}

extern "C" long ExplorerWindowDestroyWindow(HWND hwnd) {
    if (!explorer_bind_window_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_window_api.destroy_window(hwnd);
}

extern "C" long ExplorerWindowPostMessage(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    if (!explorer_bind_window_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_window_api.post_message(hwnd, message, wParam, lParam);
}

extern "C" long ExplorerWindowGetMessage(MSG* message) {
    if (!explorer_bind_window_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_window_api.get_message(message);
}

extern "C" long ExplorerWindowTranslateMessage(const MSG* message) {
    if (!explorer_bind_window_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_window_api.translate_message(message);
}

extern "C" LRESULT ExplorerWindowDispatchMessage(const MSG* message) {
    if (!explorer_bind_window_api()) {
        return 0L;
    }
    return g_window_api.dispatch_message(message);
}

extern "C" long ExplorerWindowPostSetForegroundWindow(HWND hwnd) {
    if (!explorer_bind_window_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_window_api.post_set_foreground_window(hwnd);
}

extern "C" unsigned long ExplorerWindowSetTimer(HWND hwnd, unsigned long timer_id, unsigned long interval_msec) {
    if (!explorer_bind_window_api()) {
        return 0UL;
    }
    return g_window_api.set_timer(hwnd, timer_id, interval_msec);
}

extern "C" HMENU ExplorerWindowCreateMenu(void) {
    if (!explorer_bind_window_api()) {
        return 0UL;
    }
    return g_window_api.create_menu();
}

extern "C" long ExplorerWindowDestroyMenu(HMENU menu) {
    if (!explorer_bind_window_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_window_api.destroy_menu(menu);
}

extern "C" long ExplorerWindowAppendMenuItem(HMENU menu, unsigned long command_id, unsigned long flags, const char* text, unsigned long hotkey) {
    if (!explorer_bind_window_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_window_api.append_menu_item(menu, command_id, flags, text, hotkey);
}

extern "C" long ExplorerWindowAppendSubMenu(HMENU menu, HMENU submenu, unsigned long flags, const char* text, unsigned long hotkey) {
    if (!explorer_bind_window_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_window_api.append_submenu(menu, submenu, flags, text, hotkey);
}

extern "C" long ExplorerWindowAppendMenuSeparator(HMENU menu) {
    if (!explorer_bind_window_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_window_api.append_menu_separator(menu);
}

extern "C" long ExplorerWindowTrackPopupMenu(HMENU menu, unsigned long flags, long x, long y, HWND owner) {
    if (!explorer_bind_window_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_window_api.track_popup_menu(menu, flags, x, y, owner);
}

extern "C" long ExplorerWindowMessageBoxError(const char* title, const char* message) {
    if (!explorer_bind_window_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_window_api.message_box_error(title, message);
}

extern "C" long ExplorerGdiGetWindowSurface(HWND hwnd, RosGdiSurface* surface) {
    if (!explorer_bind_gdi_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_gdi_api.get_window_surface(hwnd, surface);
}

extern "C" long ExplorerGdiReleaseWindowSurface(HWND hwnd) {
    if (!explorer_bind_gdi_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_gdi_api.release_window_surface(hwnd);
}

extern "C" long ExplorerGdiInvalidateRect(HWND hwnd, unsigned long x, unsigned long y, unsigned long width, unsigned long height) {
    if (!explorer_bind_gdi_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_gdi_api.invalidate_rect(hwnd, x, y, width, height);
}

extern "C" long ExplorerGdiFillSurfaceRect(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color) {
    if (!explorer_bind_gdi_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_gdi_api.fill_surface_rect(surface, x, y, width, height, color);
}

extern "C" long ExplorerGdiLoadFont(const char* path, unsigned long pixel_height, RosGdiFont* font) {
    if (!explorer_bind_gdi_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_gdi_api.load_font(path, pixel_height, font);
}

extern "C" long ExplorerGdiUnloadFont(RosGdiFont* font) {
    if (!explorer_bind_gdi_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_gdi_api.unload_font(font);
}

extern "C" long ExplorerGdiMeasureText(const RosGdiFont* font, const char* text, unsigned long* width, unsigned long* height) {
    if (!explorer_bind_gdi_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_gdi_api.measure_text(font, text, width, height);
}

extern "C" long ExplorerGdiDrawTextSurface(const RosGdiSurface* surface, const RosGdiFont* font, unsigned long x, unsigned long y, const char* text, unsigned long color) {
    if (!explorer_bind_gdi_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_gdi_api.draw_text_surface(surface, font, x, y, text, color);
}

extern "C" long ExplorerPngGetInfo(const char* path, U32* width, U32* height) {
    if (!explorer_bind_png_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_png_api.get_info(path, width, height);
}

extern "C" long ExplorerPngRenderFileToMemory(const char* path, RosPngMemory* memory) {
    if (!explorer_bind_png_api()) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    return g_png_api.render_file_to_memory(path, memory);
}

/*
 * Query display metrics for the explorer UI thread.
 *
 * The rewritten explorer avoids depending on gdi.dll for startup, but it still
 * needs the current framebuffer dimensions to size the desktop and taskbar
 * windows correctly.
 *
 * @param width_out Receives the display width.
 * @param height_out Receives the display height.
 * @return Nothing.
 */
static void explorer_query_display_size(unsigned long* width_out, unsigned long* height_out) {
    RosKernelGuiDisplayInfo info;
    long status;

    if (width_out == NULL || height_out == NULL) {
        return;
    }

    *width_out = 800U;
    *height_out = 600U;
    memset(&info, 0, sizeof(info));
    status = controlGui(ROS_KERNEL_GUI_CONTROL_DISPLAY_INFO, (unsigned long)&info);
    if (status >= 0L && info.version == ROS_KERNEL_GUI_DISPLAY_INFO_VERSION && info.width != 0U && info.height != 0U) {
        *width_out = info.width;
        *height_out = info.height;
    }
}

/*
 * Run the single explorer UI owner thread.
 *
 * window.dll currently expects one effective owner for class registration,
 * HWND bookkeeping, and the process-local message queue. Explorer therefore
 * creates both the desktop and taskbar windows on one UI thread even though
 * their implementation lives in separate source files.
 *
 * @param argument Unused thread argument.
 * @return Nothing.
 */
extern "C" void explorer_ui_thread_entry(unsigned long argument) {
    MSG message;
    long get_result;
    unsigned long width;
    unsigned long height;
    HWND desktop_hwnd;
    HWND taskbar_hwnd;

    (void)argument;
    (void)setCurrentThreadPriority(USER_THREAD_PRIORITY_ABOVE_NORMAL);
    if (!explorer_bind_window_api()) {
        writeLog("explorer.ui: window api bind failed");
        explorer_thread_park_forever();
    }
    explorer_query_display_size(&width, &height);

    if (ExplorerDesktopRegisterClass() < 0L) {
        writeLog("explorer.ui: desktop class registration failed");
        explorer_thread_park_forever();
    }
    if (ExplorerTaskbarRegisterClass() < 0L) {
        writeLog("explorer.ui: taskbar class registration failed");
        explorer_thread_park_forever();
    }

    desktop_hwnd = ExplorerDesktopCreateWindow(width, height);
    if (desktop_hwnd == 0UL) {
        writeLog("explorer.ui: desktop window creation failed");
        explorer_thread_park_forever();
    }

    taskbar_hwnd = ExplorerTaskbarCreateWindow(width, height);
    if (taskbar_hwnd == 0UL) {
        writeLog("explorer.ui: taskbar window creation failed");
        explorer_thread_park_forever();
    }

    writeLog("explorer.ui: ready");
    while ((get_result = ExplorerWindowGetMessage(&message)) > 0L) {
        (void)ExplorerWindowTranslateMessage(&message);
        (void)ExplorerWindowDispatchMessage(&message);
    }

    (void)get_result;
    explorer_thread_park_forever();
}

/* Helper: extract a short process name from a path (strip directories and .exe). */
static void explorer_basename(const char* path, char* out, unsigned long cap) {
    unsigned long i = 0UL;
    const char* p = path;
    const char* last = p;

    if (path == NULL || out == NULL || cap == 0UL) {
        if (out && cap) out[0] = '\0';
        return;
    }

    while (*p != '\0') {
        if (*p == '/') last = p + 1;
        ++p;
    }

    while (i + 1UL < cap && last[i] != '\0') {
        out[i] = last[i];
        ++i;
    }
    out[i] = '\0';

    /* trim a trailing ".exe" if present */
    if (i >= 4UL && strcmp(&out[i - 4UL], ".exe") == 0) {
        out[i - 4UL] = '\0';
    }
}

/*
 * explorer_main
 *
 * Run explorer on the process entry thread so window.dll sees one stable owner
 * for class registration, HWND bookkeeping, message retrieval, and dispatch.
 * This matches the repo's known-good shell model and prevents child-process or
 * helper-thread activity from corrupting the shared window client state.
 *
 * @return Zero when explorer unexpectedly returns from its UI loop.
 */
int main(void) {
    ExplorerInit();
    (void)setCurrentThreadPriority(USER_THREAD_PRIORITY_NORMAL);
    explorer_ui_thread_entry(0UL);
    ExplorerShutdown();
    return 0;
}
