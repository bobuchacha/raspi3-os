#ifndef ROS_APP_WIDGETS_H
#define ROS_APP_WIDGETS_H

#include "window.h"
#include "mini_font.h"

#define ROS_WIDGET_CLIENT_MODULE_NAME "widgets.dll"

#define ROS_WIDGET_CLASS_DIALOG "builtin.dialog"
#define ROS_WIDGET_CLASS_BUTTON "builtin.button"
#define ROS_WIDGET_CLASS_TEXTBOX "builtin.textbox"
#define ROS_WIDGET_CLASS_LABEL "builtin.label"

typedef enum RosWidgetKind {
    ROS_WIDGET_KIND_DIALOG = 1,
    ROS_WIDGET_KIND_BUTTON = 2,
    ROS_WIDGET_KIND_TEXTBOX = 3,
    ROS_WIDGET_KIND_LABEL = 4
} RosWidgetKind;

#if defined(ROS_WIDGET_EXPORTS)

#include "gdi.h"
typedef RosGdiSurface RosWidgetSurface;

#else

typedef struct RosWidgetSurface {
    HWND hwnd;
    unsigned long width;
    unsigned long height;
    unsigned long pitch;
    unsigned long pixel_format;
    void* pixels;
} RosWidgetSurface;

#endif

typedef struct RosWidgetPaintContext {
    HWND hwnd;
    RosWidgetSurface surface;
} RosWidgetPaintContext;

#if defined(ROS_WIDGET_EXPORTS)

/*
 * Register the built-in dialog, button, textbox, and label classes once for
 * the current process.
 *
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetInitialize(void);

/*
 * Register the built-in classes again if the caller wants an explicit setup
 * step in startup code.
 *
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetRegisterBuiltinClasses(void);

/*
 * Register one widget-backed class that routes messages through the widget
 * framework before optionally invoking the caller's custom procedure.
 *
 * @param class_name Stable class name used for later create calls.
 * @param kind Built-in visual behavior to attach to the class.
 * @param user_proc Optional caller procedure for extra handling.
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetRegisterClass(const char* class_name, unsigned long kind, WNDPROC user_proc);

/*
 * Create one widget-backed window using the extended window-create contract.
 *
 * @param class_name Previously registered widget class name.
 * @param title Initial caption or control text.
 * @param parent Optional parent handle for child controls.
 * @param x Surface-relative X position.
 * @param y Surface-relative Y position.
 * @param width Requested surface width.
 * @param height Requested surface height.
 * @param style Window style flags.
 * @return Non-zero window handle on success, or zero on failure.
 */
HWND WidgetCreateWindow(const char* class_name, const char* title, HWND parent, long x, long y, unsigned long width, unsigned long height, unsigned long style);

/*
 * Create one top-level dialog-style window with GWES-managed frame chrome.
 *
 * @param class_name Dialog class name previously registered through the widget framework.
 * @param title Dialog caption.
 * @param x Screen-space X position.
 * @param y Screen-space Y position.
 * @param width Outer window width.
 * @param height Outer window height.
 * @return Non-zero dialog handle on success, or zero on failure.
 */
HWND WidgetCreateDialog(const char* class_name, const char* title, long x, long y, unsigned long width, unsigned long height);

/*
 * Create one XP-style push button child window.
 *
 * @param parent Parent dialog or container window.
 * @param title Button caption.
 * @param x Client-area X position.
 * @param y Client-area Y position.
 * @param width Control width.
 * @param height Control height.
 * @return Non-zero window handle on success, or zero on failure.
 */
HWND WidgetCreateButton(HWND parent, const char* title, long x, long y, unsigned long width, unsigned long height);

/*
 * Create one label child window.
 *
 * @param parent Parent dialog or container window.
 * @param title Label text.
 * @param x Client-area X position.
 * @param y Client-area Y position.
 * @param width Control width.
 * @param height Control height.
 * @return Non-zero window handle on success, or zero on failure.
 */
HWND WidgetCreateLabel(HWND parent, const char* title, long x, long y, unsigned long width, unsigned long height);

/*
 * Create one textbox-style child window.
 *
 * @param parent Parent dialog or container window.
 * @param title Initial textbox contents.
 * @param x Client-area X position.
 * @param y Client-area Y position.
 * @param width Control width.
 * @param height Control height.
 * @return Non-zero window handle on success, or zero on failure.
 */
HWND WidgetCreateTextBox(HWND parent, const char* title, long x, long y, unsigned long width, unsigned long height);

/*
 * Configure the shared widget text font for the current process so callers
 * that care about a specific face can pin it before any controls paint.
 *
 * Passing a null or empty path keeps the default GDI policy, which chooses the
 * nearest staged raster `C:\fonts\tahoma-*.rtf` asset for the requested
 * height, then tries `C:\fonts\system_ui.rtf`, then `C:\fonts\system_ui.font`,
 * and finally the compiled-in System UI raster fallback.
 *
 * @param path Optional absolute font path inside the VFS.
 * @param pixel_height Requested rendered height, or zero to reuse the default widget size.
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetSetFont(const char* path, unsigned long pixel_height);

/*
 * Update the framework-owned caption buffer for one widget instance.
 *
 * @param hwnd Target widget handle.
 * @param text Replacement caption or contents.
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetSetText(HWND hwnd, const char* text);

/*
 * Update one widget's position and size through the shared window API.
 *
 * This keeps the widget DLL as the narrow convenience layer above `window.dll`
 * so applications can relayout controls without bypassing the framework.
 *
 * @param hwnd Target widget handle.
 * @param x New X position relative to the parent client area.
 * @param y New Y position relative to the parent client area.
 * @param width New widget width.
 * @param height New widget height.
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetSetBounds(HWND hwnd, long x, long y, unsigned long width, unsigned long height);

/*
 * Return the framework-owned caption buffer for one widget instance.
 *
 * @param hwnd Target widget handle.
 * @return Stable null-terminated widget text, or an empty string when unknown.
 */
const char* WidgetGetText(HWND hwnd);

/*
 * Acquire one widget surface for painting.
 *
 * @param hwnd Target widget handle.
 * @param context Receives the mapped paint context.
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetBeginPaint(HWND hwnd, RosWidgetPaintContext* context);

/*
 * Release the current paint surface and invalidate its full extents.
 *
 * @param context Caller-owned paint context previously filled by `WidgetBeginPaint`.
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetEndPaint(RosWidgetPaintContext* context);

/*
 * Fill one rectangle directly in the caller-owned paint context.
 *
 * @param context Active paint context.
 * @param x Rectangle X position.
 * @param y Rectangle Y position.
 * @param width Rectangle width.
 * @param height Rectangle height.
 * @param color RGB fill color.
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetFillRect(const RosWidgetPaintContext* context, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color);

/*
 * Draw a simple one-pixel border around one rectangle.
 *
 * @param context Active paint context.
 * @param x Rectangle X position.
 * @param y Rectangle Y position.
 * @param width Rectangle width.
 * @param height Rectangle height.
 * @param color RGB border color.
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetFrameRect(const RosWidgetPaintContext* context, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color);

/*
 * Draw one top-to-bottom color gradient.
 *
 * @param context Active paint context.
 * @param x Rectangle X position.
 * @param y Rectangle Y position.
 * @param width Rectangle width.
 * @param height Rectangle height.
 * @param top_color RGB color used on the first row.
 * @param bottom_color RGB color used on the last row.
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetFillVerticalGradient(const RosWidgetPaintContext* context, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long top_color, unsigned long bottom_color);

/*
 * Draw one left-aligned label string with the active GDI UI font.
 *
 * @param context Active paint context.
 * @param x Text origin X position.
 * @param y Text origin Y position.
 * @param text Null-terminated string to render.
 * @param color RGB text color.
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetDrawText(const RosWidgetPaintContext* context, unsigned long x, unsigned long y, const char* text, unsigned long color);

/*
 * Draw one centered label string inside a bounding rectangle.
 *
 * @param context Active paint context.
 * @param x Bounding rectangle X position.
 * @param y Bounding rectangle Y position.
 * @param width Bounding rectangle width.
 * @param height Bounding rectangle height.
 * @param text Null-terminated string to render.
 * @param color RGB text color.
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetDrawCenteredText(const RosWidgetPaintContext* context, unsigned long x, unsigned long y, unsigned long width, unsigned long height, const char* text, unsigned long color);

/*
 * Fill one dialog client area using the shared XP-style palette.
 *
 * @param context Active paint context.
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetPaintDialogBackground(const RosWidgetPaintContext* context);

/*
 * Queue one repaint request for the target widget.
 *
 * @param hwnd Target widget handle.
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetInvalidate(HWND hwnd);

#else

DECLARE(long, WidgetInitialize, (void), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetInitialize");
DECLARE(long, WidgetRegisterBuiltinClasses, (void), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetRegisterBuiltinClasses");
DECLARE(long, WidgetRegisterClass, (const char* class_name, unsigned long kind, WNDPROC user_proc), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetRegisterClass");
DECLARE(HWND, WidgetCreateWindow, (const char* class_name, const char* title, HWND parent, long x, long y, unsigned long width, unsigned long height, unsigned long style), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetCreateWindow");
DECLARE(HWND, WidgetCreateDialog, (const char* class_name, const char* title, long x, long y, unsigned long width, unsigned long height), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetCreateDialog");
DECLARE(HWND, WidgetCreateButton, (HWND parent, const char* title, long x, long y, unsigned long width, unsigned long height), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetCreateButton");
DECLARE(HWND, WidgetCreateLabel, (HWND parent, const char* title, long x, long y, unsigned long width, unsigned long height), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetCreateLabel");
DECLARE(HWND, WidgetCreateTextBox, (HWND parent, const char* title, long x, long y, unsigned long width, unsigned long height), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetCreateTextBox");
DECLARE(long, WidgetSetFont, (const char* path, unsigned long pixel_height), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetSetFont");
DECLARE(long, WidgetSetText, (HWND hwnd, const char* text), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetSetText");
DECLARE(long, WidgetSetBounds, (HWND hwnd, long x, long y, unsigned long width, unsigned long height), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetSetBounds");
DECLARE(const char*, WidgetGetText, (HWND hwnd), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetGetText");
DECLARE(long, WidgetBeginPaint, (HWND hwnd, RosWidgetPaintContext* context), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetBeginPaint");
DECLARE(long, WidgetEndPaint, (RosWidgetPaintContext* context), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetEndPaint");
DECLARE(long, WidgetFillRect, (const RosWidgetPaintContext* context, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetFillRect");
DECLARE(long, WidgetFrameRect, (const RosWidgetPaintContext* context, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetFrameRect");
DECLARE(long, WidgetFillVerticalGradient, (const RosWidgetPaintContext* context, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long top_color, unsigned long bottom_color), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetFillVerticalGradient");
DECLARE(long, WidgetDrawText, (const RosWidgetPaintContext* context, unsigned long x, unsigned long y, const char* text, unsigned long color), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetDrawText");
DECLARE(long, WidgetDrawCenteredText, (const RosWidgetPaintContext* context, unsigned long x, unsigned long y, unsigned long width, unsigned long height, const char* text, unsigned long color), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetDrawCenteredText");
DECLARE(long, WidgetPaintDialogBackground, (const RosWidgetPaintContext* context), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetPaintDialogBackground");
DECLARE(long, WidgetInvalidate, (HWND hwnd), FROM, ROS_WIDGET_CLIENT_MODULE_NAME, "WidgetInvalidate");

#endif

#endif
