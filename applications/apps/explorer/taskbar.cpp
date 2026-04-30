/*
 * taskbar.cpp
 *
 * Owner-drawn Explorer taskbar with an XP-inspired flat shell treatment.
 */

#define ROS_APP_USE_EXPLORER_SHELL 1
#define ROS_APP_USE_WINDOW 1
#define ROS_WINDOW_NO_IMPORTS 1
#define ROS_GDI_NO_IMPORTS 1
#define ROS_PNG_NO_IMPORTS 1

#include "explorer.h"
#include "generated/start_icon.h"
#include "app/app.h"
#include <string.h>

#define EXPLORER_TASKBAR_HEIGHT 40UL
#define EXPLORER_TASKBAR_TIMER_MSEC 200UL
#define EXPLORER_TASKBAR_FONT_PATH "C:\\fonts\\tahoma-14.rtf"
#define EXPLORER_TASKBAR_ICON_PATH "C:\\icons\\start_menu.png"
#define EXPLORER_TASKBAR_INVALID_INDEX 0xFFFFFFFFUL
#define EXPLORER_TASKBAR_MENU_CMD_SHELL 0x2001UL
#define EXPLORER_TASKBAR_MENU_CMD_WIDGETDEMO 0x2002UL
#define EXPLORER_TASKBAR_MENU_CMD_GUISAMPLE 0x2003UL
#define EXPLORER_TASKBAR_MENU_CMD_IMAGEBOX 0x2004UL
#define EXPLORER_TASKBAR_MENU_CMD_TTFDEMO 0x2005UL
#define EXPLORER_TASKBAR_MENU_CMD_TOGGLE_START 0x2006UL

#define TASKBAR_COLOR_BAR_TOP 0x00F4FAFFUL
#define TASKBAR_COLOR_BAR_MID 0x00D9E9FAUL
#define TASKBAR_COLOR_BAR_BOTTOM 0x0097BCE4UL
#define TASKBAR_COLOR_BAR_BORDER 0x00587EA6UL
#define TASKBAR_COLOR_BAR_HILITE 0x00FFFFFFUL

#define TASKBAR_COLOR_START_NORMAL 0x0090CB5FUL
#define TASKBAR_COLOR_START_HOVER 0x00A6DA73UL
#define TASKBAR_COLOR_START_PRESSED 0x0079B54AUL
#define TASKBAR_COLOR_START_BORDER 0x004E7D29UL

#define TASKBAR_COLOR_TASK_NORMAL 0x00DCEAF9UL
#define TASKBAR_COLOR_TASK_HOVER 0x00EFF6FEUL
#define TASKBAR_COLOR_TASK_ACTIVE 0x00F8FBFFUL
#define TASKBAR_COLOR_TASK_PRESSED 0x00C7DCF2UL
#define TASKBAR_COLOR_TASK_BORDER 0x00759CC5UL

#define TASKBAR_COLOR_TRAY_BG 0x00EAF3FCUL
#define TASKBAR_COLOR_TRAY_BORDER 0x0087AACCUL
#define TASKBAR_COLOR_TEXT 0x001D3550UL
#define TASKBAR_COLOR_TEXT_DIM 0x004E6A87UL

typedef enum TaskbarHitPart {
    TASKBAR_HIT_NONE = 0UL,
    TASKBAR_HIT_START = 1UL,
    TASKBAR_HIT_TASK = 2UL,
    TASKBAR_HIT_TRAY = 3UL,
} TaskbarHitPart;

typedef struct TaskbarRect {
    unsigned long x;
    unsigned long y;
    unsigned long width;
    unsigned long height;
} TaskbarRect;

typedef struct TaskbarHitResult {
    unsigned long part;
    unsigned long task_index;
} TaskbarHitResult;

typedef struct TaskbarPngIcon {
    unsigned long width;
    unsigned long height;
    U32* pixels;
    long load_status;
} TaskbarPngIcon;

typedef struct Taskbar {
    HWND hwnd;
    unsigned long width;
    unsigned long height;
    RosGdiFont font;
    int font_ready;
    int start_menu_visible;
    HMENU tray_menu;
    HMENU demos_menu;
    TaskbarPngIcon start_icon;
    RosExplorerShellSharedState* shell_state;
    uint64_t last_task_generation;
    uint64_t last_shell_generation;
    unsigned long hover_part;
    unsigned long hover_task_index;
    unsigned long pressed_part;
    unsigned long pressed_task_index;
} Taskbar;

static Taskbar g_taskbar = { 0 };

/*
 * Park the current taskbar thread forever.
 *
 * Userspace helper threads must not return from their entrypoint because the
 * current runtime has no safe thread-return trampoline.
 *
 * @return Nothing.
 */
static void taskbar_thread_park_forever(void) {
    for (;;) {
        (void)sleepMs(1000UL);
    }
}

/*
 * Return the smaller of two unsigned values.
 *
 * @param lhs Left-hand value.
 * @param rhs Right-hand value.
 * @return Smaller input value.
 */
static unsigned long taskbar_min_unsigned(unsigned long lhs, unsigned long rhs) {
    return lhs < rhs ? lhs : rhs;
}

/*
 * Return the larger of two unsigned values.
 *
 * @param lhs Left-hand value.
 * @param rhs Right-hand value.
 * @return Larger input value.
 */
static unsigned long taskbar_max_unsigned(unsigned long lhs, unsigned long rhs) {
    return lhs > rhs ? lhs : rhs;
}

/*
 * Initialize one rectangle record.
 *
 * @param rect Rectangle to overwrite.
 * @param x New X coordinate.
 * @param y New Y coordinate.
 * @param width New width.
 * @param height New height.
 * @return Nothing.
 */
static inline void taskbar_set_rect(TaskbarRect* rect, unsigned long x, unsigned long y, unsigned long width, unsigned long height) {
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
 * @param x Screen-relative X coordinate.
 * @param y Screen-relative Y coordinate.
 * @return Non-zero when the point lies inside the rectangle.
 */
static int taskbar_rect_contains(const TaskbarRect* rect, long x, long y) {
    if ((rect == NULL) || (rect->width == 0UL) || (rect->height == 0UL)) {
        return 0;
    }

    return x >= (long)rect->x
        && y >= (long)rect->y
        && x < (long)(rect->x + rect->width)
        && y < (long)(rect->y + rect->height);
}

/*
 * Copy one ASCII string into a fixed caller buffer.
 *
 * @param destination Destination buffer.
 * @param capacity Destination capacity in bytes.
 * @param source Null-terminated source string.
 * @return Nothing.
 */
static void taskbar_copy_text(char* destination, unsigned long capacity, const char* source) {
    unsigned long index = 0UL;

    if ((destination == NULL) || (capacity == 0UL)) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    while ((source[index] != '\0') && ((index + 1UL) < capacity)) {
        destination[index] = source[index];
        ++index;
    }

    destination[index] = '\0';
}

/*
 * Return the length of one small ASCII string.
 *
 * @param text Null-terminated string.
 * @return Character count excluding the terminator.
 */
static unsigned long taskbar_text_length(const char* text) {
    unsigned long length = 0UL;

    if (text == NULL) {
        return 0UL;
    }

    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

/*
 * Return one normalized RGB surface color for the active pixel format.
 *
 * @param pixel_format Target surface pixel format.
 * @param color RGB color in `0x00RRGGBB` form.
 * @return Encoded surface pixel value.
 */
static unsigned long taskbar_encode_color(unsigned long pixel_format, unsigned long color) {
    if (pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
        return color;
    }

    return ((color & 0x000000FFUL) << 16)
        | (color & 0x0000FF00UL)
        | ((color & 0x00FF0000UL) >> 16);
}

/*
 * Decode one mapped surface pixel back into canonical RGB order.
 *
 * @param pixel_format Source surface pixel format.
 * @param color Encoded surface pixel value.
 * @return RGB color in `0x00RRGGBB` form.
 */
static unsigned long taskbar_decode_color(unsigned long pixel_format, unsigned long color) {
    if (pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
        return color;
    }

    return ((color & 0x000000FFUL) << 16)
        | (color & 0x0000FF00UL)
        | ((color & 0x00FF0000UL) >> 16);
}

/*
 * Blend one RGB foreground pixel over one RGB background pixel.
 *
 * @param background Existing RGB background color.
 * @param foreground New RGB foreground color.
 * @param alpha Foreground alpha in the range 0..255.
 * @return Blended RGB color.
 */
static unsigned long taskbar_blend_rgb(unsigned long background, unsigned long foreground, unsigned int alpha) {
    const unsigned int inverse_alpha = 255U - alpha;
    const unsigned int red = (unsigned int)(((((background >> 16) & 0xFFUL) * inverse_alpha) + (((foreground >> 16) & 0xFFUL) * alpha) + 127U) / 255U);
    const unsigned int green = (unsigned int)(((((background >> 8) & 0xFFUL) * inverse_alpha) + (((foreground >> 8) & 0xFFUL) * alpha) + 127U) / 255U);
    const unsigned int blue = (unsigned int)((((background & 0xFFUL) * inverse_alpha) + ((foreground & 0xFFUL) * alpha) + 127U) / 255U);

    if (alpha >= 255U) {
        return foreground;
    }
    if (alpha == 0U) {
        return background;
    }

    return ((unsigned long)red << 16) | ((unsigned long)green << 8) | (unsigned long)blue;
}

/*
 * Release one cached PNG icon buffer.
 *
 * @param icon Icon cache entry to destroy.
 * @return Nothing.
 */
static void taskbar_free_icon(TaskbarPngIcon* icon) {
    if ((icon != NULL) && (icon->pixels != NULL)) {
        user_shared_heap_free(icon->pixels);
        icon->pixels = NULL;
        icon->width = 0UL;
        icon->height = 0UL;
    }
}

/*
 * Map the shared shell state if GWES already published it.
 *
 * @return Non-zero when the shared state is available and version-compatible.
 */
static int taskbar_ensure_shell_state(void) {
    if ((g_taskbar.shell_state != NULL)
        && (g_taskbar.shell_state->version == ROS_EXPLORER_SHELL_SHARED_STATE_VERSION)
        && (ExplorerShellTaskCapacity(g_taskbar.shell_state) != 0UL)) {
        return 1;
    }

    g_taskbar.shell_state = ExplorerShellSharedState(0UL);
    if ((g_taskbar.shell_state == NULL)
        || (g_taskbar.shell_state->version != ROS_EXPLORER_SHELL_SHARED_STATE_VERSION)
        || (ExplorerShellTaskCapacity(g_taskbar.shell_state) == 0UL)) {
        g_taskbar.shell_state = NULL;
        return 0;
    }

    return 1;
}

/*
 * Return the current Start-menu window handle published through shared shell state.
 *
 * The taskbar already synchronizes with the popup through the shared shell
 * snapshot, so reading the handle from that authoritative state avoids an
 * extra cross-translation-unit symbol dependency during explorer linking.
 *
 * @return Published Start-menu window handle, or zero when the popup is hidden.
 */
static HWND taskbar_shared_start_menu_window(void) {
    if (!taskbar_ensure_shell_state()) {
        return 0UL;
    }

    return (HWND)g_taskbar.shell_state->start_menu_hwnd;
}

/*
 * Publish taskbar-owned shell visibility fields into the shared shell state.
 *
 * @param visible Non-zero when the taskbar should be reported as visible.
 * @return Nothing.
 */
static void taskbar_publish_shell_state(int visible) {
    RosExplorerShellSharedState* state;

    if (!taskbar_ensure_shell_state()) {
        return;
    }

    state = g_taskbar.shell_state;
    state->taskbar_hwnd = visible ? (uint64_t)g_taskbar.hwnd : 0ULL;
    state->taskbar_visible = visible ? 1U : 0U;
    state->taskbar_height = visible ? (uint32_t)g_taskbar.height : 0U;
    state->start_menu_visible = g_taskbar.start_menu_visible ? 1U : 0U;
    state->start_menu_hwnd = g_taskbar.start_menu_visible ? (uint64_t)taskbar_shared_start_menu_window() : 0ULL;
    ++state->shell_generation;
    g_taskbar.last_shell_generation = state->shell_generation;
}

/*
 * Return the number of published task entries that Explorer should render.
 *
 * @return Published task count clipped to the shared buffer capacity.
 */
static unsigned long taskbar_task_count(void) {
    unsigned long task_count;
    unsigned long task_capacity;

    if (!taskbar_ensure_shell_state()) {
        return 0UL;
    }

    task_capacity = ExplorerShellTaskCapacity(g_taskbar.shell_state);
    task_count = (unsigned long)g_taskbar.shell_state->task_count;
    return task_count < task_capacity ? task_count : task_capacity;
}

/*
 * Return the preferred label for one task button.
 *
 * @param index Published task index.
 * @return Task title, fallback class name, or a stable placeholder.
 */
static const char* taskbar_task_label(unsigned long index) {
    RosExplorerShellTaskEntry* entry;

    if (!taskbar_ensure_shell_state() || index >= taskbar_task_count()) {
        return "Task";
    }

    entry = &g_taskbar.shell_state->tasks[index];
    if (entry->title[0] != '\0') {
        return entry->title;
    }
    if (entry->class_name[0] != '\0') {
        return entry->class_name;
    }
    return "Task";
}

/*
 * Return whether one published task is the current foreground entry.
 *
 * @param index Published task index.
 * @return Non-zero when the task is active.
 */
static int taskbar_task_is_active(unsigned long index) {
    if (!taskbar_ensure_shell_state() || index >= taskbar_task_count()) {
        return 0;
    }

    return g_taskbar.shell_state->tasks[index].hwnd == g_taskbar.shell_state->foreground_hwnd;
}

/*
 * Return the rendered width of one text run.
 *
 * @param text Null-terminated text.
 * @return Text width in pixels.
 */
static unsigned long taskbar_measure_text_width(const char* text) {
    unsigned long width = 0UL;
    unsigned long height = 0UL;

    if (!g_taskbar.font_ready || text == NULL || text[0] == '\0') {
        return 0UL;
    }
    if (ExplorerGdiMeasureText(&g_taskbar.font, text, &width, &height) < 0L) {
        return 0UL;
    }

    return width;
}

/*
 * Truncate one label with an ellipsis so it fits a taskbar button.
 *
 * @param source Source label.
 * @param max_width Maximum rendered width in pixels.
 * @param destination Destination buffer.
 * @param capacity Destination capacity in bytes.
 * @return Nothing.
 */
static void taskbar_fit_text(const char* source, unsigned long max_width, char* destination, unsigned long capacity) {
    unsigned long source_length;
    unsigned long index;

    if ((destination == NULL) || (capacity == 0UL)) {
        return;
    }

    taskbar_copy_text(destination, capacity, source);
    if (destination[0] == '\0' || !g_taskbar.font_ready) {
        return;
    }
    if (taskbar_measure_text_width(destination) <= max_width) {
        return;
    }
    if (capacity <= 4UL) {
        destination[0] = '\0';
        return;
    }

    source_length = taskbar_text_length(source);
    for (index = source_length; index > 0UL; --index) {
        unsigned long copy_length = index < (capacity - 4UL) ? index : (capacity - 4UL);

        memcopy(destination, source, copy_length);
        destination[copy_length] = '\0';
        taskbar_copy_text(destination + copy_length, capacity - copy_length, "...");
        if (taskbar_measure_text_width(destination) <= max_width) {
            return;
        }
    }

    taskbar_copy_text(destination, capacity, "...");
}

/*
 * Build the compact tray text for the right-side shell area.
 *
 * @param destination Destination buffer.
 * @param capacity Destination capacity in bytes.
 * @return Nothing.
 */
static void taskbar_build_tray_text(char* destination, unsigned long capacity) {
    char* cursor;

    if ((destination == NULL) || (capacity == 0UL)) {
        return;
    }

    cursor = destination;
    if (g_taskbar.start_menu_visible) {
        cursor = appendText(cursor, "Menu");
    }
    else {
        cursor = appendUnsignedLong(appendText(cursor, "Apps "), taskbar_task_count());
    }
    *cursor = '\0';
}

/*
 * Request one full taskbar repaint.
 *
 * @return Nothing.
 */
static void taskbar_invalidate_full(void) {
    if (g_taskbar.hwnd == 0UL) {
        return;
    }

    (void)ExplorerGdiInvalidateRect(
        g_taskbar.hwnd,
        0UL,
        0UL,
        g_taskbar.width != 0UL ? g_taskbar.width : 800UL,
        g_taskbar.height != 0UL ? g_taskbar.height : EXPLORER_TASKBAR_HEIGHT);
}

/*
 * Fill one rectangle on the mapped taskbar surface.
 *
 * @param surface Target taskbar surface.
 * @param rect Rectangle to fill.
 * @param color RGB fill color.
 * @return Nothing.
 */
static void taskbar_fill_rect(const RosGdiSurface* surface, const TaskbarRect* rect, unsigned long color) {
    if ((surface == NULL) || (rect == NULL) || (rect->width == 0UL) || (rect->height == 0UL)) {
        return;
    }

    (void)ExplorerGdiFillSurfaceRect(
        surface,
        rect->x,
        rect->y,
        rect->width,
        rect->height,
        taskbar_encode_color(surface->pixel_format, color));
}

/*
 * Fill one raw rectangle on the mapped taskbar surface.
 *
 * @param surface Target taskbar surface.
 * @param x Rectangle X coordinate.
 * @param y Rectangle Y coordinate.
 * @param width Rectangle width.
 * @param height Rectangle height.
 * @param color RGB fill color.
 * @return Nothing.
 */
static void taskbar_fill_box(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color) {
    TaskbarRect rect;

    taskbar_set_rect(&rect, x, y, width, height);
    taskbar_fill_rect(surface, &rect, color);
}

/*
 * Draw a one-pixel frame around one rectangle.
 *
 * @param surface Target taskbar surface.
 * @param rect Rectangle to frame.
 * @param color RGB frame color.
 * @return Nothing.
 */
static void taskbar_frame_rect(const RosGdiSurface* surface, const TaskbarRect* rect, unsigned long color) {
    if ((rect == NULL) || (rect->width < 2UL) || (rect->height < 2UL)) {
        return;
    }

    taskbar_fill_box(surface, rect->x, rect->y, rect->width, 1UL, color);
    taskbar_fill_box(surface, rect->x, rect->y + rect->height - 1UL, rect->width, 1UL, color);
    taskbar_fill_box(surface, rect->x, rect->y, 1UL, rect->height, color);
    taskbar_fill_box(surface, rect->x + rect->width - 1UL, rect->y, 1UL, rect->height, color);
}

/*
 * Draw a soft inner highlight around one rectangle.
 *
 * @param surface Target taskbar surface.
 * @param rect Rectangle to accent.
 * @param color RGB highlight color.
 * @return Nothing.
 */
static void taskbar_inner_highlight(const RosGdiSurface* surface, const TaskbarRect* rect, unsigned long color) {
    if ((rect == NULL) || (rect->width < 4UL) || (rect->height < 4UL)) {
        return;
    }

    taskbar_fill_box(surface, rect->x + 1UL, rect->y + 1UL, rect->width - 2UL, 1UL, color);
    taskbar_fill_box(surface, rect->x + 1UL, rect->y + 1UL, 1UL, rect->height - 2UL, color);
}

/*
 * Alpha-blend one cached ARGB PNG icon into the live taskbar surface.
 *
 * @param surface Destination window surface.
 * @param icon Cached icon bitmap.
 * @param x Destination X coordinate.
 * @param y Destination Y coordinate.
 * @param target_size Target square size in pixels.
 * @return Nothing.
 */
static void taskbar_blit_icon(const RosGdiSurface* surface, const TaskbarPngIcon* icon, unsigned long x, unsigned long y, unsigned long target_size) {
    unsigned long draw_width;
    unsigned long draw_height;
    unsigned long row;
    unsigned long column;
    unsigned long* surface_pixels;

    if ((surface == NULL) || (icon == NULL) || (icon->pixels == NULL) || (surface->pixels == NULL) || (surface->pitch < (surface->width * 4UL))) {
        return;
    }

    /*
     * The staged Start button asset is larger than the taskbar glyph slot, so
     * copying the top-left `target_size` pixels would sample only transparent
     * padding and make the icon appear missing. Scale the full decoded ARGB
     * image into the requested square instead so the visible logo survives.
     */
    draw_width = target_size != 0UL ? target_size : icon->width;
    draw_height = target_size != 0UL ? target_size : icon->height;
    surface_pixels = (unsigned long*)surface->pixels;
    for (row = 0UL; row < draw_height; ++row) {
        const unsigned long source_row = (icon->height > 1UL && draw_height > 1UL)
            ? ((row * (icon->height - 1UL)) / (draw_height - 1UL))
            : 0UL;

        if ((y + row) >= surface->height) {
            break;
        }

        for (column = 0UL; column < draw_width; ++column) {
            const unsigned long source_column = (icon->width > 1UL && draw_width > 1UL)
                ? ((column * (icon->width - 1UL)) / (draw_width - 1UL))
                : 0UL;
            const unsigned long source_pixel = icon->pixels[(source_row * icon->width) + source_column];
            const unsigned int alpha = (unsigned int)((source_pixel >> 24) & 0xFFUL);
            unsigned long* destination_pixel;
            unsigned long background_rgb;
            unsigned long foreground_rgb;

            if ((x + column) >= surface->width || alpha == 0U) {
                continue;
            }

            destination_pixel = (unsigned long*)((unsigned char*)surface_pixels + ((y + row) * surface->pitch) + ((x + column) * 4UL));
            background_rgb = taskbar_decode_color(surface->pixel_format, *destination_pixel);
            foreground_rgb = source_pixel & 0x00FFFFFFUL;
            *destination_pixel = taskbar_encode_color(
                surface->pixel_format,
                taskbar_blend_rgb(background_rgb, foreground_rgb, alpha));
        }
    }
}

/*
 * Blend the generated in-binary Explorer start glyph into one live surface.
 *
 * The staged PNG is preferred, but the taskbar should still show one visible
 * Start icon when PNG decode is unavailable. The generated alpha mask keeps a
 * guaranteed fallback inside explorer.exe itself.
 *
 * @param surface Destination taskbar surface.
 * @param x Destination X coordinate.
 * @param y Destination Y coordinate.
 * @param color RGB icon color.
 * @return Nothing.
 */
static void taskbar_blit_builtin_start_icon(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long color) {
    unsigned long row;

    if (surface == NULL || surface->pixels == NULL || surface->pitch < (surface->width * 4UL)) {
        return;
    }

    for (row = 0UL; row < 16UL; ++row) {
        unsigned long column;

        if ((y + row) >= surface->height) {
            break;
        }

        for (column = 0UL; column < 16UL; ++column) {
            const unsigned int alpha = g_start_icon_alpha[(row * 16UL) + column];
            unsigned long* destination_pixel;
            unsigned long background_rgb;

            if ((x + column) >= surface->width || alpha == 0U) {
                continue;
            }

            destination_pixel = (unsigned long*)((unsigned char*)surface->pixels + ((y + row) * surface->pitch) + ((x + column) * 4UL));
            background_rgb = taskbar_decode_color(surface->pixel_format, *destination_pixel);
            *destination_pixel = taskbar_encode_color(
                surface->pixel_format,
                taskbar_blend_rgb(background_rgb, color, alpha));
        }
    }
}

/*
 * Load the fallback UI font used by the taskbar chrome.
 *
 * @return Nothing.
 */
static void taskbar_prepare_font(void) {
    if (g_taskbar.font_ready) {
        return;
    }

    if (ExplorerGdiLoadFont(EXPLORER_TASKBAR_FONT_PATH, 14UL, &g_taskbar.font) >= 0L
        || ExplorerGdiLoadFont(0, 14UL, &g_taskbar.font) >= 0L) {
        g_taskbar.font_ready = 1;
        return;
    }

    writeLog("explorer.taskbar: shell font load failed");
}

/*
 * Decode the staged PNG start icon into shared-heap scratch storage.
 *
 * @return Nothing.
 */
static void taskbar_prepare_icon(void) {
    RosPngMemory memory;
    U32 width = 0U;
    U32 height = 0U;

    if (g_taskbar.start_icon.pixels != NULL) {
        return;
    }

    g_taskbar.start_icon.load_status = ExplorerPngGetInfo(EXPLORER_TASKBAR_ICON_PATH, &width, &height);
    if (g_taskbar.start_icon.load_status < 0L || width == 0U || height == 0U) {
        writeLog("explorer.taskbar: staged start icon unavailable, using built-in fallback");
        return;
    }

    g_taskbar.start_icon.pixels = (U32*)user_shared_heap_malloc((size_t)width * (size_t)height * sizeof(U32));
    if (g_taskbar.start_icon.pixels == NULL) {
        g_taskbar.start_icon.load_status = ROS_USER_IPC_STATUS_NO_SPACE;
        return;
    }

    memory.x = 0U;
    memory.y = 0U;
    memory.width = width;
    memory.height = height;
    memory.buffer = g_taskbar.start_icon.pixels;
    g_taskbar.start_icon.load_status = ExplorerPngRenderFileToMemory(EXPLORER_TASKBAR_ICON_PATH, &memory);
    if (g_taskbar.start_icon.load_status < 0L) {
        taskbar_free_icon(&g_taskbar.start_icon);
        writeLog("explorer.taskbar: staged start icon decode failed, using built-in fallback");
        return;
    }

    g_taskbar.start_icon.width = (unsigned long)width;
    g_taskbar.start_icon.height = (unsigned long)height;
}

/*
 * Return the taskbar strip rectangles for the current surface size.
 *
 * @param surface Taskbar surface.
 * @param start_button_rect Receives the start-button bounds.
 * @param task_strip_rect Receives the task-button strip bounds.
 * @param tray_rect Receives the right-side tray bounds.
 * @return Nothing.
 */
static void taskbar_layout(const RosGdiSurface* surface, TaskbarRect* start_button_rect, TaskbarRect* task_strip_rect, TaskbarRect* tray_rect) {
    const unsigned long outer_padding = 7UL;
    const unsigned long section_gap = 8UL;
    const unsigned long button_height = surface->height > 12UL ? (surface->height - 12UL) : surface->height;
    const unsigned long content_y = (surface->height - button_height) / 2UL;
    const unsigned long start_width = 108UL;
    const unsigned long tray_width = 112UL;
    const unsigned long start_x = outer_padding;
    const unsigned long tray_x = surface->width > (outer_padding + tray_width) ? (surface->width - outer_padding - tray_width) : 0UL;
    const unsigned long task_x = start_x + start_width + section_gap;
    const unsigned long task_width = tray_x > (task_x + section_gap) ? (tray_x - task_x - section_gap) : 0UL;

    taskbar_set_rect(start_button_rect, start_x, content_y, start_width, button_height);
    taskbar_set_rect(task_strip_rect, task_x, content_y, task_width, button_height);
    taskbar_set_rect(tray_rect, tray_x, content_y, tray_width, button_height);
}

/*
 * Return the rectangle for one task button inside the current task strip.
 *
 * @param task_strip_rect Task strip bounds.
 * @param task_count Published task count.
 * @param task_index Target task index.
 * @param rect_out Receives the button rectangle.
 * @return Non-zero when the rectangle was computed successfully.
 */
static int taskbar_task_button_rect(
    const TaskbarRect* task_strip_rect,
    unsigned long task_count,
    unsigned long task_index,
    TaskbarRect* rect_out) {

    const unsigned long gap = task_count > 4UL ? 4UL : 6UL;
    unsigned long total_gap;
    unsigned long button_width;
    unsigned long button_x;

    if ((task_strip_rect == NULL)
        || (rect_out == NULL)
        || (task_count == 0UL)
        || (task_index >= task_count)
        || (task_strip_rect->width == 0UL)) {
        return 0;
    }

    total_gap = task_count > 1UL ? (gap * (task_count - 1UL)) : 0UL;
    if (task_strip_rect->width <= total_gap) {
        return 0;
    }

    button_width = (task_strip_rect->width - total_gap) / task_count;
    if (button_width == 0UL) {
        return 0;
    }

    button_x = task_strip_rect->x + (task_index * (button_width + gap));
    if (task_index == (task_count - 1UL)) {
        button_width = (task_strip_rect->x + task_strip_rect->width) - button_x;
    }

    taskbar_set_rect(rect_out, button_x, task_strip_rect->y, button_width, task_strip_rect->height);
    return 1;
}

/*
 * Return the current hovered or pressed hit target.
 *
 * @param surface Current taskbar surface.
 * @param x Pointer X coordinate inside the taskbar window.
 * @param y Pointer Y coordinate inside the taskbar window.
 * @return Hit-test result for the pointer location.
 */
static TaskbarHitResult taskbar_hit_test(const RosGdiSurface* surface, long x, long y) {
    TaskbarHitResult result;
    TaskbarRect start_button_rect;
    TaskbarRect task_strip_rect;
    TaskbarRect tray_rect;
    unsigned long task_count;
    unsigned long index;

    result.part = TASKBAR_HIT_NONE;
    result.task_index = EXPLORER_TASKBAR_INVALID_INDEX;
    taskbar_layout(surface, &start_button_rect, &task_strip_rect, &tray_rect);
    if (taskbar_rect_contains(&start_button_rect, x, y)) {
        result.part = TASKBAR_HIT_START;
        return result;
    }
    if (taskbar_rect_contains(&tray_rect, x, y)) {
        result.part = TASKBAR_HIT_TRAY;
        return result;
    }

    task_count = taskbar_task_count();
    for (index = 0UL; index < task_count; ++index) {
        TaskbarRect task_rect;

        if (!taskbar_task_button_rect(&task_strip_rect, task_count, index, &task_rect)) {
            continue;
        }
        if (taskbar_rect_contains(&task_rect, x, y)) {
            result.part = TASKBAR_HIT_TASK;
            result.task_index = index;
            return result;
        }
    }

    return result;
}

/*
 * Draw one text run when the taskbar font is available.
 *
 * @param surface Target window surface.
 * @param x Text X coordinate.
 * @param y Text Y coordinate.
 * @param text Null-terminated text.
 * @param color RGB text color.
 * @return Nothing.
 */
static void taskbar_draw_text(const RosGdiSurface* surface, unsigned long x, unsigned long y, const char* text, unsigned long color) {
    if (!g_taskbar.font_ready || surface == NULL || text == NULL || text[0] == '\0') {
        return;
    }

    (void)ExplorerGdiDrawTextSurface(surface, &g_taskbar.font, x, y, text, color);
}

/*
 * Draw the XP-inspired taskbar background bands.
 *
 * @param surface Target window surface.
 * @return Nothing.
 */
static void taskbar_draw_background(const RosGdiSurface* surface) {
    taskbar_fill_box(surface, 0UL, 0UL, surface->width, 6UL, TASKBAR_COLOR_BAR_TOP);
    taskbar_fill_box(surface, 0UL, 6UL, surface->width, surface->height > 12UL ? (surface->height - 12UL) : 0UL, TASKBAR_COLOR_BAR_MID);
    taskbar_fill_box(surface, 0UL, surface->height > 6UL ? (surface->height - 6UL) : 0UL, surface->width, 6UL, TASKBAR_COLOR_BAR_BOTTOM);
    taskbar_fill_box(surface, 0UL, 0UL, surface->width, 1UL, TASKBAR_COLOR_BAR_HILITE);
    taskbar_fill_box(surface, 0UL, surface->height - 1UL, surface->width, 1UL, TASKBAR_COLOR_BAR_BORDER);
}

/*
 * Draw the Start button with XP-style green chrome and PNG icon.
 *
 * @param surface Target taskbar surface.
 * @param rect Start-button bounds.
 * @return Nothing.
 */
static void taskbar_draw_start_button(const RosGdiSurface* surface, const TaskbarRect* rect) {
    unsigned long fill_color = TASKBAR_COLOR_START_NORMAL;
    unsigned long text_color = TASKBAR_COLOR_TEXT;

    if (g_taskbar.pressed_part == TASKBAR_HIT_START) {
        fill_color = TASKBAR_COLOR_START_PRESSED;
    }
    else if (g_taskbar.hover_part == TASKBAR_HIT_START) {
        fill_color = TASKBAR_COLOR_START_HOVER;
    }

    taskbar_fill_rect(surface, rect, fill_color);
    taskbar_frame_rect(surface, rect, TASKBAR_COLOR_START_BORDER);
    taskbar_inner_highlight(surface, rect, TASKBAR_COLOR_BAR_HILITE);
    if (g_taskbar.start_icon.pixels != NULL) {
        taskbar_blit_icon(surface, &g_taskbar.start_icon, rect->x + 8UL, rect->y + ((rect->height - 16UL) / 2UL), 16UL);
    }
    else {
        taskbar_blit_builtin_start_icon(surface, rect->x + 8UL, rect->y + ((rect->height - 16UL) / 2UL), TASKBAR_COLOR_TEXT);
    }

    if (g_taskbar.start_menu_visible) {
        text_color = TASKBAR_COLOR_BAR_HILITE;
    }
    taskbar_draw_text(surface, rect->x + 31UL, rect->y + 6UL, "Start", text_color);
}

/*
 * Draw one owner-drawn task button.
 *
 * @param surface Target taskbar surface.
 * @param rect Button bounds.
 * @param task_index Published task index.
 * @return Nothing.
 */
static void taskbar_draw_task_button(const RosGdiSurface* surface, const TaskbarRect* rect, unsigned long task_index) {
    char fitted_text[ROS_WINDOW_TITLE_MAX];
    unsigned long fill_color = TASKBAR_COLOR_TASK_NORMAL;
    unsigned long text_x = rect->x + 10UL;
    unsigned long text_width = rect->width > 18UL ? (rect->width - 18UL) : rect->width;

    if (g_taskbar.pressed_part == TASKBAR_HIT_TASK && g_taskbar.pressed_task_index == task_index) {
        fill_color = TASKBAR_COLOR_TASK_PRESSED;
    }
    else if (taskbar_task_is_active(task_index)) {
        fill_color = TASKBAR_COLOR_TASK_ACTIVE;
    }
    else if (g_taskbar.hover_part == TASKBAR_HIT_TASK && g_taskbar.hover_task_index == task_index) {
        fill_color = TASKBAR_COLOR_TASK_HOVER;
    }

    taskbar_fill_rect(surface, rect, fill_color);
    taskbar_frame_rect(surface, rect, TASKBAR_COLOR_TASK_BORDER);
    taskbar_inner_highlight(surface, rect, TASKBAR_COLOR_BAR_HILITE);
    if (g_taskbar.start_icon.pixels != NULL && rect->width > 34UL) {
        taskbar_blit_icon(surface, &g_taskbar.start_icon, rect->x + 7UL, rect->y + ((rect->height - 14UL) / 2UL), 14UL);
        text_x = rect->x + 26UL;
        text_width = rect->width > 34UL ? (rect->width - 34UL) : rect->width;
    }
    else if (rect->width > 34UL) {
        taskbar_blit_builtin_start_icon(surface, rect->x + 7UL, rect->y + ((rect->height - 14UL) / 2UL), TASKBAR_COLOR_TASK_BORDER);
        text_x = rect->x + 26UL;
        text_width = rect->width > 34UL ? (rect->width - 34UL) : rect->width;
    }

    taskbar_fit_text(taskbar_task_label(task_index), text_width, fitted_text, sizeof(fitted_text));
    taskbar_draw_text(surface, text_x, rect->y + 6UL, fitted_text, TASKBAR_COLOR_TEXT);
}

/*
 * Draw the compact tray/status area on the right side of the taskbar.
 *
 * @param surface Target taskbar surface.
 * @param rect Tray bounds.
 * @return Nothing.
 */
static void taskbar_draw_tray(const RosGdiSurface* surface, const TaskbarRect* rect) {
    char tray_text[32];

    taskbar_fill_rect(surface, rect, TASKBAR_COLOR_TRAY_BG);
    taskbar_frame_rect(surface, rect, TASKBAR_COLOR_TRAY_BORDER);
    taskbar_inner_highlight(surface, rect, TASKBAR_COLOR_BAR_HILITE);
    if (g_taskbar.start_icon.pixels != NULL) {
        taskbar_blit_icon(surface, &g_taskbar.start_icon, rect->x + 7UL, rect->y + ((rect->height - 14UL) / 2UL), 14UL);
    }
    else {
        taskbar_blit_builtin_start_icon(surface, rect->x + 7UL, rect->y + ((rect->height - 14UL) / 2UL), TASKBAR_COLOR_TRAY_BORDER);
    }

    taskbar_build_tray_text(tray_text, sizeof(tray_text));
    taskbar_draw_text(surface, rect->x + 27UL, rect->y + 6UL, tray_text, TASKBAR_COLOR_TEXT_DIM);
}

/*
 * Paint the full owner-drawn taskbar.
 *
 * @param hwnd Taskbar window handle.
 * @return Nothing.
 */
static void taskbar_do_paint(HWND hwnd) {
    RosGdiSurface surface;
    TaskbarRect start_button_rect;
    TaskbarRect task_strip_rect;
    TaskbarRect tray_rect;
    unsigned long task_count;
    unsigned long index;

    if (ExplorerGdiGetWindowSurface(hwnd, &surface) < 0L) {
        debugError("explorer.taskbar: GdiGetWindowSurface failed");
        return;
    }

    g_taskbar.width = surface.width;
    g_taskbar.height = surface.height;
    taskbar_draw_background(&surface);
    taskbar_layout(&surface, &start_button_rect, &task_strip_rect, &tray_rect);
    taskbar_draw_start_button(&surface, &start_button_rect);
    task_count = taskbar_task_count();
    for (index = 0UL; index < task_count; ++index) {
        TaskbarRect button_rect;

        if (taskbar_task_button_rect(&task_strip_rect, task_count, index, &button_rect)) {
            taskbar_draw_task_button(&surface, &button_rect, index);
        }
    }
    taskbar_draw_tray(&surface, &tray_rect);

    if (ExplorerGdiReleaseWindowSurface(hwnd) < 0L) {
        debugError("explorer.taskbar: GdiReleaseWindowSurface failed");
    }
}

/*
 * Refresh shell-state tracking and repaint when the model changed.
 *
 * @return Nothing.
 */
static void taskbar_poll_shell_state(void) {
    if (!taskbar_ensure_shell_state()) {
        return;
    }

    if ((g_taskbar.start_menu_visible == 0) && (taskbar_shared_start_menu_window() != 0UL)) {
        StartMenu_Hide();
    }

    if ((g_taskbar.shell_state->task_generation != g_taskbar.last_task_generation)
        || (g_taskbar.shell_state->shell_generation != g_taskbar.last_shell_generation)) {
        g_taskbar.last_task_generation = g_taskbar.shell_state->task_generation;
        g_taskbar.last_shell_generation = g_taskbar.shell_state->shell_generation;
        taskbar_invalidate_full();
    }
}

/*
 * Toggle the placeholder Start menu state and publish it to shell state.
 *
 * @return Nothing.
 */
static void taskbar_toggle_start_menu(void) {
    long menu_x = 0L;
    long menu_y = 0L;

    g_taskbar.start_menu_visible = !g_taskbar.start_menu_visible;
    if (g_taskbar.start_menu_visible) {
        TaskbarRect start_button_rect;
        TaskbarRect task_strip_rect;
        TaskbarRect tray_rect;
        RosGdiSurface fake_surface;

        fake_surface.width = g_taskbar.width != 0UL ? g_taskbar.width : 800UL;
        fake_surface.height = g_taskbar.height != 0UL ? g_taskbar.height : EXPLORER_TASKBAR_HEIGHT;
        fake_surface.pitch = 0UL;
        fake_surface.pixel_format = ROS_KERNEL_GUI_PIXEL_FORMAT_XRGB8888;
        fake_surface.pixels = 0;
        taskbar_layout(&fake_surface, &start_button_rect, &task_strip_rect, &tray_rect);
        menu_x = (long)start_button_rect.x;
        if (taskbar_ensure_shell_state() && g_taskbar.shell_state->desktop_height > g_taskbar.height) {
            menu_y = (long)(g_taskbar.shell_state->desktop_height - g_taskbar.height);
        }
        StartMenu_Show(menu_x, menu_y);
    }
    else {
        StartMenu_Hide();
    }

    taskbar_publish_shell_state(1);
    taskbar_invalidate_full();
}

/*
 * Release any popup-menu models cached by the taskbar.
 *
 * The taskbar owns both the root popup and the nested demos submenu, so it
 * tears them down explicitly during shutdown or after a partial build failure.
 *
 * @return Nothing.
 */
static void taskbar_destroy_menus(void) {
    if (g_taskbar.demos_menu != 0UL) {
        (void)ExplorerWindowDestroyMenu(g_taskbar.demos_menu);
        g_taskbar.demos_menu = 0UL;
    }
    if (g_taskbar.tray_menu != 0UL) {
        (void)ExplorerWindowDestroyMenu(g_taskbar.tray_menu);
        g_taskbar.tray_menu = 0UL;
    }
}

/*
 * Build the tray popup menu and the nested demos submenu once per taskbar.
 *
 * The tray hit target is the current low-risk place to exercise the new menu
 * stack, so the taskbar prebuilds a reusable menu tree instead of allocating
 * it on every click.
 *
 * @return Non-zero when the menu tree is ready.
 */
static int taskbar_prepare_menus(void) {
    long status;

    if ((g_taskbar.tray_menu != 0UL) && (g_taskbar.demos_menu != 0UL)) {
        return 1;
    }

    taskbar_destroy_menus();
    g_taskbar.tray_menu = ExplorerWindowCreateMenu();
    g_taskbar.demos_menu = ExplorerWindowCreateMenu();
    if ((g_taskbar.tray_menu == 0UL) || (g_taskbar.demos_menu == 0UL)) {
        taskbar_destroy_menus();
        return 0;
    }

    status = ExplorerWindowAppendMenuItem(g_taskbar.tray_menu, EXPLORER_TASKBAR_MENU_CMD_SHELL, 0UL, "Shell", 'S');
    if (status < 0L) {
        taskbar_destroy_menus();
        return 0;
    }
    status = ExplorerWindowAppendMenuItem(g_taskbar.demos_menu, EXPLORER_TASKBAR_MENU_CMD_WIDGETDEMO, 0UL, "Widget Demo", 'W');
    if (status < 0L) {
        taskbar_destroy_menus();
        return 0;
    }
    status = ExplorerWindowAppendMenuItem(g_taskbar.demos_menu, EXPLORER_TASKBAR_MENU_CMD_GUISAMPLE, 0UL, "GUI Sample", 'G');
    if (status < 0L) {
        taskbar_destroy_menus();
        return 0;
    }
    status = ExplorerWindowAppendMenuItem(g_taskbar.demos_menu, EXPLORER_TASKBAR_MENU_CMD_IMAGEBOX, 0UL, "ImageBox Demo", 'I');
    if (status < 0L) {
        taskbar_destroy_menus();
        return 0;
    }
    status = ExplorerWindowAppendMenuItem(g_taskbar.demos_menu, EXPLORER_TASKBAR_MENU_CMD_TTFDEMO, 0UL, "TrueType Demo", 'T');
    if (status < 0L) {
        taskbar_destroy_menus();
        return 0;
    }
    status = ExplorerWindowAppendSubMenu(g_taskbar.tray_menu, g_taskbar.demos_menu, 0UL, "Demos", 'D');
    if (status < 0L) {
        taskbar_destroy_menus();
        return 0;
    }
    status = ExplorerWindowAppendMenuSeparator(g_taskbar.tray_menu);
    if (status < 0L) {
        taskbar_destroy_menus();
        return 0;
    }
    status = ExplorerWindowAppendMenuItem(g_taskbar.tray_menu, EXPLORER_TASKBAR_MENU_CMD_TOGGLE_START, 0UL, "Toggle Start Menu", 'M');
    if (status < 0L) {
        taskbar_destroy_menus();
        return 0;
    }

    return 1;
}

/*
 * Launch the tray popup menu at one taskbar-relative pointer position.
 *
 * The popup API expects desktop coordinates, so the taskbar converts its local
 * click point into the bottom-docked desktop space before starting tracking.
 *
 * @param client_x Taskbar-local pointer X coordinate.
 * @param client_y Taskbar-local pointer Y coordinate.
 * @return Nothing.
 */
static void taskbar_show_tray_menu(long client_x, long client_y) {
    long screen_y = client_y;

    if (!taskbar_prepare_menus()) {
        return;
    }
    if (g_taskbar.start_menu_visible) {
        g_taskbar.start_menu_visible = 0;
        StartMenu_Hide();
        taskbar_publish_shell_state(1);
    }
    if (taskbar_ensure_shell_state() && (g_taskbar.shell_state->desktop_height >= g_taskbar.height)) {
        screen_y += (long)(g_taskbar.shell_state->desktop_height - g_taskbar.height);
    }

    (void)ExplorerWindowTrackPopupMenu(g_taskbar.tray_menu, 0UL, client_x, screen_y, g_taskbar.hwnd);
}

/*
 * Dispatch one command selected from the tray popup menu.
 *
 * Reusing the taskbar window as the menu owner exercises the standard
 * `WM_COMMAND` delivery path instead of short-circuiting around it.
 *
 * @param command_id Selected menu command identifier.
 * @return Nothing.
 */
static void taskbar_handle_menu_command(unsigned long command_id) {
    if (command_id == EXPLORER_TASKBAR_MENU_CMD_SHELL) {
        (void)LaunchProgram("/bin/shell.exe");
        return;
    }
    if (command_id == EXPLORER_TASKBAR_MENU_CMD_WIDGETDEMO) {
        (void)LaunchProgram("/bin/widgetdemo.exe");
        return;
    }
    if (command_id == EXPLORER_TASKBAR_MENU_CMD_GUISAMPLE) {
        (void)LaunchProgram("/bin/guisample.exe");
        return;
    }
    if (command_id == EXPLORER_TASKBAR_MENU_CMD_IMAGEBOX) {
        (void)LaunchProgram("/bin/imageboxdemo.exe");
        return;
    }
    if (command_id == EXPLORER_TASKBAR_MENU_CMD_TTFDEMO) {
        (void)LaunchProgram("/bin/ttfdemo.exe");
        return;
    }
    if (command_id == EXPLORER_TASKBAR_MENU_CMD_TOGGLE_START) {
        taskbar_toggle_start_menu();
    }
}

/*
 * Handle one completed pointer-up activation on the taskbar.
 *
 * @param hit_result Final pointer hit result.
 * @return Nothing.
 */
static void taskbar_activate_hit(const TaskbarHitResult* hit_result, long pointer_x, long pointer_y) {
    if (hit_result == NULL) {
        return;
    }

    if (hit_result->part == TASKBAR_HIT_START) {
        taskbar_toggle_start_menu();
        return;
    }

    if ((hit_result->part == TASKBAR_HIT_TASK)
        && taskbar_ensure_shell_state()
        && (hit_result->task_index < taskbar_task_count())) {
        if (g_taskbar.start_menu_visible) {
            g_taskbar.start_menu_visible = 0;
            StartMenu_Hide();
            taskbar_publish_shell_state(1);
        }
        (void)ExplorerWindowPostSetForegroundWindow((HWND)g_taskbar.shell_state->tasks[hit_result->task_index].hwnd);
        return;
    }

    if (hit_result->part == TASKBAR_HIT_TRAY) {
        taskbar_show_tray_menu(pointer_x, pointer_y);
    }
}

/*
 * Initialize the taskbar window-local resources.
 *
 * @return Nothing.
 */
static void taskbar_initialize_resources(void) {
    g_taskbar.hover_part = TASKBAR_HIT_NONE;
    g_taskbar.hover_task_index = EXPLORER_TASKBAR_INVALID_INDEX;
    g_taskbar.pressed_part = TASKBAR_HIT_NONE;
    g_taskbar.pressed_task_index = EXPLORER_TASKBAR_INVALID_INDEX;
    g_taskbar.tray_menu = 0UL;
    g_taskbar.demos_menu = 0UL;
    taskbar_prepare_font();
    taskbar_prepare_icon();
    (void)taskbar_prepare_menus();
    (void)ExplorerWindowSetTimer(g_taskbar.hwnd, 1UL, EXPLORER_TASKBAR_TIMER_MSEC);
    taskbar_publish_shell_state(1);
    taskbar_invalidate_full();
}

/*
 * Tear down taskbar-local resources before the window disappears.
 *
 * @return Nothing.
 */
static void taskbar_shutdown_resources(void) {
    taskbar_publish_shell_state(0);
    taskbar_destroy_menus();
    if (g_taskbar.font_ready) {
        (void)ExplorerGdiUnloadFont(&g_taskbar.font);
        g_taskbar.font_ready = 0;
    }
    taskbar_free_icon(&g_taskbar.start_icon);
}

/*
 * Dispatch taskbar messages.
 *
 * @param hwnd Taskbar window handle.
 * @param message Window message identifier.
 * @param wParam First message payload word.
 * @param lParam Second message payload word.
 * @return Window-procedure result.
 */
static LRESULT taskbar_wndproc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    if (message == WM_CREATE) {
        g_taskbar.hwnd = hwnd;
        taskbar_initialize_resources();
        return 0L;
    }

    if (message == WM_TIMER) {
        taskbar_poll_shell_state();
        return 0L;
    }

    if (message == EXPLORER_WM_STARTMENU_CLOSED) {
        g_taskbar.start_menu_visible = 0;
        taskbar_publish_shell_state(1);
        taskbar_invalidate_full();
        return 0L;
    }

    if (message == WM_COMMAND) {
        taskbar_handle_menu_command(wParam);
        return 0L;
    }

    if (message == WM_MOUSEMOVE) {
        RosGdiSurface surface;
        TaskbarHitResult hit_result;
        long pointer_x = 0L;
        long pointer_y = 0L;

        WindowUnpackSignedPair(lParam, &pointer_x, &pointer_y);
        if (ExplorerGdiGetWindowSurface(hwnd, &surface) >= 0L) {
            hit_result = taskbar_hit_test(&surface, pointer_x, pointer_y);
            (void)ExplorerGdiReleaseWindowSurface(hwnd);
            if ((hit_result.part != g_taskbar.hover_part) || (hit_result.task_index != g_taskbar.hover_task_index)) {
                g_taskbar.hover_part = hit_result.part;
                g_taskbar.hover_task_index = hit_result.task_index;
                taskbar_invalidate_full();
            }
        }
        return 0L;
    }

    if (message == WM_LBUTTONDOWN) {
        RosGdiSurface surface;
        long pointer_x = 0L;
        long pointer_y = 0L;

        WindowUnpackSignedPair(lParam, &pointer_x, &pointer_y);
        if (ExplorerGdiGetWindowSurface(hwnd, &surface) >= 0L) {
            TaskbarHitResult hit_result = taskbar_hit_test(&surface, pointer_x, pointer_y);

            g_taskbar.pressed_part = hit_result.part;
            g_taskbar.pressed_task_index = hit_result.task_index;
            (void)ExplorerGdiReleaseWindowSurface(hwnd);
            taskbar_invalidate_full();
        }
        return 0L;
    }

    if (message == WM_LBUTTONUP) {
        RosGdiSurface surface;
        long pointer_x = 0L;
        long pointer_y = 0L;

        WindowUnpackSignedPair(lParam, &pointer_x, &pointer_y);
        if (ExplorerGdiGetWindowSurface(hwnd, &surface) >= 0L) {
            TaskbarHitResult hit_result = taskbar_hit_test(&surface, pointer_x, pointer_y);

            (void)ExplorerGdiReleaseWindowSurface(hwnd);
            if ((hit_result.part == g_taskbar.pressed_part) && (hit_result.task_index == g_taskbar.pressed_task_index)) {
                taskbar_activate_hit(&hit_result, pointer_x, pointer_y);
            }
        }

        g_taskbar.pressed_part = TASKBAR_HIT_NONE;
        g_taskbar.pressed_task_index = EXPLORER_TASKBAR_INVALID_INDEX;
        taskbar_invalidate_full();
        return 0L;
    }

    if (message == WM_PAINT || message == WM_REPAINT) {
        taskbar_do_paint(hwnd);
        return 0L;
    }

    if (message == WM_CLOSE || message == WM_DESTROY) {
        taskbar_shutdown_resources();
        return 0L;
    }

    (void)wParam;
    return 0L;
}

/*
 * Register the taskbar window class with window.dll.
 *
 * The explorer rewrite uses one UI owner thread for every window API call, so
 * class registration is serialized through that thread even though the taskbar
 * implementation stays isolated in this source file.
 *
 * @return Zero or greater on success, negative status on failure.
 */
extern "C" long ExplorerTaskbarRegisterClass(void) {
    return ExplorerWindowCreateClass("explorer.taskbar", taskbar_wndproc);
}

/*
 * Create the taskbar window.
 *
 * @param width Requested taskbar width.
 * @param height Requested display height so the bar can anchor to the bottom.
 * @return Created window handle, or zero on failure.
 */
extern "C" HWND ExplorerTaskbarCreateWindow(unsigned long width, unsigned long height) {
    WindowCreateParams params;

    /*
     * Explorer creates the taskbar visible immediately, so warm the font and
     * icon cache before the first frame to avoid a blank Start button caption
     * or missing glyph until some later repaint happens.
     */
    taskbar_prepare_font();
    taskbar_prepare_icon();

    g_taskbar.width = width;
    g_taskbar.height = EXPLORER_TASKBAR_HEIGHT;
    params.class_name = "explorer.taskbar";
    params.title = "Explorer Taskbar";
    params.parent = 0UL;
    params.x = 0L;
    params.y = (long)(height >= EXPLORER_TASKBAR_HEIGHT ? (height - EXPLORER_TASKBAR_HEIGHT) : 0U);
    params.width = width;
    params.height = EXPLORER_TASKBAR_HEIGHT;
    params.style = ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_TOPMOST | ROS_WINDOW_STYLE_SYSTEM_UI;

    g_taskbar.hwnd = ExplorerWindowCreateWindowEx(&params);
    return g_taskbar.hwnd;
}

/* Taskbar UI thread entry */
extern "C" void explorer_taskbar_thread_entry(unsigned long argument) {
    (void)argument;
    if (ExplorerWindowCreateClass("explorer.taskbar", taskbar_wndproc) < 0L) {
        writeLog("explorer.taskbar: CreateWindowClass failed");
        taskbar_thread_park_forever();
    }

    WindowCreateParams params;
    params.class_name = "explorer.taskbar";
    params.title = "Explorer Taskbar";
    params.parent = 0UL;
    params.x = 0L;
    params.y = 560L;
    params.width = 800U;
    params.height = EXPLORER_TASKBAR_HEIGHT;
    params.style = ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_TOPMOST | ROS_WINDOW_STYLE_SYSTEM_UI | ROS_WINDOW_STYLE_DECORATED | ROS_WINDOW_STYLE_BORDER;

    g_taskbar.width = 800U;
    g_taskbar.height = EXPLORER_TASKBAR_HEIGHT;
    g_taskbar.hwnd = ExplorerWindowCreateWindowEx(&params);
    if (g_taskbar.hwnd == 0UL) {
        writeLog("explorer.taskbar: CreateWindowEx failed");
        taskbar_thread_park_forever();
    }

    MSG msg;
    long res;
    while ((res = ExplorerWindowGetMessage(&msg)) > 0L) {
        ExplorerWindowTranslateMessage(&msg);
        ExplorerWindowDispatchMessage(&msg);
    }

    (void)res;
    taskbar_thread_park_forever();
}
 