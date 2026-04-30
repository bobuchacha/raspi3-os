/*
 * explorer.h
 *
 * Minimal public interface for the rewritten explorer process.
 */
#ifndef EXPLORER_HEADER_H
#define EXPLORER_HEADER_H

#include "app/app.h"

 /*
  * Explorer routes GDI through explicit runtime binding, so every explorer
  * translation unit needs the surface and font types without generating direct
  * import stubs from `app/gdi.h`.
  */
#if !defined(ROS_GDI_NO_IMPORTS)
#define ROS_GDI_NO_IMPORTS 1
#endif
#include "app/gdi.h"

  /*
     * Explorer also routes PNG decoding through explicit runtime binding so the
     * taskbar can stage icon assets without generating direct `png.dll` imports.
     */
#if !defined(ROS_PNG_NO_IMPORTS)
#define ROS_PNG_NO_IMPORTS 1
#endif
#include "app/png.h"

#define EXPLORER_WM_STARTMENU_CLOSED (WM_USER + 1UL)

#ifdef __cplusplus
extern "C" {
#endif

    typedef SpawnTaskAsyncSuccessCallback ExplorerLaunchSuccessCallback;
    typedef SpawnTaskAsyncErrorCallback ExplorerLaunchErrorCallback;

    /* Enqueue a program launch request. Returns non-zero on success. */
    int LaunchProgram(const char* path);

    /* Enqueue a program launch request with explicit success and failure callbacks. */
    int LaunchProgramAsyncCallback(
        const char* path,
        ExplorerLaunchSuccessCallback success_callback,
        ExplorerLaunchErrorCallback error_callback,
        void* context);

    /* Run the single explorer UI owner thread. */
    void explorer_ui_thread_entry(unsigned long argument);

    /* Dynamically bound window.dll wrappers used by the explorer rewrite. */
    long ExplorerWindowCreateClass(const char* class_name, WNDPROC proc);
    HWND ExplorerWindowCreateWindowEx(const WindowCreateParams* params);
    long ExplorerWindowDestroyWindow(HWND hwnd);
    long ExplorerWindowPostMessage(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam);
    long ExplorerWindowGetMessage(MSG* message);
    long ExplorerWindowTranslateMessage(const MSG* message);
    LRESULT ExplorerWindowDispatchMessage(const MSG* message);
    long ExplorerWindowPostSetForegroundWindow(HWND hwnd);
    unsigned long ExplorerWindowSetTimer(HWND hwnd, unsigned long timer_id, unsigned long interval_msec);
    HMENU ExplorerWindowCreateMenu(void);
    long ExplorerWindowDestroyMenu(HMENU menu);
    long ExplorerWindowAppendMenuItem(HMENU menu, unsigned long command_id, unsigned long flags, const char* text, unsigned long hotkey);
    long ExplorerWindowAppendSubMenu(HMENU menu, HMENU submenu, unsigned long flags, const char* text, unsigned long hotkey);
    long ExplorerWindowAppendMenuSeparator(HMENU menu);
    long ExplorerWindowTrackPopupMenu(HMENU menu, unsigned long flags, long x, long y, HWND owner);
    long ExplorerWindowMessageBoxError(const char* title, const char* message);

    /* Dynamically bound gdi.dll wrappers used by the explorer rewrite. */
    long ExplorerGdiGetWindowSurface(HWND hwnd, RosGdiSurface* surface);
    long ExplorerGdiReleaseWindowSurface(HWND hwnd);
    long ExplorerGdiInvalidateRect(HWND hwnd, unsigned long x, unsigned long y, unsigned long width, unsigned long height);
    long ExplorerGdiFillSurfaceRect(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color);
    long ExplorerGdiLoadFont(const char* path, unsigned long pixel_height, RosGdiFont* font);
    long ExplorerGdiUnloadFont(RosGdiFont* font);
    long ExplorerGdiMeasureText(const RosGdiFont* font, const char* text, unsigned long* width, unsigned long* height);
    long ExplorerGdiDrawTextSurface(const RosGdiSurface* surface, const RosGdiFont* font, unsigned long x, unsigned long y, const char* text, unsigned long color);

    /* Dynamically bound png.dll wrappers used by the explorer rewrite. */
    long ExplorerPngGetInfo(const char* path, U32* width, U32* height);
    long ExplorerPngRenderFileToMemory(const char* path, RosPngMemory* memory);

    /* Desktop class registration and window creation helpers. */
    long ExplorerDesktopRegisterClass(void);
    HWND ExplorerDesktopCreateWindow(unsigned long width, unsigned long height);

    /* Taskbar class registration and window creation helpers. */
    long ExplorerTaskbarRegisterClass(void);
    HWND ExplorerTaskbarCreateWindow(unsigned long width, unsigned long height);

    /* Start-menu helpers. */
    void StartMenu_Show(long x, long y);
    void StartMenu_Hide(void);
    HWND StartMenu_GetWindow(void);

    /* Optional lifecycle helpers. */
    void ExplorerInit(void);
    void ExplorerShutdown(void);

    typedef struct Rect {
        U32 x;
        U32 y;
        U32 width;
        U32 height;
    } Rect;

    /**
     * Taskbar UI state and metrics structures.
     */
    typedef struct TaskbarMetrics {
        unsigned long barThickness;
        unsigned long leftPadding;
        unsigned long rightPadding;
        unsigned long topPadding;
        unsigned long bottomPadding;
        unsigned long elementSpacing;
        unsigned long buttonWidth;
        unsigned long buttonMinWidth;
        unsigned long buttonHeight;
        unsigned long iconSize;
        unsigned long borderThickness;
    } TaskbarMetrics;

    /**
     * Taskbar state structure with all relevant metadata for layout and behavior decisions.
     */
    typedef struct TaskbarRects {
        Rect barRect;
        Rect clientRect;
        Rect startButtonRect;
        Rect doneButtonRect;
        Rect titleRect;
        Rect taskButtonsRect;
        Rect trayRect;
        Rect sipButtonRect;
        Rect notificationRect;
    } TaskbarRects;

    /**
     * Task item structure representing individual tasks in the taskbar.
     */
    typedef struct TaskItem {
        unsigned long windowId;
        const char* title;
        bool isActive;
        bool isRunning;
    } TaskItem;

    /**
    * Tray icon structure representing individual icons in the system tray.
    */
    typedef struct TrayIcon {
        unsigned long id;
        const char* tooltip;
        const RosGdiSurface* iconSurface;
    } TrayIcon;

    typedef enum class DockEdge {
        None,
        Left,
        Top,
        Right,
        Bottom
    } DockEdge;

    /**
     * Comprehensive taskbar state structure encapsulating all necessary information for rendering and interaction logic.
     */
    typedef struct TaskbarState {
        HWND hwndTaskbar;
        HWND hwndDesktop;
        DockEdge dockEdge;
        bool isVisible;
        bool isFullscreenHidden;
        bool isTopMost;
        bool useSingleTitleMode;
        bool showStartButton;
        bool showDoneButton;
        bool showSipButton;
        const char* currentNavText;
        unsigned long foregroundWindowId;
        unsigned long pressedPart;
        unsigned long hotPart;
        unsigned long capturedPart;
        TaskItem* tasks;
        TrayIcon* trayIcons;
        //notifications: list<NotificationItem>
        TaskbarMetrics metrics;
        TaskbarRects rects;
        Rect visibleDesktopRect;
        unsigned long fullScreenOwnerWindowId;
    } TaskbarState;




#ifdef __cplusplus
}
#endif

#endif
