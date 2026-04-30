#define ROS_APP_USE_WINDOW 1
#define ROS_APP_USE_WIDGETS 1
#include "app/app.h"

#include <stdio.h>

#define WIDGETDEMO_DIALOG_CLASS "widgetdemo.dialog"
#define WIDGETDEMO_BUTTON_CLASS "widgetdemo.button"
#define WIDGETDEMO_TEXTBOX_CLASS "widgetdemo.textbox"
#define WIDGETDEMO_DIALOG_TITLE "Widget Demo"
#define WIDGETDEMO_DIALOG_X 72L
#define WIDGETDEMO_DIALOG_Y 72L
#define WIDGETDEMO_DIALOG_WIDTH 420UL
#define WIDGETDEMO_DIALOG_HEIGHT 296UL
#define WIDGETDEMO_WM_FONT_READY (WM_USER + 1UL)
#define WIDGETDEMO_DIALOG_BORDER 4UL
#define WIDGETDEMO_DIALOG_TITLEBAR 28UL
#define WIDGETDEMO_TAHOMA_FONT_PATH "C:\\fonts\\tahoma-12.rtf"
#define WIDGETDEMO_FONT_HEIGHT 12UL

#define WIDGETDEMO_HEADER_X 16UL
#define WIDGETDEMO_HEADER_Y 18UL
#define WIDGETDEMO_HEADER_HEIGHT 56UL
#define WIDGETDEMO_CONTENT_MARGIN 24L
#define WIDGETDEMO_CONTROL_GAP 12L
#define WIDGETDEMO_BUTTON_WIDTH 92UL
#define WIDGETDEMO_BUTTON_HEIGHT 28UL
#define WIDGETDEMO_FIELD_MIN_WIDTH 120UL
#define WIDGETDEMO_INPUT_MIN_WIDTH 180UL
#define WIDGETDEMO_NOTE_HEIGHT 20UL
#define WIDGETDEMO_LEFT_COLUMN_X 24L
#define WIDGETDEMO_RIGHT_COLUMN_X 118L
#define WIDGETDEMO_FIRST_ROW_Y 92L
#define WIDGETDEMO_ROW_STEP 32L

static HWND widgetdemo_dialog = 0UL;
static HWND widgetdemo_status_label = 0UL;
static HWND widgetdemo_theme_label = 0UL;
static HWND widgetdemo_state_textbox = 0UL;
static HWND widgetdemo_note_label = 0UL;
static HWND widgetdemo_input_label = 0UL;
static HWND widgetdemo_primary_button = 0UL;
static HWND widgetdemo_secondary_button = 0UL;
static unsigned long widgetdemo_click_count = 0UL;
static int widgetdemo_secondary_active = 0;
static int widgetdemo_font_load_started = 0;
static int widgetdemo_font_load_running = 0;
static long widgetdemo_client_x = WIDGETDEMO_DIALOG_X + (long)WIDGETDEMO_DIALOG_BORDER;
static long widgetdemo_client_y = WIDGETDEMO_DIALOG_Y + (long)WIDGETDEMO_DIALOG_TITLEBAR;
static unsigned long widgetdemo_client_width = WIDGETDEMO_DIALOG_WIDTH - (WIDGETDEMO_DIALOG_BORDER * 2UL);
static unsigned long widgetdemo_client_height = WIDGETDEMO_DIALOG_HEIGHT - WIDGETDEMO_DIALOG_TITLEBAR - WIDGETDEMO_DIALOG_BORDER;
static const char* widgetdemo_font_line = "FONT: pending";
static char widgetdemo_input_line[128] = "INPUT: waiting for keyboard or pointer";

/*
 * Update the helper note line shown near the bottom of the dialog.
 *
 * @param text New null-terminated note line.
 * @return Nothing.
 */
static void widgetdemo_set_note_line(const char* text) {
    if (!text || widgetdemo_note_label == 0UL) {
        return;
    }

    (void)WidgetSetText(widgetdemo_note_label, text);
}

/*
 * Update the live input status line so manual virt input testing is visible on
 * screen instead of only through the serial log.
 *
 * @param text New null-terminated status string.
 * @return Nothing.
 */
static void widgetdemo_set_input_line(const char* text) {
    if (!text) {
        return;
    }

    snprintf(widgetdemo_input_line, sizeof(widgetdemo_input_line), "%s", text);
    if (widgetdemo_input_label != 0UL) {
        (void)WidgetSetText(widgetdemo_input_label, widgetdemo_input_line);
    }
}

/*
 * Format one readable summary for the keyboard and pointer messages the new
 * shared-input path should deliver into the dialog.
 *
 * @param message Window message identifier.
 * @param wParam First message payload word.
 * @param lParam Second message payload word.
 * @return Nothing.
 */
static void widgetdemo_record_input(unsigned long message, unsigned long wParam, unsigned long lParam) {
    long x;
    long y;
    char line[128];

    switch (message) {
    case WM_KEYDOWN:
    case WM_KEYUP:
        if ((wParam >= 32UL) && (wParam <= 126UL)) {
            snprintf(line, sizeof(line), "INPUT: %s '%c' (%lu)", message == WM_KEYDOWN ? "key down" : "key up", (int)wParam, wParam);
        }
        else {
            snprintf(line, sizeof(line), "INPUT: %s code=%lu", message == WM_KEYDOWN ? "key down" : "key up", wParam);
        }
        widgetdemo_set_input_line(line);
        break;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSECLICKED:
    case WM_MOUSELEAVE:
        WindowUnpackSignedPair(lParam, &x, &y);
        snprintf(line,
            sizeof(line),
            "INPUT: %s x=%ld y=%ld buttons=%lu",
            WindowMessageName(message),
            x,
            y,
            wParam);
        widgetdemo_set_input_line(line);
        break;
    default:
        break;
    }
}

/*
 * Write one widget-demo status line so bring-up failures remain visible even
 * before the GUI sample is on screen.
 *
 * @param text Null-terminated line to print.
 * @return Nothing.
 */
static void widgetdemo_log(const char* text) {
    if (!text) {
        return;
    }

    writeLine(text);
}

/*
 * Notify the dialog owner that async font state changed.
 *
 * The new window.dll asset callback runs on a background worker thread, so it
 * must post back to the dialog owner before refreshing widget state or asking
 * the framework to repaint.
 *
 * @return Nothing.
 */
static void widgetdemo_post_font_ready(void) {
    if (widgetdemo_dialog != 0UL) {
        (void)PostMessage(widgetdemo_dialog, WIDGETDEMO_WM_FONT_READY, 0UL, 0UL);
    }
}

/*
 * Ask the widget framework to load the staged prerasterized Tahoma face up
 * front so the sample paints with the same bitmap assets that GDI now prefers
 * by default.
 *
 * @return Nothing.
 */
static void widgetdemo_prepare_font(void) {
    long tahoma_status = WidgetSetFont(WIDGETDEMO_TAHOMA_FONT_PATH, WIDGETDEMO_FONT_HEIGHT);

    if (tahoma_status >= 0L) {
        widgetdemo_font_line = "FONT: Tahoma raster 14px";
        widgetdemo_log("widgetdemo.exe: loaded C:\\fonts\\tahoma-14.rtf");
        return;
    }

    {
        char line[128];

        snprintf(line, sizeof(line), "widgetdemo.exe: explicit tahoma raster load failed status=%ld", tahoma_status);
        widgetdemo_log(line);
    }

    {
        long fallback_status = WidgetSetFont(0, WIDGETDEMO_FONT_HEIGHT);

        if (fallback_status >= 0L) {
            widgetdemo_font_line = "FONT: Default UI fallback";
            {
                char line[128];

                snprintf(line, sizeof(line), "widgetdemo.exe: raster default policy selected fallback status=%ld", fallback_status);
                widgetdemo_log(line);
            }
            return;
        }

        {
            char line[128];

            snprintf(line, sizeof(line), "widgetdemo.exe: default raster widget font load failed status=%ld", fallback_status);
            widgetdemo_log(line);
        }
    }

    widgetdemo_font_line = "FONT: Mini-font fallback";
    widgetdemo_log("widgetdemo.exe: widget font load failed, using mini-font fallback");
}

/*
 * Complete one async font-load attempt and wake the dialog owner.
 *
 * The widget font state is mutated from the background callback thread, but the
 * visible label text and invalidation still belong on the UI thread.
 *
 * @return Nothing.
 */
static void widgetdemo_finish_font_load(void) {
    widgetdemo_font_load_running = 0;
    widgetdemo_post_font_ready();
}

/*
 * Prepare the widget font after window.dll finished staging the preferred font asset.
 *
 * The current widget framework still loads fonts by path, so the async asset
 * callback uses the staged file-read completion as the non-blocking scheduler
 * and then performs the actual `WidgetSetFont` call on the same background
 * worker thread.
 *
 * @param bytes Heap-backed asset bytes returned by window.dll.
 * @return Nothing.
 */
static void widgetdemo_prepare_font_from_callback(const void* bytes) {
    if (bytes != 0) {
        FreeAssetBuffer((void*)bytes);
    }

    widgetdemo_prepare_font();
    widgetdemo_finish_font_load();
}

/*
 * Handle one successful async staging of the preferred widget font file.
 *
 * @param request_id Stable request identifier returned by `LoadFileAssetAsync`.
 * @param bytes Heap-backed file bytes returned by window.dll.
 * @param size Byte count stored in `bytes`.
 * @param path Source file path.
 * @param resource_name Unused embedded-resource name for file-backed loads.
 * @param context Unused callback context.
 * @return Nothing.
 */
static void widgetdemo_font_asset_success(
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
    widgetdemo_prepare_font_from_callback(bytes);
}

/*
 * Handle one failed async staging attempt for the preferred widget font file.
 *
 * The explicit Tahoma path may be missing on some images, but the sample still
 * wants the default-font and mini-font fallback chain to run on the background
 * worker instead of blocking the initial window creation path.
 *
 * @param request_id Stable request identifier returned by `LoadFileAssetAsync`.
 * @param status Negative failure status from the staging attempt.
 * @param path Source file path.
 * @param resource_name Unused embedded-resource name for file-backed loads.
 * @param context Unused callback context.
 * @return Nothing.
 */
static void widgetdemo_font_asset_error(
    unsigned long request_id,
    long status,
    const char* path,
    const char* resource_name,
    void* context) {
    char line[160];

    (void)request_id;
    (void)resource_name;
    (void)context;
    snprintf(line, sizeof(line), "widgetdemo.exe: async font stage failed path=%s status=%ld", path ? path : "<null>", status);
    widgetdemo_log(line);
    widgetdemo_prepare_font_from_callback(0);
}

/*
 * Start the non-blocking widget font staging path once.
 *
 * The dialog can be created immediately with a pending font line, and the
 * async completion later refreshes the theme label without dedicating an app-
 * specific parked helper thread to this one startup task.
 *
 * @return Nothing.
 */
static void widgetdemo_start_font_load(void) {
    long request_id;
    char line[160];

    if (widgetdemo_font_load_started) {
        return;
    }

    widgetdemo_font_load_started = 1;
    widgetdemo_font_load_running = 1;
    request_id = LoadFileAssetAsync(
        WIDGETDEMO_TAHOMA_FONT_PATH,
        widgetdemo_font_asset_success,
        widgetdemo_font_asset_error,
        0);
    if (request_id >= 0L) {
        snprintf(line, sizeof(line), "widgetdemo.exe: queued async font stage request=%ld", request_id);
        widgetdemo_log(line);
        return;
    }

    snprintf(line, sizeof(line), "widgetdemo.exe: async font queue failed status=%ld", request_id);
    widgetdemo_log(line);
    widgetdemo_prepare_font();
    widgetdemo_finish_font_load();
}

/*
 * Refresh the demo captions so button clicks, textbox edits, and geometry
 * changes stay visible without relying on the serial log.
 *
 * The original sample refreshed these controls every 20 ms even when no state
 * had changed, which created avoidable invalidation traffic and made multiple
 * widgetdemo instances act like a background stress test. Event-driven updates
 * keep the sample representative without penalizing the rest of the shell.
 *
 * @return Nothing.
 */
static void widgetdemo_refresh_children(void) {
    char status_line[128];
    char theme_line[128];
    char note_line[160];
    const char* textbox_text = WidgetGetText(widgetdemo_state_textbox);

    snprintf(status_line,
        sizeof(status_line),
        "STATUS: clicks=%lu text=%s",
        widgetdemo_click_count,
        textbox_text && textbox_text[0] != '\0' ? textbox_text : "<empty>");
    (void)WidgetSetText(widgetdemo_status_label, status_line);

    snprintf(theme_line,
        sizeof(theme_line),
        "%s | mode=%s | updates=event-driven",
        widgetdemo_font_line,
        widgetdemo_secondary_active ? "ACTIVE" : "STABLE");
    (void)WidgetSetText(widgetdemo_theme_label, theme_line);

    snprintf(note_line,
        sizeof(note_line),
        "Move=%ld,%ld Size=%lux%lu. Drag title bar or edges, click buttons, type in textbox.",
        widgetdemo_client_x,
        widgetdemo_client_y,
        widgetdemo_client_width,
        widgetdemo_client_height);
    widgetdemo_set_note_line(note_line);

    snprintf(status_line, sizeof(status_line), "Clicks %lu", widgetdemo_click_count);
    (void)WidgetSetText(widgetdemo_primary_button, status_line);
    (void)WidgetSetText(widgetdemo_secondary_button, widgetdemo_secondary_active ? "ACTIVE" : "STABLE");
}

/*
 * Reposition child controls after the dialog client area changes size.
 *
 * The sample intentionally does this from `WM_SIZE` so the relayout path uses
 * the same `MoveWindow`-style contract as Win32 child windows instead of
 * recreating controls or depending on hard-coded startup geometry.
 *
 * @return Nothing.
 */
static void widgetdemo_layout_children(void) {
    long button_x;
    unsigned long field_width;
    unsigned long input_width;
    long note_y;
    unsigned long note_width;

    button_x = (widgetdemo_client_width > (unsigned long)(WIDGETDEMO_CONTENT_MARGIN + WIDGETDEMO_BUTTON_WIDTH))
        ? ((long)widgetdemo_client_width - WIDGETDEMO_CONTENT_MARGIN - (long)WIDGETDEMO_BUTTON_WIDTH)
        : (WIDGETDEMO_RIGHT_COLUMN_X + 140L);
    if (button_x < (WIDGETDEMO_RIGHT_COLUMN_X + 108L)) {
        button_x = WIDGETDEMO_RIGHT_COLUMN_X + 108L;
    }

    field_width = button_x > (WIDGETDEMO_RIGHT_COLUMN_X + WIDGETDEMO_CONTROL_GAP)
        ? (unsigned long)(button_x - WIDGETDEMO_RIGHT_COLUMN_X - WIDGETDEMO_CONTROL_GAP)
        : WIDGETDEMO_FIELD_MIN_WIDTH;
    if (field_width < WIDGETDEMO_FIELD_MIN_WIDTH) {
        field_width = WIDGETDEMO_FIELD_MIN_WIDTH;
    }

    input_width = widgetdemo_client_width > (unsigned long)(WIDGETDEMO_RIGHT_COLUMN_X + WIDGETDEMO_CONTENT_MARGIN)
        ? (widgetdemo_client_width - (unsigned long)WIDGETDEMO_RIGHT_COLUMN_X - (unsigned long)WIDGETDEMO_CONTENT_MARGIN)
        : WIDGETDEMO_INPUT_MIN_WIDTH;
    if (input_width < WIDGETDEMO_INPUT_MIN_WIDTH) {
        input_width = WIDGETDEMO_INPUT_MIN_WIDTH;
    }

    note_y = widgetdemo_client_height > 54UL
        ? ((long)widgetdemo_client_height - 38L)
        : 226L;
    note_width = widgetdemo_client_width > (unsigned long)(WIDGETDEMO_CONTENT_MARGIN * 2L)
        ? (widgetdemo_client_width - (unsigned long)(WIDGETDEMO_CONTENT_MARGIN * 2L))
        : 220UL;

    if (widgetdemo_status_label != 0UL) {
        (void)WidgetSetBounds(widgetdemo_status_label, WIDGETDEMO_RIGHT_COLUMN_X, WIDGETDEMO_FIRST_ROW_Y, field_width, 20UL);
    }
    if (widgetdemo_theme_label != 0UL) {
        (void)WidgetSetBounds(widgetdemo_theme_label, WIDGETDEMO_RIGHT_COLUMN_X, WIDGETDEMO_FIRST_ROW_Y + WIDGETDEMO_ROW_STEP, field_width, 20UL);
    }
    if (widgetdemo_state_textbox != 0UL) {
        (void)WidgetSetBounds(widgetdemo_state_textbox, WIDGETDEMO_RIGHT_COLUMN_X, WIDGETDEMO_FIRST_ROW_Y + (WIDGETDEMO_ROW_STEP * 2L), field_width, 24UL);
    }
    if (widgetdemo_input_label != 0UL) {
        (void)WidgetSetBounds(widgetdemo_input_label, WIDGETDEMO_RIGHT_COLUMN_X, WIDGETDEMO_FIRST_ROW_Y + (WIDGETDEMO_ROW_STEP * 3L), input_width, 20UL);
    }
    if (widgetdemo_primary_button != 0UL) {
        (void)WidgetSetBounds(widgetdemo_primary_button, button_x, 90L, WIDGETDEMO_BUTTON_WIDTH, WIDGETDEMO_BUTTON_HEIGHT);
    }
    if (widgetdemo_secondary_button != 0UL) {
        (void)WidgetSetBounds(widgetdemo_secondary_button, button_x, 124L, WIDGETDEMO_BUTTON_WIDTH, WIDGETDEMO_BUTTON_HEIGHT);
    }
    if (widgetdemo_note_label != 0UL) {
        (void)WidgetSetBounds(widgetdemo_note_label, WIDGETDEMO_CONTENT_MARGIN, note_y, note_width, WIDGETDEMO_NOTE_HEIGHT);
    }
}

/*
 * Mirror one control-local input message into the shared input status line.
 *
 * @param message Window message identifier.
 * @param wParam First message payload word.
 * @param lParam Second message payload word.
 * @return Nothing.
 */
static void widgetdemo_record_control_input(unsigned long message, unsigned long wParam, unsigned long lParam) {
    if (message == WM_MOUSEMOVE
        || message == WM_MOUSELEAVE
        || message == WM_LBUTTONDOWN
        || message == WM_LBUTTONUP
        || message == WM_MOUSECLICKED
        || message == WM_KEYDOWN
        || message == WM_KEYUP) {
        widgetdemo_record_input(message, wParam, lParam);
    }
}

/*
 * Handle button-specific widget events so the demo proves child procedures are
 * wired independently from the dialog window procedure.
 *
 * @param hwnd Target button handle.
 * @param message Window message identifier.
 * @param wParam First message payload.
 * @param lParam Second message payload.
 * @return Always zero for the sample.
 */
static LRESULT widgetdemo_button_proc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    widgetdemo_record_control_input(message, wParam, lParam);

    if (message == WM_MOUSECLICKED) {
        if (hwnd == widgetdemo_primary_button) {
            ++widgetdemo_click_count;
        }
        else if (hwnd == widgetdemo_secondary_button) {
            widgetdemo_secondary_active = !widgetdemo_secondary_active;
        }
        widgetdemo_refresh_children();
    }

    return 0L;
}

/*
 * Handle textbox change notifications so the demo mirrors classic edit-control
 * behavior where text entry updates application state through window messages.
 *
 * @param hwnd Target textbox handle.
 * @param message Window message identifier.
 * @param wParam First message payload.
 * @param lParam Second message payload.
 * @return Always zero for the sample.
 */
static LRESULT widgetdemo_textbox_proc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    (void)hwnd;

    widgetdemo_record_control_input(message, wParam, lParam);
    if (message == WM_CHANGED) {
        widgetdemo_refresh_children();
    }

    return 0L;
}

/*
 * Paint the dialog client area while GWES owns the outer frame, title bar, and
 * close button chrome.
 *
 * The footer now explains the event-driven update model directly, so the demo
 * no longer needs a synthetic liveness timer that repaints the dialog forever.
 *
 * @param hwnd Dialog window handle.
 * @return Zero on success, or a negative status code on failure.
 */
static long widgetdemo_paint_dialog(HWND hwnd) {
    RosWidgetPaintContext context;
    unsigned long header_width;
    unsigned long footer_y;

    if (WidgetBeginPaint(hwnd, &context) < 0) {
        return -1L;
    }

    (void)WidgetPaintDialogBackground(&context);

    header_width = context.surface.width > (WIDGETDEMO_HEADER_X * 2UL) ? (context.surface.width - (WIDGETDEMO_HEADER_X * 2UL)) : context.surface.width;
    (void)WidgetFillVerticalGradient(&context, WIDGETDEMO_HEADER_X, WIDGETDEMO_HEADER_Y, header_width, WIDGETDEMO_HEADER_HEIGHT, 0x00FFFFFFUL, 0x00DCEAF8UL);
    (void)WidgetFrameRect(&context, WIDGETDEMO_HEADER_X, WIDGETDEMO_HEADER_Y, header_width, WIDGETDEMO_HEADER_HEIGHT, 0x008AA2BDUL);
    (void)WidgetDrawText(&context, WIDGETDEMO_HEADER_X + 10UL, WIDGETDEMO_HEADER_Y + 10UL, "XP STYLE USERSPACE WIDGETS", 0x00243752UL);
    (void)WidgetDrawText(&context, WIDGETDEMO_HEADER_X + 10UL, WIDGETDEMO_HEADER_Y + 28UL, "GWES draws chrome. The app paints content. Child controls own surfaces.", 0x00304860UL);

    (void)WidgetDrawText(&context, 24UL, 84UL, "STATUS", 0x00283848UL);
    (void)WidgetDrawText(&context, 24UL, 116UL, "THEME", 0x00283848UL);
    (void)WidgetDrawText(&context, 24UL, 148UL, "STATE", 0x00283848UL);
    (void)WidgetDrawText(&context, 24UL, 180UL, "INPUT", 0x00283848UL);

    footer_y = context.surface.height > 42UL ? (context.surface.height - 34UL) : 0UL;
    (void)WidgetFillVerticalGradient(&context, 16UL, footer_y, context.surface.width > 32UL ? (context.surface.width - 32UL) : context.surface.width, 18UL, 0x00E6EBF2UL, 0x00CCD7E3UL);
    (void)WidgetFrameRect(&context, 16UL, footer_y, context.surface.width > 32UL ? (context.surface.width - 32UL) : context.surface.width, 18UL, 0x0091A0B3UL);
    (void)WidgetDrawText(&context, 24UL, footer_y + 4UL, "Updates happen only on input, edits, clicks, and resizes.", 0x00303E4FUL);

    return WidgetEndPaint(&context);
}

/*
 * Handle the demo dialog lifecycle and create the built-in child controls.
 *
 * @param hwnd Target window handle.
 * @param message Window message identifier.
 * @param wParam First message payload.
 * @param lParam Second message payload.
 * @return Always zero for the sample.
 */
static LRESULT widgetdemo_dialog_proc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    if (message == WM_CREATE) {
        widgetdemo_dialog = hwnd;
        widgetdemo_status_label = WidgetCreateLabel(hwnd, "STATUS: booting", WIDGETDEMO_RIGHT_COLUMN_X, WIDGETDEMO_FIRST_ROW_Y, 214UL, 20UL);
        widgetdemo_theme_label = WidgetCreateLabel(hwnd, widgetdemo_font_line, WIDGETDEMO_RIGHT_COLUMN_X, WIDGETDEMO_FIRST_ROW_Y + WIDGETDEMO_ROW_STEP, 214UL, 20UL);
        widgetdemo_state_textbox = WidgetCreateWindow(
            WIDGETDEMO_TEXTBOX_CLASS,
            "Type here",
            hwnd,
            WIDGETDEMO_RIGHT_COLUMN_X,
            WIDGETDEMO_FIRST_ROW_Y + (WIDGETDEMO_ROW_STEP * 2L),
            224UL,
            24UL,
            ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_CHILD | ROS_WINDOW_STYLE_BORDER);
        widgetdemo_input_label = WidgetCreateLabel(hwnd, widgetdemo_input_line, WIDGETDEMO_RIGHT_COLUMN_X, WIDGETDEMO_FIRST_ROW_Y + (WIDGETDEMO_ROW_STEP * 3L), 250UL, 20UL);
        widgetdemo_note_label = WidgetCreateLabel(hwnd, "Move=0,0 Size=0x0", 24L, 226L, 330UL, 20UL);
        widgetdemo_primary_button = WidgetCreateWindow(
            WIDGETDEMO_BUTTON_CLASS,
            "Clicks 0",
            hwnd,
            330L,
            90L,
            92UL,
            28UL,
            ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_CHILD | ROS_WINDOW_STYLE_BORDER);
        widgetdemo_secondary_button = WidgetCreateWindow(
            WIDGETDEMO_BUTTON_CLASS,
            "STABLE",
            hwnd,
            330L,
            124L,
            92UL,
            28UL,
            ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_CHILD | ROS_WINDOW_STYLE_BORDER);
        widgetdemo_layout_children();
        widgetdemo_refresh_children();
    }
    else if (message == WM_PAINT) {
        (void)widgetdemo_paint_dialog(hwnd);
    }
    else if (message == WIDGETDEMO_WM_FONT_READY) {
        widgetdemo_refresh_children();
        (void)WidgetInvalidate(hwnd);
    }
    else if (message == WM_MOVE) {
        WindowUnpackSignedPair(lParam, &widgetdemo_client_x, &widgetdemo_client_y);
        widgetdemo_refresh_children();
    }
    else if (message == WM_SIZE) {
        long width;
        long height;

        WindowUnpackSignedPair(lParam, &width, &height);
        widgetdemo_client_width = width > 0L ? (unsigned long)width : 0UL;
        widgetdemo_client_height = height > 0L ? (unsigned long)height : 0UL;
        widgetdemo_layout_children();
        widgetdemo_refresh_children();
        (void)WidgetInvalidate(hwnd);
    }
    else if (message == WM_MOUSEMOVE || message == WM_MOUSELEAVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_MOUSECLICKED || message == WM_KEYDOWN || message == WM_KEYUP) {
        widgetdemo_record_input(message, wParam, lParam);
    }
    else if (message == WM_CLOSE) {
        (void)PostQuitMessage(0L);
    }

    return 0L;
}

int main(void) {
    MSG message;
    long get_result;

    if (WidgetRegisterClass(WIDGETDEMO_DIALOG_CLASS, ROS_WIDGET_KIND_DIALOG, widgetdemo_dialog_proc) < 0) {
        widgetdemo_log("widgetdemo.exe: failed to register dialog class");
        return 1;
    }
    if (WidgetRegisterClass(WIDGETDEMO_BUTTON_CLASS, ROS_WIDGET_KIND_BUTTON, widgetdemo_button_proc) < 0) {
        widgetdemo_log("widgetdemo.exe: failed to register button class");
        return 1;
    }
    if (WidgetRegisterClass(WIDGETDEMO_TEXTBOX_CLASS, ROS_WIDGET_KIND_TEXTBOX, widgetdemo_textbox_proc) < 0) {
        widgetdemo_log("widgetdemo.exe: failed to register textbox class");
        return 1;
    }

    widgetdemo_dialog = WidgetCreateDialog(
        WIDGETDEMO_DIALOG_CLASS,
        WIDGETDEMO_DIALOG_TITLE,
        WIDGETDEMO_DIALOG_X,
        WIDGETDEMO_DIALOG_Y,
        WIDGETDEMO_DIALOG_WIDTH,
        WIDGETDEMO_DIALOG_HEIGHT);
    if (widgetdemo_dialog == 0UL) {
        widgetdemo_log("widgetdemo.exe: failed to create dialog window");
        return 1;
    }

    widgetdemo_start_font_load();
    widgetdemo_log("widgetdemo.exe: dialog created");
    for (;;) {
        get_result = GetMessage(&message);
        if (get_result <= 0L) {
            break;
        }
        (void)TranslateMessage(&message);
        (void)DispatchMessage(&message);
    }

    while (widgetdemo_font_load_running) {
        (void)sleepMs(1UL);
    }

    widgetdemo_log("widgetdemo.exe: message loop exited");
    return 0;
}
