/*
 * desktop.cpp
 *
 * Lightweight C-style `Desktop` class and its UI thread.
 */

#define ROS_APP_USE_EXPLORER_SHELL 1
#define ROS_APP_USE_WINDOW 1
#define ROS_WINDOW_NO_IMPORTS 1
#define ROS_GDI_NO_IMPORTS 1

#include "app/app.h"
#include "app/core_log.h"
#include "explorer.h"
#include <string.h>

#define EXPLORER_DESKTOP_FONT_PATH "C:\\fonts\\tahoma-14.rtf"
#define EXPLORER_DESKTOP_MENU_CMD_REFRESH 0x2101UL
#define EXPLORER_DESKTOP_MENU_CMD_SHELL 0x2102UL
#define EXPLORER_DESKTOP_MENU_CMD_WIDGETDEMO 0x2103UL
#define EXPLORER_DESKTOP_MENU_CMD_GUISAMPLE 0x2104UL
#define EXPLORER_DESKTOP_MENU_CMD_IMAGEBOX 0x2105UL
#define EXPLORER_DESKTOP_MENU_CMD_TTFDEMO 0x2106UL

typedef struct Desktop {
    HWND hwnd;
    unsigned long width;
    unsigned long height;
    RosGdiFont font;
    int font_ready;
    HMENU context_menu;
    HMENU demos_menu;
    int right_button_down;
} Desktop;

static Desktop g_desktop = { 0 };

/*
 * Load the desktop overlay font before the first visible frame.
 *
 * Explorer creates the desktop window with the visible style already set, so
 * warming the font here avoids one first-paint frame where the wallpaper is
 * present but the shell caption text is still missing.
 *
 * @return Nothing.
 */
static void desktop_prepare_font(void) {
    if (g_desktop.font_ready) {
        return;
    }

    if (ExplorerGdiLoadFont(EXPLORER_DESKTOP_FONT_PATH, 14UL, &g_desktop.font) >= 0L
        || ExplorerGdiLoadFont(0, 14UL, &g_desktop.font) >= 0L) {
        g_desktop.font_ready = 1;
    }
}

static void desktop_paint(HWND hwnd) {
    RosGdiSurface surface;
    if (ExplorerGdiGetWindowSurface(hwnd, &surface) < 0L) {
        debugError("explorer.desktop: GdiGetWindowSurface failed");
        return;
    }

    /*
     * Leave the desktop surface otherwise transparent so GWES's retained
     * wallpaper remains visible, then overlay the shell caption text.
     */
    if (g_desktop.font_ready) {
        (void)ExplorerGdiDrawTextSurface(&surface, &g_desktop.font, 20UL, 20UL, "raspi3 Explorer", 0x00F7FBFFUL);
        (void)ExplorerGdiDrawTextSurface(&surface, &g_desktop.font, 20UL, 40UL, "Right-click for demos and shell actions", 0x00D7E3F1UL);
    }

    if (ExplorerGdiReleaseWindowSurface(hwnd) < 0L) {
        debugError("explorer.desktop: GdiReleaseWindowSurface failed");
    }
}

/*
 * Request a full desktop repaint.
 *
 * The desktop currently paints a lightweight background, so a full invalidate
 * is the simplest refresh action to expose through the context menu.
 *
 * @return Nothing.
 */
static void desktop_invalidate_full(void) {
    if (g_desktop.hwnd == 0UL || g_desktop.width == 0UL || g_desktop.height == 0UL) {
        return;
    }

    (void)ExplorerGdiInvalidateRect(g_desktop.hwnd, 0UL, 0UL, g_desktop.width, g_desktop.height);
}

/*
 * Release any popup-menu models owned by the desktop window.
 *
 * The desktop reuses the same window.dll popup-menu API as the taskbar, so it
 * tears down both the root menu and nested demos submenu explicitly.
 *
 * @return Nothing.
 */
static void desktop_destroy_menus(void) {
    if (g_desktop.demos_menu != 0UL) {
        (void)ExplorerWindowDestroyMenu(g_desktop.demos_menu);
        g_desktop.demos_menu = 0UL;
    }
    if (g_desktop.context_menu != 0UL) {
        (void)ExplorerWindowDestroyMenu(g_desktop.context_menu);
        g_desktop.context_menu = 0UL;
    }
}

/*
 * Build the reusable desktop context menu tree.
 *
 * Keeping the menu cached on the desktop window avoids per-click allocations
 * and mirrors the already validated taskbar popup-menu pattern.
 *
 * @return Non-zero when the desktop menu is ready.
 */
static int desktop_prepare_menus(void) {
    long status;

    if ((g_desktop.context_menu != 0UL) && (g_desktop.demos_menu != 0UL)) {
        return 1;
    }

    desktop_destroy_menus();
    g_desktop.context_menu = ExplorerWindowCreateMenu();
    g_desktop.demos_menu = ExplorerWindowCreateMenu();
    if ((g_desktop.context_menu == 0UL) || (g_desktop.demos_menu == 0UL)) {
        desktop_destroy_menus();
        return 0;
    }

    status = ExplorerWindowAppendMenuItem(g_desktop.context_menu, EXPLORER_DESKTOP_MENU_CMD_REFRESH, 0UL, "Refresh", 'R');
    if (status < 0L) {
        desktop_destroy_menus();
        return 0;
    }
    status = ExplorerWindowAppendMenuItem(g_desktop.context_menu, EXPLORER_DESKTOP_MENU_CMD_SHELL, 0UL, "Shell", 'S');
    if (status < 0L) {
        desktop_destroy_menus();
        return 0;
    }
    status = ExplorerWindowAppendMenuItem(g_desktop.demos_menu, EXPLORER_DESKTOP_MENU_CMD_WIDGETDEMO, 0UL, "Widget Demo", 'W');
    if (status < 0L) {
        desktop_destroy_menus();
        return 0;
    }
    status = ExplorerWindowAppendMenuItem(g_desktop.demos_menu, EXPLORER_DESKTOP_MENU_CMD_GUISAMPLE, 0UL, "GUI Sample", 'G');
    if (status < 0L) {
        desktop_destroy_menus();
        return 0;
    }
    status = ExplorerWindowAppendMenuItem(g_desktop.demos_menu, EXPLORER_DESKTOP_MENU_CMD_IMAGEBOX, 0UL, "ImageBox Demo", 'I');
    if (status < 0L) {
        desktop_destroy_menus();
        return 0;
    }
    status = ExplorerWindowAppendMenuItem(g_desktop.demos_menu, EXPLORER_DESKTOP_MENU_CMD_TTFDEMO, 0UL, "TrueType Demo", 'T');
    if (status < 0L) {
        desktop_destroy_menus();
        return 0;
    }
    status = ExplorerWindowAppendSubMenu(g_desktop.context_menu, g_desktop.demos_menu, 0UL, "Demos", 'D');
    if (status < 0L) {
        desktop_destroy_menus();
        return 0;
    }

    return 1;
}

/*
 * Track the desktop popup menu at one desktop-space pointer location.
 *
 * The desktop window already lives in desktop coordinates, so the pointer
 * payload can be passed straight through to the popup-menu tracker.
 *
 * @param x Desktop-relative X coordinate.
 * @param y Desktop-relative Y coordinate.
 * @return Nothing.
 */
static void desktop_show_context_menu(long x, long y) {
    if (!desktop_prepare_menus()) {
        return;
    }

    (void)ExplorerWindowTrackPopupMenu(g_desktop.context_menu, 0UL, x, y, g_desktop.hwnd);
}

/*
 * Execute one command selected from the desktop context menu.
 *
 * The desktop owns the popup menu so the normal `WM_COMMAND` path can launch
 * apps or refresh the desktop without any shell-specific side channel.
 *
 * @param command_id Selected menu command identifier.
 * @return Nothing.
 */
static void desktop_handle_menu_command(unsigned long command_id) {
    if (command_id == EXPLORER_DESKTOP_MENU_CMD_REFRESH) {
        desktop_invalidate_full();
        return;
    }
    if (command_id == EXPLORER_DESKTOP_MENU_CMD_SHELL) {
        (void)LaunchProgram("/bin/shell.exe");
        return;
    }
    if (command_id == EXPLORER_DESKTOP_MENU_CMD_WIDGETDEMO) {
        (void)LaunchProgram("/bin/widgetdemo.exe");
        return;
    }
    if (command_id == EXPLORER_DESKTOP_MENU_CMD_GUISAMPLE) {
        (void)LaunchProgram("/bin/guisample.exe");
        return;
    }
    if (command_id == EXPLORER_DESKTOP_MENU_CMD_IMAGEBOX) {
        (void)LaunchProgram("/bin/imageboxdemo.exe");
        return;
    }
    if (command_id == EXPLORER_DESKTOP_MENU_CMD_TTFDEMO) {
        (void)LaunchProgram("/bin/ttfdemo.exe");
    }
}


/*
 * Park the current desktop thread forever.
 *
 * Userspace helper threads must not return from their entrypoint because the
 * current runtime has no safe thread-return trampoline.
 *
 * @return Nothing.
 */
static void desktop_thread_park_forever(void) {
    for (;;) {
        (void)sleepMs(1000UL);
    }
}

static LRESULT desktop_wndproc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    if (message == WM_PAINT || message == WM_REPAINT) {
        /* minimal paint: nothing fancy, just ensure GWES knows we handled paint */
        desktop_paint(hwnd);
        return 0L;
    }

    if (message == WM_CREATE) {
        /* desktop can do initialization here if needed */
        g_desktop.hwnd = hwnd;
        g_desktop.right_button_down = 0;
        desktop_prepare_font();
        (void)desktop_prepare_menus();
        desktop_paint(hwnd);
        return 0L;
    }

    if (message == WM_COMMAND) {
        desktop_handle_menu_command(wParam);
        return 0L;
    }

    if (message == WM_RBUTTONDOWN) {
        g_desktop.right_button_down = 1;
        return 0L;
    }

    if (message == WM_RBUTTONUP) {
        long pointer_x = 0L;
        long pointer_y = 0L;

        WindowUnpackSignedPair(lParam, &pointer_x, &pointer_y);
        if (g_desktop.right_button_down) {
            desktop_show_context_menu(pointer_x, pointer_y);
        }
        g_desktop.right_button_down = 0;
        return 0L;
    }

    if (message == WM_CLOSE || message == WM_DESTROY) {
        /* desktop hides itself but does not terminate explorer */
        g_desktop.right_button_down = 0;
        desktop_destroy_menus();
        if (g_desktop.font_ready) {
            (void)ExplorerGdiUnloadFont(&g_desktop.font);
            g_desktop.font_ready = 0;
        }
        return 0L;
    }

    return 0L;
}

/*
 * Register the desktop window class with window.dll.
 *
 * The rewritten explorer keeps one UI owner thread for all window APIs, so
 * class registration is performed centrally but implemented inside the desktop
 * module to keep its state and procedure local.
 *
 * @return Zero or greater on success, negative status on failure.
 */
extern "C" long ExplorerDesktopRegisterClass(void) {
    return ExplorerWindowCreateClass("explorer.desktop", desktop_wndproc);
}

/*
 * Create the desktop window.
 *
 * The desktop stays on the single explorer UI owner thread so window.dll sees
 * one effective message-pump owner for all HWND state.
 *
 * @param width Requested desktop width.
 * @param height Requested desktop height.
 * @return Created window handle, or zero on failure.
 */
extern "C" HWND ExplorerDesktopCreateWindow(unsigned long width, unsigned long height) {
    WindowCreateParams params;

    desktop_prepare_font();

    params.class_name = "explorer.desktop";
    params.title = "Explorer Desktop";
    params.parent = 0UL;
    params.x = 0L;
    params.y = 0L;
    params.width = width;
    params.height = height;
    params.style = ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_SYSTEM_UI;

    g_desktop.width = width;
    g_desktop.height = height;
    g_desktop.hwnd = ExplorerWindowCreateWindowEx(&params);
    return g_desktop.hwnd;
}

/* Desktop UI thread entry */
extern "C" void explorer_desktop_thread_entry(unsigned long argument) {
    (void)argument;
    if (ExplorerWindowCreateClass("explorer.desktop", desktop_wndproc) < 0L) {
        writeLog("explorer.desktop: CreateWindowClass failed");
        desktop_thread_park_forever();
    }

    WindowCreateParams params;
    params.class_name = "explorer.desktop";
    params.title = "Explorer Desktop";
    params.parent = 0UL;
    params.x = 0L;
    params.y = 0L;
    params.width = 800U;
    params.height = 600U;
    params.style = ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_SYSTEM_UI;

    g_desktop.width = params.width;
    g_desktop.height = params.height;
    g_desktop.hwnd = ExplorerWindowCreateWindowEx(&params);
    if (g_desktop.hwnd == 0UL) {
        writeLog("explorer.desktop: CreateWindowEx failed");
        desktop_thread_park_forever();
    }

    MSG msg;
    long res;
    while ((res = ExplorerWindowGetMessage(&msg)) > 0L) {
        ExplorerWindowTranslateMessage(&msg);
        ExplorerWindowDispatchMessage(&msg);
    }

    (void)res;
    desktop_thread_park_forever();
}
