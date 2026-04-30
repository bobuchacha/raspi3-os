#define ROS_APP_USE_WINDOW 1
#define ROS_APP_USE_GDI 1
#include "app/app.h"
#include "console.h"

#define CONSOLEDEMO_CLASS_NAME "consoledemo.main"
#define CONSOLEDEMO_WINDOW_TITLE "Console Demo"
#define CONSOLEDEMO_WINDOW_X 88L
#define CONSOLEDEMO_WINDOW_Y 72L
#define CONSOLEDEMO_WINDOW_WIDTH 720UL
#define CONSOLEDEMO_WINDOW_HEIGHT 420UL
#define CONSOLEDEMO_FONT_HEIGHT 14UL
#define CONSOLEDEMO_MAX_LINES 12UL
#define CONSOLEDEMO_LINE_CAPACITY 112UL
#define CONSOLEDEMO_MARGIN 18UL
#define CONSOLEDEMO_HEADER_HEIGHT 44UL
#define CONSOLEDEMO_TIMER_MSEC 500UL

static HWND g_consoledemo_window = 0UL;
static unsigned long g_consoledemo_timer_id = 0UL;
static RosGdiSurface g_consoledemo_surface;
static int g_consoledemo_surface_ready = 0;
static RosGdiFont g_consoledemo_font;
static int g_consoledemo_font_ready = 0;
static unsigned long g_consoledemo_tick_count = 0UL;
static unsigned long g_consoledemo_paint_count = 0UL;
static unsigned long g_consoledemo_key_count = 0UL;
static unsigned long g_consoledemo_click_count = 0UL;
static unsigned long g_consoledemo_move_count = 0UL;
static unsigned long g_consoledemo_first_line = 0UL;
static unsigned long g_consoledemo_line_count = 0UL;
static char g_consoledemo_status_line[CONSOLEDEMO_LINE_CAPACITY] = "Waiting for window events";
static char g_consoledemo_lines[CONSOLEDEMO_MAX_LINES][CONSOLEDEMO_LINE_CAPACITY];

/*
 * Append one signed decimal value to a caller-owned text cursor.
 *
 * @param destination Output cursor inside a writable character buffer.
 * @param value Signed value to append.
 * @return Advanced cursor after the appended digits.
 */
static char* consoledemo_append_long(char* destination, long value) {
    if (value < 0L) {
        *destination++ = '-';
        return appendUnsignedLong(destination, (unsigned long)(-value));
    }

    return appendUnsignedLong(destination, (unsigned long)value);
}

/*
 * Copy one short text string into a caller-owned fixed buffer.
 *
 * @param destination Output buffer.
 * @param capacity Output buffer capacity in bytes.
 * @param source Optional source string.
 * @return Nothing.
 */
static void consoledemo_copy_text(char* destination, unsigned long capacity, const char* source) {
    unsigned long index = 0UL;

    if (destination == 0 || capacity == 0UL) {
        return;
    }
    if (source == 0) {
        destination[0] = '\0';
        return;
    }

    while (source[index] != '\0' && (index + 1UL) < capacity) {
        destination[index] = source[index];
        ++index;
    }

    destination[index] = '\0';
}

/*
 * Invalidate the full sample window when a state change should repaint the
 * visible debug overlay.
 *
 * @return Nothing.
 */
static void consoledemo_invalidate_window(void) {
    if (!g_consoledemo_surface_ready || g_consoledemo_window == 0UL) {
        return;
    }

    (void)GdiInvalidateRect(
        g_consoledemo_window,
        0UL,
        0UL,
        g_consoledemo_surface.width,
        g_consoledemo_surface.height);
}

/*
 * Update the prominent on-screen status line.
 *
 * @param text New status string.
 * @return Nothing.
 */
static void consoledemo_set_status(const char* text) {
    consoledemo_copy_text(g_consoledemo_status_line, sizeof(g_consoledemo_status_line), text);
    consoledemo_invalidate_window();
}

/*
 * Append one line to the visible ring buffer and mirror it to the serial
 * console through the new `Console` port.
 *
 * @param text New event line.
 * @return Nothing.
 */
static void consoledemo_append_line(const char* text) {
    unsigned long slot_index;

    if (text == 0) {
        return;
    }

    if (g_consoledemo_line_count < CONSOLEDEMO_MAX_LINES) {
        slot_index = (g_consoledemo_first_line + g_consoledemo_line_count) % CONSOLEDEMO_MAX_LINES;
        ++g_consoledemo_line_count;
    }
    else {
        slot_index = g_consoledemo_first_line;
        g_consoledemo_first_line = (g_consoledemo_first_line + 1UL) % CONSOLEDEMO_MAX_LINES;
    }

    consoledemo_copy_text(g_consoledemo_lines[slot_index], CONSOLEDEMO_LINE_CAPACITY, text);
    (void)Console::WriteLine("%s", text);
    consoledemo_set_status(text);
}

/*
 * Format one compact counters line used by the sample overlay.
 *
 * @param destination Output buffer.
 * @param capacity Output buffer capacity in bytes.
 * @return Nothing.
 */
static void consoledemo_format_counters(char* destination, unsigned long capacity) {
    char* cursor = destination;

    if (destination == 0 || capacity == 0UL) {
        return;
    }

    cursor = appendText(cursor, "ticks=");
    cursor = appendUnsignedLong(cursor, g_consoledemo_tick_count);
    cursor = appendText(cursor, " paints=");
    cursor = appendUnsignedLong(cursor, g_consoledemo_paint_count);
    cursor = appendText(cursor, " keys=");
    cursor = appendUnsignedLong(cursor, g_consoledemo_key_count);
    cursor = appendText(cursor, " clicks=");
    cursor = appendUnsignedLong(cursor, g_consoledemo_click_count);
    cursor = appendText(cursor, " moves=");
    cursor = appendUnsignedLong(cursor, g_consoledemo_move_count);
    *cursor = '\0';
}

/*
 * Format one mouse-event description and either store it as the current status
 * or append it to the visible event history.
 *
 * @param message Window message identifier.
 * @param wParam First payload word.
 * @param lParam Packed pointer coordinates.
 * @param append_history Non-zero when the line should enter the history buffer.
 * @return Nothing.
 */
static void consoledemo_record_mouse_event(unsigned long message, unsigned long wParam, unsigned long lParam, int append_history) {
    char line[CONSOLEDEMO_LINE_CAPACITY];
    char* cursor = line;
    long x;
    long y;

    WindowUnpackSignedPair(lParam, &x, &y);
    cursor = appendText(cursor, WindowMessageName(message));
    cursor = appendText(cursor, " x=");
    cursor = consoledemo_append_long(cursor, x);
    cursor = appendText(cursor, " y=");
    cursor = consoledemo_append_long(cursor, y);
    cursor = appendText(cursor, " buttons=");
    cursor = appendUnsignedLong(cursor, wParam);
    *cursor = '\0';

    if (append_history) {
        consoledemo_append_line(line);
    }
    else {
        consoledemo_set_status(line);
    }
}

/*
 * Format one keyboard-event description and append it to the sample history.
 *
 * @param message Window message identifier.
 * @param wParam Virtual key or translated character.
 * @return Nothing.
 */
static void consoledemo_record_key_event(unsigned long message, unsigned long wParam) {
    char line[CONSOLEDEMO_LINE_CAPACITY];
    char* cursor = line;

    cursor = appendText(cursor, WindowMessageName(message));
    cursor = appendText(cursor, " code=");
    cursor = appendUnsignedLong(cursor, wParam);
    if (wParam >= 32UL && wParam <= 126UL) {
        *cursor++ = ' ';
        *cursor++ = '\'';
        *cursor++ = (char)wParam;
        *cursor++ = '\'';
    }
    *cursor = '\0';
    consoledemo_append_line(line);
}

/*
 * Load one default UI font for the sample so successful text rendering is
 * obvious on screen and not just in the serial log.
 *
 * @return Nothing.
 */
static void consoledemo_prepare_font(void) {
    long status;

    if (g_consoledemo_font_ready) {
        return;
    }

    status = GdiLoadFont(0, CONSOLEDEMO_FONT_HEIGHT, &g_consoledemo_font);
    if (status >= 0L) {
        g_consoledemo_font_ready = 1;
        return;
    }

    consoledemo_append_line("consoledemo.exe: GdiLoadFont failed, text drawing disabled");
}

/*
 * Acquire the shared drawing surface for the sample window.
 *
 * @param hwnd Target window handle.
 * @return Zero on success, or a negative status code on failure.
 */
static long consoledemo_acquire_surface(HWND hwnd) {
    long status = GdiGetWindowSurface(hwnd, &g_consoledemo_surface);

    g_consoledemo_surface_ready = status >= 0L;
    return status;
}

/*
 * Release the cached drawing surface when the sample shuts down.
 *
 * @return Nothing.
 */
static void consoledemo_release_surface(void) {
    if (!g_consoledemo_surface_ready || g_consoledemo_window == 0UL) {
        return;
    }

    (void)GdiReleaseWindowSurface(g_consoledemo_window);
    g_consoledemo_surface_ready = 0;
}

/*
 * Draw the current sample state into the shared window surface.
 *
 * @param hwnd Target window handle.
 * @return Nothing.
 */
static void consoledemo_paint(HWND hwnd) {
    char counters_line[CONSOLEDEMO_LINE_CAPACITY];
    unsigned long y;
    unsigned long index;
    unsigned long visible_index;

    if (!g_consoledemo_surface_ready && consoledemo_acquire_surface(hwnd) < 0L) {
        consoledemo_append_line("consoledemo.exe: failed to acquire window surface");
        return;
    }

    ++g_consoledemo_paint_count;
    (void)GdiFillSurfaceRect(&g_consoledemo_surface, 0UL, 0UL, g_consoledemo_surface.width, g_consoledemo_surface.height, 0x00111924UL);
    (void)GdiFillSurfaceRect(&g_consoledemo_surface, CONSOLEDEMO_MARGIN, CONSOLEDEMO_MARGIN, g_consoledemo_surface.width > (CONSOLEDEMO_MARGIN * 2UL) ? (g_consoledemo_surface.width - (CONSOLEDEMO_MARGIN * 2UL)) : g_consoledemo_surface.width, CONSOLEDEMO_HEADER_HEIGHT, 0x003A6EA5UL);
    (void)GdiFillSurfaceRect(&g_consoledemo_surface, CONSOLEDEMO_MARGIN, CONSOLEDEMO_MARGIN + CONSOLEDEMO_HEADER_HEIGHT + 10UL, g_consoledemo_surface.width > (CONSOLEDEMO_MARGIN * 2UL) ? (g_consoledemo_surface.width - (CONSOLEDEMO_MARGIN * 2UL)) : g_consoledemo_surface.width, 2UL, 0x004F6378UL);

    if (g_consoledemo_font_ready) {
        (void)GdiDrawTextSurface(&g_consoledemo_surface, &g_consoledemo_font, CONSOLEDEMO_MARGIN + 12UL, CONSOLEDEMO_MARGIN + 10UL, "Console Port Sample", 0x00FFFFFFUL);
        (void)GdiDrawTextSurface(&g_consoledemo_surface, &g_consoledemo_font, CONSOLEDEMO_MARGIN + 12UL, CONSOLEDEMO_MARGIN + 26UL, "Serial logging uses Console::WriteLine. Mouse, key, timer, and paint state render here.", 0x00E8F2FBUL);
        (void)GdiDrawTextSurface(&g_consoledemo_surface, &g_consoledemo_font, CONSOLEDEMO_MARGIN + 4UL, CONSOLEDEMO_MARGIN + CONSOLEDEMO_HEADER_HEIGHT + 18UL, g_consoledemo_status_line, 0x00FCD34DUL);

        consoledemo_format_counters(counters_line, sizeof(counters_line));
        (void)GdiDrawTextSurface(&g_consoledemo_surface, &g_consoledemo_font, CONSOLEDEMO_MARGIN + 4UL, CONSOLEDEMO_MARGIN + CONSOLEDEMO_HEADER_HEIGHT + 36UL, counters_line, 0x00D6E8FFUL);

        y = CONSOLEDEMO_MARGIN + CONSOLEDEMO_HEADER_HEIGHT + 68UL;
        for (index = 0UL; index < g_consoledemo_line_count; ++index) {
            visible_index = (g_consoledemo_first_line + index) % CONSOLEDEMO_MAX_LINES;
            (void)GdiDrawTextSurface(&g_consoledemo_surface, &g_consoledemo_font, CONSOLEDEMO_MARGIN + 4UL, y, g_consoledemo_lines[visible_index], 0x00E5EDF6UL);
            y += g_consoledemo_font.line_height != 0UL ? g_consoledemo_font.line_height : 16UL;
        }
    }

    (void)GdiInvalidateRect(hwnd, 0UL, 0UL, g_consoledemo_surface.width, g_consoledemo_surface.height);
}

/*
 * Handle the sample window messages and keep both the serial console and the
 * on-screen debug view aligned.
 *
 * @param hwnd Target window handle.
 * @param message Message identifier.
 * @param wParam First payload word.
 * @param lParam Second payload word.
 * @return Always zero for the current sample.
 */
static LRESULT consoledemo_wndproc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    if (message == WM_CREATE) {
        g_consoledemo_window = hwnd;
        consoledemo_prepare_font();
        if (consoledemo_acquire_surface(hwnd) < 0L) {
            consoledemo_append_line("consoledemo.exe: WM_CREATE surface acquisition failed");
        }
        consoledemo_append_line("consoledemo.exe: WM_CREATE received");
        consoledemo_invalidate_window();
        return 0L;
    }

    if (message == WM_PAINT) {
        consoledemo_paint(hwnd);
        return 0L;
    }

    if (message == WM_TIMER) {
        char line[CONSOLEDEMO_LINE_CAPACITY];
        char* cursor = line;

        ++g_consoledemo_tick_count;
        if (g_consoledemo_tick_count <= 3UL || (g_consoledemo_tick_count % 5UL) == 0UL) {
            cursor = appendText(cursor, "WM_TIMER tick=");
            cursor = appendUnsignedLong(cursor, g_consoledemo_tick_count);
            *cursor = '\0';
            consoledemo_append_line(line);
        }
        else {
            cursor = appendText(cursor, "WM_TIMER tick=");
            cursor = appendUnsignedLong(cursor, g_consoledemo_tick_count);
            *cursor = '\0';
            consoledemo_set_status(line);
        }

        return 0L;
    }

    if (message == WM_MOUSEMOVE) {
        ++g_consoledemo_move_count;
        consoledemo_record_mouse_event(message, wParam, lParam, 0);
        return 0L;
    }

    if (message == WM_MOUSELEAVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_MOUSECLICKED) {
        if (message == WM_MOUSECLICKED) {
            ++g_consoledemo_click_count;
        }
        consoledemo_record_mouse_event(message, wParam, lParam, 1);
        return 0L;
    }

    if (message == WM_KEYDOWN || message == WM_KEYUP) {
        ++g_consoledemo_key_count;
        consoledemo_record_key_event(message, wParam);
        return 0L;
    }

    if (message == WM_SIZE) {
        consoledemo_release_surface();
        if (consoledemo_acquire_surface(hwnd) >= 0L) {
            char line[CONSOLEDEMO_LINE_CAPACITY];
            char* cursor = line;

            cursor = appendText(cursor, "WM_SIZE width=");
            cursor = appendUnsignedLong(cursor, g_consoledemo_surface.width);
            cursor = appendText(cursor, " height=");
            cursor = appendUnsignedLong(cursor, g_consoledemo_surface.height);
            *cursor = '\0';
            consoledemo_append_line(line);
        }
        return 0L;
    }

    if (message == WM_CLOSE) {
        consoledemo_append_line("consoledemo.exe: WM_CLOSE received");
        (void)PostQuitMessage(0L);
        return 0L;
    }

    return 0L;
}

/*
 * Create the console-demo window and drive the standard message loop.
 *
 * @return Zero on success, or a non-zero failure code.
 */
int main(void) {
    MSG message;
    WindowCreateParams params;
    long get_result;

    (void)Console::WriteLine(Console::Colors::Cyan, "consoledemo.exe: startup");
    (void)Console::WriteLine("consoledemo.exe: format test dec=%d hex=0x%X ptr=%p", 42, 0xBEEF, (void*)&main);

    if (CreateWindowClass(CONSOLEDEMO_CLASS_NAME, consoledemo_wndproc) < 0L) {
        (void)Console::WriteLine(Console::Colors::BrightRed, "consoledemo.exe: CreateWindowClass failed");
        return 1;
    }

    params.class_name = CONSOLEDEMO_CLASS_NAME;
    params.title = CONSOLEDEMO_WINDOW_TITLE;
    params.parent = 0UL;
    params.x = CONSOLEDEMO_WINDOW_X;
    params.y = CONSOLEDEMO_WINDOW_Y;
    params.width = CONSOLEDEMO_WINDOW_WIDTH;
    params.height = CONSOLEDEMO_WINDOW_HEIGHT;
    params.style = ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_DECORATED | ROS_WINDOW_STYLE_BORDER;

    g_consoledemo_window = CreateWindowEx(&params);
    if (g_consoledemo_window == 0UL) {
        (void)Console::WriteLine(Console::Colors::BrightRed, "consoledemo.exe: CreateWindowEx failed");
        return 2;
    }
    g_consoledemo_timer_id = SetTimer(g_consoledemo_window, 0UL, CONSOLEDEMO_TIMER_MSEC);
    if (g_consoledemo_timer_id == 0UL) {
        consoledemo_append_line("consoledemo.exe: timer registration failed");
    }

    consoledemo_append_line("consoledemo.exe: window created");
    consoledemo_append_line("Move the pointer, click, type keys, and watch WM_TIMER pulses");

    for (;;) {
        get_result = GetMessage(&message);
        if (get_result <= 0L) {
            break;
        }

        (void)TranslateMessage(&message);
        (void)DispatchMessage(&message);
    }

    consoledemo_release_surface();
    if (g_consoledemo_window != 0UL && g_consoledemo_timer_id != 0UL) {
        (void)KillTimer(g_consoledemo_window, g_consoledemo_timer_id);
        g_consoledemo_timer_id = 0UL;
    }
    if (g_consoledemo_font_ready) {
        (void)GdiUnloadFont(&g_consoledemo_font);
        g_consoledemo_font_ready = 0;
    }

    (void)Console::WriteLine(Console::Colors::Green, "consoledemo.exe: message loop exited");
    return 0;
}