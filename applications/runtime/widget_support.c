#define ROS_WIDGET_EXPORTS 1
#include "app/syscall.h"
#include "app/widgets.h"

DLL_EXPORT(WidgetInitialize);
DLL_EXPORT(WidgetRegisterBuiltinClasses);
DLL_EXPORT(WidgetRegisterClass);
DLL_EXPORT(WidgetCreateWindow);
DLL_EXPORT(WidgetCreateDialog);
DLL_EXPORT(WidgetCreateButton);
DLL_EXPORT(WidgetCreateLabel);
DLL_EXPORT(WidgetCreateTextBox);
DLL_EXPORT(WidgetSetFont);
DLL_EXPORT(WidgetSetText);
DLL_EXPORT(WidgetSetBounds);
DLL_EXPORT(WidgetGetText);
DLL_EXPORT(WidgetBeginPaint);
DLL_EXPORT(WidgetEndPaint);
DLL_EXPORT(WidgetFillRect);
DLL_EXPORT(WidgetFrameRect);
DLL_EXPORT(WidgetFillVerticalGradient);
DLL_EXPORT(WidgetDrawText);
DLL_EXPORT(WidgetDrawCenteredText);
DLL_EXPORT(WidgetPaintDialogBackground);
DLL_EXPORT(WidgetInvalidate);

#define WIDGET_STATUS_INVALID (-1L)
#define WIDGET_STATUS_ERROR (-4L)
#define WIDGET_DEFAULT_FONT_HEIGHT 14UL

#define WIDGET_DIALOG_BG_TOP 0x00F5F3EBUL
#define WIDGET_DIALOG_BG_BOTTOM 0x00E7E3D3UL
#define WIDGET_DIALOG_TEXT 0x00282828UL

#define WIDGET_BUTTON_TOP 0x00FDFCF8UL
#define WIDGET_BUTTON_BOTTOM 0x00D7E4F6UL
#define WIDGET_BUTTON_HOVER_TOP 0x00FFFFFFUL
#define WIDGET_BUTTON_HOVER_BOTTOM 0x00E4EEFBU
#define WIDGET_BUTTON_PRESSED_TOP 0x00C6D3E2UL
#define WIDGET_BUTTON_PRESSED_BOTTOM 0x00E8F0F7UL
#define WIDGET_BUTTON_HILIGHT 0x00FFFFFFUL
#define WIDGET_BUTTON_SHADOW 0x007E7A6EUL
#define WIDGET_BUTTON_TEXT 0x00202020UL

#define WIDGET_TEXTBOX_BG 0x00FFFFFFUL
#define WIDGET_TEXTBOX_BORDER_DARK 0x008A8479UL
#define WIDGET_TEXTBOX_BORDER_LIGHT 0x00FDFDFDUL
#define WIDGET_TEXTBOX_BORDER_FOCUS 0x003E77B4UL
#define WIDGET_TEXTBOX_CARET 0x00243752UL
#define WIDGET_TEXTBOX_TEXT 0x00202020UL

typedef struct WidgetClassDefinition {
    int in_use;
    unsigned long kind;
    WNDPROC user_proc;
    char class_name[ROS_WINDOW_CLASS_NAME_MAX];
    struct WidgetClassDefinition* next;
} WidgetClassDefinition;

typedef struct WidgetInstance {
    int in_use;
    HWND hwnd;
    HWND parent;
    unsigned long kind;
    unsigned long style;
    long x;
    long y;
    unsigned long width;
    unsigned long height;
    WNDPROC user_proc;
    char class_name[ROS_WINDOW_CLASS_NAME_MAX];
    char text[ROS_WINDOW_TITLE_MAX];
    int hover_hot;
    int pointer_down;
    int has_focus;
    unsigned long caret_index;
    struct WidgetInstance* next;
} WidgetInstance;

static WidgetClassDefinition* g_widget_classes = 0;
static WidgetInstance* g_widget_instances = 0;
static int g_widget_initialized = 0;
static const char g_widget_empty_text[] = "";
static RosGdiFont g_widget_font;
static int g_widget_font_attempted = 0;
static int g_widget_font_loaded = 0;
static HWND g_widget_focus_hwnd = 0UL;

static WidgetInstance* widget_find_instance(HWND hwnd);
static unsigned long widget_measure_text(const char* text);
static void widget_release_instance_record(WidgetInstance* instance);

/*
 * Allocate one widget-framework heap block directly from the kernel-backed
 * userspace allocator.
 *
 * widgets.dll is a thin helper DLL and does not link the full `ros_support.c`
 * malloc/free runtime. Using the raw syscall-backed allocator keeps the new
 * dynamic registries self-contained without introducing a new DLL import.
 *
 * @param size Bytes required for the allocation.
 * @return Heap block on success, or null when the allocation fails.
 */
static void* widget_heap_alloc(unsigned long size) {
    unsigned long bytes = size != 0UL ? size : 1UL;
    long result = (long)invokeSyscall1(SYS_MALLOC, bytes);

    return result < 0L ? 0 : (void*)(unsigned long)result;
}

/*
 * Release one widget-framework heap block acquired from `widget_heap_alloc`.
 *
 * @param memory Heap block to free.
 * @return Nothing.
 */
static void widget_heap_free(void* memory) {
    if (!memory) {
        return;
    }

    (void)invokeSyscall1(SYS_FREE, (unsigned long)memory);
}

/*
 * Release every heap-backed widget class record owned by the current process.
 *
 * @return Nothing.
 */
static void widget_release_all_classes(void) {
    WidgetClassDefinition* current = g_widget_classes;

    while (current) {
        WidgetClassDefinition* next = current->next;

        widget_heap_free(current);
        current = next;
    }

    g_widget_classes = 0;
}

/*
 * Release every heap-backed widget instance record owned by the current
 * process.
 *
 * @return Nothing.
 */
static void widget_release_all_instances(void) {
    WidgetInstance* current = g_widget_instances;

    while (current) {
        WidgetInstance* next = current->next;

        widget_heap_free(current);
        current = next;
    }

    g_widget_instances = 0;
}

/*
 * Clear one byte range without relying on hosted runtime helpers.
 *
 * @param destination Buffer to clear.
 * @param size Number of bytes to clear.
 * @return Nothing.
 */
static void widget_zero_memory(void* destination, unsigned long size) {
    unsigned char* bytes = (unsigned char*)destination;
    unsigned long index;

    if (!bytes) {
        return;
    }

    for (index = 0UL; index < size; ++index) {
        bytes[index] = 0U;
    }
}

/*
 * Release the current process-local widget font and reopen the lazy-load path
 * so callers can switch faces or retry the default font policy cleanly.
 *
 * @return Nothing.
 */
static void widget_reset_font_state(void) {
    if (g_widget_font_loaded) {
        (void)GdiUnloadFont(&g_widget_font);
    }

    widget_zero_memory(&g_widget_font, sizeof(g_widget_font));
    g_widget_font_attempted = 0;
    g_widget_font_loaded = 0;
}

/*
 * Reset all per-process widget framework state.
 *
 * @return Nothing.
 */
static void widget_reset_state(void) {
    widget_reset_font_state();
    widget_release_all_classes();
    widget_release_all_instances();
    g_widget_focus_hwnd = 0UL;
    g_widget_initialized = 0;
}

/*
 * Copy one caller string into a fixed framework-owned buffer.
 *
 * @param destination Destination buffer.
 * @param destination_size Buffer size including the terminator.
 * @param source Optional source string.
 * @return Nothing.
 */
static void widget_copy_text(char* destination, unsigned long destination_size, const char* source) {
    unsigned long index;

    if (!destination || destination_size == 0UL) {
        return;
    }

    if (!source) {
        destination[0] = '\0';
        return;
    }

    for (index = 0UL; (index + 1UL) < destination_size && source[index] != '\0'; ++index) {
        destination[index] = source[index];
    }
    destination[index] = '\0';
}

/*
 * Measure one null-terminated widget text buffer up to its fixed storage size.
 *
 * @param text Source string stored in one widget instance.
 * @param capacity Fixed buffer capacity including the terminator.
 * @return Character count excluding the terminator.
 */
static unsigned long widget_text_length(const char* text, unsigned long capacity) {
    unsigned long length = 0UL;

    if (!text || capacity == 0UL) {
        return 0UL;
    }

    while (length < capacity && text[length] != '\0') {
        ++length;
    }

    return length < capacity ? length : capacity;
}

/*
 * Measure one text prefix so caret placement can track proportional fonts.
 *
 * @param text Source text buffer.
 * @param prefix_length Number of leading characters to measure.
 * @return Pixel width of the requested prefix.
 */
static unsigned long widget_measure_text_prefix(const char* text, unsigned long prefix_length) {
    char buffer[ROS_WINDOW_TITLE_MAX];
    unsigned long index = 0UL;

    if (!text) {
        return 0UL;
    }

    while (index < prefix_length && (index + 1UL) < sizeof(buffer) && text[index] != '\0') {
        buffer[index] = text[index];
        ++index;
    }
    buffer[index] = '\0';
    return widget_measure_text(buffer);
}

/*
 * Report whether one widget kind should participate in keyboard focus.
 *
 * @param instance Candidate widget instance.
 * @return Non-zero when the widget can take focus.
 */
static int widget_instance_can_focus(const WidgetInstance* instance) {
    if (!instance) {
        return 0;
    }

    return instance->kind == ROS_WIDGET_KIND_BUTTON || instance->kind == ROS_WIDGET_KIND_TEXTBOX;
}

/*
 * Clamp one textbox caret index to the current text length.
 *
 * @param instance Textbox widget instance to update.
 * @param caret_index Requested insertion index.
 * @return Nothing.
 */
static void widget_textbox_set_caret(WidgetInstance* instance, unsigned long caret_index) {
    unsigned long length;

    if (!instance || instance->kind != ROS_WIDGET_KIND_TEXTBOX) {
        return;
    }

    length = widget_text_length(instance->text, sizeof(instance->text));
    instance->caret_index = caret_index > length ? length : caret_index;
}

/*
 * Promote one widget into the process-local focus slot and repaint the old and
 * new focus owners when the state changes.
 *
 * @param hwnd Widget handle that should become focused, or zero to clear focus.
 * @return Nothing.
 */
static void widget_set_focus(HWND hwnd) {
    WidgetInstance* previous = widget_find_instance(g_widget_focus_hwnd);
    WidgetInstance* next = widget_find_instance(hwnd);

    if (next != NULL && !widget_instance_can_focus(next)) {
        next = 0;
        hwnd = 0UL;
    }
    if (g_widget_focus_hwnd == hwnd) {
        return;
    }

    if (previous != NULL) {
        previous->has_focus = 0;
        (void)WidgetInvalidate(previous->hwnd);
    }

    g_widget_focus_hwnd = hwnd;
    if (next != NULL) {
        next->has_focus = 1;
        if (next->kind == ROS_WIDGET_KIND_TEXTBOX) {
            widget_textbox_set_caret(next, next->caret_index);
        }
        (void)WidgetInvalidate(next->hwnd);
    }
}

/*
 * Choose one insertion index inside a textbox from a client-relative pointer X
 * position so mouse clicks can reposition the caret like Win32 edit controls.
 *
 * @param instance Textbox widget instance under the pointer.
 * @param local_x Client-relative pointer X coordinate.
 * @return Caret index closest to the requested X position.
 */
static unsigned long widget_textbox_hit_test_caret(const WidgetInstance* instance, long local_x) {
    unsigned long length;
    unsigned long index;
    const unsigned long inset = 6UL;

    if (!instance || instance->kind != ROS_WIDGET_KIND_TEXTBOX) {
        return 0UL;
    }
    if (local_x <= (long)inset) {
        return 0UL;
    }

    length = widget_text_length(instance->text, sizeof(instance->text));
    for (index = 0UL; index < length; ++index) {
        unsigned long left = widget_measure_text_prefix(instance->text, index);
        unsigned long right = widget_measure_text_prefix(instance->text, index + 1UL);
        unsigned long midpoint = inset + ((left + right) / 2UL);

        if ((unsigned long)local_x < midpoint) {
            return index;
        }
    }

    return length;
}

/*
 * Apply one printable or editing key to the built-in textbox state.
 *
 * @param instance Textbox widget instance being edited.
 * @param key ASCII key value carried by `WM_KEYDOWN`.
 * @return Non-zero when the textbox contents changed.
 */
static int widget_textbox_apply_key(WidgetInstance* instance, unsigned long key) {
    unsigned long length;
    unsigned long index;

    if (!instance || instance->kind != ROS_WIDGET_KIND_TEXTBOX) {
        return 0;
    }

    length = widget_text_length(instance->text, sizeof(instance->text));
    if (instance->caret_index > length) {
        instance->caret_index = length;
    }

    if (key == 8UL) {
        if (instance->caret_index == 0UL) {
            return 0;
        }

        for (index = instance->caret_index - 1UL; index < length; ++index) {
            instance->text[index] = instance->text[index + 1UL];
        }
        --instance->caret_index;
        return 1;
    }

    if (key == 127UL) {
        if (instance->caret_index >= length) {
            return 0;
        }

        for (index = instance->caret_index; index < length; ++index) {
            instance->text[index] = instance->text[index + 1UL];
        }
        return 1;
    }

    if (key == '\r' || key == '\n' || key == '\t') {
        return 0;
    }
    if (key < 32UL || key > 126UL || (length + 1UL) >= sizeof(instance->text)) {
        return 0;
    }

    for (index = length + 1UL; index > instance->caret_index; --index) {
        instance->text[index] = instance->text[index - 1UL];
    }
    instance->text[instance->caret_index] = (char)key;
    ++instance->caret_index;
    return 1;
}

/*
 * Load the process-local default widget font once and keep the old mini-font
 * path available as a transparent fallback when no staged raster asset exists.
 *
 * @return Loaded font wrapper, or null when the caller should use the fallback.
 */
static const RosGdiFont* widget_get_text_font(void) {
    if (!g_widget_font_attempted) {
        g_widget_font_attempted = 1;
        if (GdiLoadFont(0, WIDGET_DEFAULT_FONT_HEIGHT, &g_widget_font) >= 0L) {
            g_widget_font_loaded = 1;
        }
    }

    if (!g_widget_font_loaded) {
        return 0;
    }

    return &g_widget_font;
}

/*
 * Return the active widget text-line height so controls can center captions
 * consistently whether they are using the loaded font or the mini-font backup.
 *
 * @return Active text line height in pixels.
 */
static unsigned long widget_text_height(void) {
    const RosGdiFont* font = widget_get_text_font();

    if (font) {
        return font->line_height;
    }

    return ROS_MINI_FONT_LINE_HEIGHT;
}

/*
 * Compare two ASCII strings for exact equality without relying on hosted libc.
 *
 * @param left First string.
 * @param right Second string.
 * @return Non-zero when both strings match exactly.
 */
static int widget_text_equals(const char* left, const char* right) {
    unsigned long index = 0UL;

    if (left == right) {
        return 1;
    }
    if (!left || !right) {
        return 0;
    }

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }
        ++index;
    }

    return left[index] == right[index];
}

/*
 * Encode one RGB color for the active surface pixel format.
 *
 * @param pixel_format Target surface pixel format.
 * @param color Caller-visible RGB value.
 * @return Encoded 32-bit pixel.
 */
static unsigned long widget_encode_color(unsigned long pixel_format, unsigned long color) {
    if (pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
        return color;
    }

    return ((color & 0x000000FFUL) << 16) |
        (color & 0x0000FF00UL) |
        ((color & 0x00FF0000UL) >> 16);
}

/*
 * Blend two colors with an eight-bit interpolation factor.
 *
 * @param first Start color.
 * @param second End color.
 * @param factor Blend factor in the inclusive range [0, 255].
 * @return Interpolated RGB color.
 */
static unsigned long widget_blend_color(unsigned long first, unsigned long second, unsigned long factor) {
    unsigned long inv = 255UL - factor;
    unsigned long red = ((((first >> 16) & 0xFFUL) * inv) + (((second >> 16) & 0xFFUL) * factor)) / 255UL;
    unsigned long green = ((((first >> 8) & 0xFFUL) * inv) + (((second >> 8) & 0xFFUL) * factor)) / 255UL;
    unsigned long blue = (((first & 0xFFUL) * inv) + ((second & 0xFFUL) * factor)) / 255UL;

    return (red << 16) | (green << 8) | blue;
}

/*
 * Decode one stored surface pixel back into RGB order for alpha blending.
 *
 * @param pixel_format Surface pixel format.
 * @param encoded Encoded pixel value already stored in the surface.
 * @return RGB color in `0x00RRGGBB` order.
 */
static unsigned long widget_decode_color(unsigned long pixel_format, unsigned long encoded) {
    if (pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
        return encoded;
    }

    return ((encoded & 0x000000FFUL) << 16) |
        (encoded & 0x0000FF00UL) |
        ((encoded & 0x00FF0000UL) >> 16);
}

/*
 * Blend one text pixel directly into the mapped surface.
 *
 * @param surface Target mapped surface.
 * @param x Pixel X coordinate.
 * @param y Pixel Y coordinate.
 * @param color Source RGB text color.
 * @param alpha Glyph coverage alpha in the range [0, 255].
 * @return Zero on success, or a negative status code on failure.
 */
static long widget_blend_surface_pixel(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long color, unsigned long alpha) {
    uint32_t* pixel;

    if (!surface || !surface->pixels) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (x >= surface->width || y >= surface->height || alpha == 0UL) {
        return ROS_USER_IPC_STATUS_OK;
    }

    pixel = (uint32_t*)((uint8_t*)surface->pixels + (y * surface->pitch)) + x;
    if (alpha >= 255UL) {
        *pixel = (uint32_t)widget_encode_color(surface->pixel_format, color);
        return ROS_USER_IPC_STATUS_OK;
    }

    *pixel = (uint32_t)widget_encode_color(
        surface->pixel_format,
        widget_blend_color(widget_decode_color(surface->pixel_format, *pixel), color, alpha));
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Clamp one rectangle to the active surface and fill it directly.
 *
 * @param surface Writable mapped surface.
 * @param x Requested rectangle X coordinate.
 * @param y Requested rectangle Y coordinate.
 * @param width Requested rectangle width.
 * @param height Requested rectangle height.
 * @param color RGB fill color.
 * @return Zero on success, or a negative status code on failure.
 */
static long widget_fill_surface_rect(const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color) {
    unsigned long row;
    unsigned long col;
    unsigned long end_x;
    unsigned long end_y;
    unsigned long encoded_color;

    if (!surface || !surface->pixels) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (width == 0UL || height == 0UL) {
        return ROS_USER_IPC_STATUS_OK;
    }
    if (x >= surface->width || y >= surface->height) {
        return ROS_USER_IPC_STATUS_OK;
    }

    end_x = x + width;
    end_y = y + height;
    if (end_x > surface->width) {
        end_x = surface->width;
    }
    if (end_y > surface->height) {
        end_y = surface->height;
    }

    encoded_color = widget_encode_color(surface->pixel_format, color);
    for (row = y; row < end_y; ++row) {
        uint32_t* pixels = (uint32_t*)((uint8_t*)surface->pixels + (row * surface->pitch));
        for (col = x; col < end_x; ++col) {
            pixels[col] = (uint32_t)encoded_color;
        }
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Find one registered widget class definition.
 *
 * @param class_name Class name to search.
 * @return Matching class record, or null when absent.
 */
static WidgetClassDefinition* widget_find_class(const char* class_name) {
    WidgetClassDefinition* current;

    if (!class_name || class_name[0] == '\0') {
        return 0;
    }

    for (current = g_widget_classes; current != 0; current = current->next) {
        if (!current->in_use) {
            continue;
        }
        if (widget_text_equals(current->class_name, class_name)) {
            return current;
        }
    }

    return 0;
}

/*
 * Allocate one heap-backed class-definition record.
 *
 * @return Available class record, or null when allocation fails.
 */
static WidgetClassDefinition* widget_reserve_class(void) {
    WidgetClassDefinition* slot = (WidgetClassDefinition*)widget_heap_alloc(sizeof(*slot));

    if (!slot) {
        return 0;
    }

    widget_zero_memory(slot, sizeof(*slot));
    slot->next = g_widget_classes;
    g_widget_classes = slot;
    return slot;
}

/*
 * Find one live widget instance by handle.
 *
 * @param hwnd Widget handle to search.
 * @return Matching instance, or null when absent.
 */
static WidgetInstance* widget_find_instance(HWND hwnd) {
    WidgetInstance* current;

    if (hwnd == 0UL) {
        return 0;
    }

    for (current = g_widget_instances; current != 0; current = current->next) {
        if (current->in_use && current->hwnd == hwnd) {
            return current;
        }
    }

    return 0;
}

/*
 * Reserve one heap-backed widget-instance record before the actual server
 * create call.
 *
 * @return Reserved instance record, or null when allocation fails.
 */
static WidgetInstance* widget_reserve_instance(void) {
    WidgetInstance* instance = (WidgetInstance*)widget_heap_alloc(sizeof(*instance));

    if (!instance) {
        return 0;
    }

    widget_zero_memory(instance, sizeof(*instance));
    instance->in_use = 1;
    instance->next = g_widget_instances;
    g_widget_instances = instance;
    return instance;
}

/*
 * Remove one widget-instance record once the window lifecycle is over.
 *
 * @param hwnd Widget handle to forget.
 * @return Nothing.
 */
static void widget_release_instance(HWND hwnd) {
    WidgetInstance* instance = widget_find_instance(hwnd);

    if (!instance) {
        return;
    }

    widget_release_instance_record(instance);
}

/*
 * Release one widget-instance record by pointer.
 *
 * @param instance Widget record to unlink.
 * @return Nothing.
 */
static void widget_release_instance_record(WidgetInstance* instance) {
    WidgetInstance** link;

    if (!instance) {
        return;
    }

    for (link = &g_widget_instances; *link != 0; link = &((*link)->next)) {
        if (*link == instance) {
            *link = instance->next;
            widget_heap_free(instance);
            return;
        }
    }
}

/*
 * Measure one string in the shared fixed-width font.
 *
 * @param text Text to measure.
 * @return Pixel width at the default scale.
 */
static unsigned long widget_measure_text(const char* text) {
    const RosGdiFont* font = widget_get_text_font();
    unsigned long width = 0UL;
    unsigned long height = 0UL;

    if (!text) {
        return 0UL;
    }

    if (font && GdiMeasureText(font, text, &width, &height) >= 0L) {
        (void)height;
        return width;
    }

    return rosMiniFontMeasureText(text);
}

/*
 * Draw one text string directly into the mapped surface.
 *
 * @param surface Target mapped surface.
 * @param x Baseline-left X coordinate.
 * @param y Baseline-top Y coordinate.
 * @param text Null-terminated string to render.
 * @param color RGB text color.
 * @return Zero on success, or a negative status code on failure.
 */
static long widget_draw_text_surface(const RosGdiSurface* surface, unsigned long x, unsigned long y, const char* text, unsigned long color) {
    const RosGdiFont* font = widget_get_text_font();
    unsigned long cursor_x = x;
    unsigned long cursor_y = y;
    unsigned long index;

    if (!surface || !text) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    if (font) {
        long status = GdiDrawTextSurface(surface, font, x, y, text, color);

        if (status >= 0L) {
            return status;
        }
    }

    for (index = 0UL; text[index] != '\0'; ++index) {
        unsigned char ch = (unsigned char)text[index];
        const RosMiniFontGlyph* glyph;
        const uint8_t* coverage;
        unsigned long row;
        unsigned long baseline_y;

        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            cursor_x = x;
            cursor_y += ROS_MINI_FONT_LINE_HEIGHT;
            continue;
        }

        glyph = rosMiniFontGlyph((unsigned long)ch);
        coverage = rosMiniFontGlyphCoverage(glyph);
        baseline_y = cursor_y + (unsigned long)ROS_MINI_FONT_ASCENT;
        if (coverage && glyph->width != 0U && glyph->height != 0U) {
            for (row = 0UL; row < glyph->height; ++row) {
                unsigned long col;
                long draw_y = (long)baseline_y - glyph->bitmap_top + (long)row;

                if (draw_y < 0L) {
                    continue;
                }

                for (col = 0UL; col < glyph->width; ++col) {
                    unsigned long alpha = coverage[(row * glyph->width) + col];
                    long draw_x = (long)cursor_x + glyph->bitmap_left + (long)col;

                    if (alpha == 0UL || draw_x < 0L) {
                        continue;
                    }

                    (void)widget_blend_surface_pixel(surface, (unsigned long)draw_x, (unsigned long)draw_y, color, alpha);
                }
            }
        }

        cursor_x += glyph->advance;
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Paint the shared XP-style dialog client background.
 *
 * @param surface Target mapped surface.
 * @return Zero on success, or a negative status code on failure.
 */
static long widget_paint_dialog_surface(const RosGdiSurface* surface) {
    unsigned long highlight_height;
    unsigned long row;

    if (!surface) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    highlight_height = surface->height / 3UL;
    if (highlight_height < 18UL) {
        highlight_height = surface->height;
    }
    for (row = 0UL; row < surface->height; ++row) {
        unsigned long factor = (surface->height <= 1UL) ? 255UL : ((row * 255UL) / (surface->height - 1UL));
        unsigned long color = widget_blend_color(WIDGET_DIALOG_BG_TOP, WIDGET_DIALOG_BG_BOTTOM, factor);
        if (row < highlight_height) {
            color = widget_blend_color(0x00FFFFFFUL, color, 96UL);
        }
        (void)widget_fill_surface_rect(surface, 0UL, row, surface->width, 1UL, color);
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Paint one XP-style button face into the target surface.
 *
 * @param instance Widget instance being painted.
 * @param surface Target mapped surface.
 * @return Zero on success, or a negative status code on failure.
 */
static long widget_paint_button(const WidgetInstance* instance, const RosGdiSurface* surface) {
    unsigned long text_width;
    unsigned long text_x;
    unsigned long text_y;
    unsigned long row;
    unsigned long face_top;
    unsigned long face_bottom;
    unsigned long top_edge;
    unsigned long left_edge;
    unsigned long bottom_edge;
    unsigned long right_edge;
    unsigned long text_offset = 0UL;

    if (!instance || !surface) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    if (instance->pointer_down) {
        face_top = WIDGET_BUTTON_PRESSED_TOP;
        face_bottom = WIDGET_BUTTON_PRESSED_BOTTOM;
        top_edge = WIDGET_BUTTON_SHADOW;
        left_edge = WIDGET_BUTTON_SHADOW;
        bottom_edge = WIDGET_BUTTON_HILIGHT;
        right_edge = WIDGET_BUTTON_HILIGHT;
        text_offset = 1UL;
    }
    else if (instance->hover_hot) {
        face_top = WIDGET_BUTTON_HOVER_TOP;
        face_bottom = WIDGET_BUTTON_HOVER_BOTTOM;
        top_edge = WIDGET_BUTTON_HILIGHT;
        left_edge = WIDGET_BUTTON_HILIGHT;
        bottom_edge = WIDGET_BUTTON_SHADOW;
        right_edge = WIDGET_BUTTON_SHADOW;
    }
    else {
        face_top = WIDGET_BUTTON_TOP;
        face_bottom = WIDGET_BUTTON_BOTTOM;
        top_edge = WIDGET_BUTTON_HILIGHT;
        left_edge = WIDGET_BUTTON_HILIGHT;
        bottom_edge = WIDGET_BUTTON_SHADOW;
        right_edge = WIDGET_BUTTON_SHADOW;
    }

    (void)widget_fill_surface_rect(surface, 0UL, 0UL, surface->width, surface->height, WIDGET_BUTTON_BOTTOM);
    for (row = 1UL; row + 1UL < surface->height; ++row) {
        unsigned long factor = (surface->height <= 2UL) ? 255UL : (((row - 1UL) * 255UL) / (surface->height - 2UL));
        unsigned long color = widget_blend_color(face_top, face_bottom, factor);
        (void)widget_fill_surface_rect(surface, 1UL, row, surface->width > 2UL ? (surface->width - 2UL) : 0UL, 1UL, color);
    }

    (void)widget_fill_surface_rect(surface, 1UL, 1UL, surface->width > 2UL ? (surface->width - 2UL) : 0UL, 1UL, top_edge);
    if (surface->height > 3UL) {
        (void)widget_fill_surface_rect(surface, 1UL, surface->height - 2UL, surface->width > 2UL ? (surface->width - 2UL) : 0UL, 1UL, 0x00B9C6D7UL);
    }
    (void)widget_fill_surface_rect(surface, 0UL, 0UL, surface->width, 1UL, top_edge);
    (void)widget_fill_surface_rect(surface, 0UL, 0UL, 1UL, surface->height, left_edge);
    (void)widget_fill_surface_rect(surface, 0UL, surface->height > 0UL ? (surface->height - 1UL) : 0UL, surface->width, 1UL, bottom_edge);
    (void)widget_fill_surface_rect(surface, surface->width > 0UL ? (surface->width - 1UL) : 0UL, 0UL, 1UL, surface->height, right_edge);
    if (surface->width > 2UL && surface->height > 2UL) {
        (void)widget_fill_surface_rect(surface, 1UL, surface->height - 2UL, surface->width - 2UL, 1UL, 0x00939FB2UL);
        (void)widget_fill_surface_rect(surface, surface->width - 2UL, 1UL, 1UL, surface->height - 2UL, 0x00939FB2UL);
    }

    text_width = widget_measure_text(instance->text);
    text_x = ((surface->width > text_width) ? ((surface->width - text_width) / 2UL) : 2UL) + text_offset;
    text_y = ((surface->height > widget_text_height()) ? ((surface->height - widget_text_height()) / 2UL) : 1UL) + text_offset;
    return widget_draw_text_surface(surface, text_x, text_y, instance->text, WIDGET_BUTTON_TEXT);
}

/*
 * Paint one textbox face into the target surface.
 *
 * @param instance Widget instance being painted.
 * @param surface Target mapped surface.
 * @return Zero on success, or a negative status code on failure.
 */
static long widget_paint_textbox(const WidgetInstance* instance, const RosGdiSurface* surface) {
    unsigned long border_color;
    unsigned long text_y;

    if (!instance || !surface) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    border_color = instance->has_focus ? WIDGET_TEXTBOX_BORDER_FOCUS : WIDGET_TEXTBOX_BORDER_DARK;
    (void)widget_fill_surface_rect(surface, 0UL, 0UL, surface->width, surface->height, border_color);
    if (surface->width > 2UL && surface->height > 2UL) {
        (void)widget_fill_surface_rect(surface, 1UL, 1UL, surface->width - 2UL, surface->height - 2UL, WIDGET_TEXTBOX_BORDER_LIGHT);
    }
    if (surface->width > 4UL && surface->height > 4UL) {
        (void)widget_fill_surface_rect(surface, 2UL, 2UL, surface->width - 4UL, surface->height - 4UL, WIDGET_TEXTBOX_BG);
    }

    text_y = (surface->height > widget_text_height()) ? ((surface->height - widget_text_height()) / 2UL) : 2UL;
    (void)widget_draw_text_surface(surface, 4UL, text_y, instance->text, WIDGET_TEXTBOX_TEXT);
    if (instance->has_focus && surface->height > 6UL) {
        unsigned long caret_x = 4UL + widget_measure_text_prefix(instance->text, instance->caret_index);

        if (caret_x + 1UL >= surface->width) {
            caret_x = surface->width > 3UL ? (surface->width - 3UL) : 0UL;
        }
        (void)widget_fill_surface_rect(surface, caret_x, text_y > 1UL ? (text_y - 1UL) : 1UL, 1UL, widget_text_height() > 2UL ? (widget_text_height() - 1UL) : 1UL, WIDGET_TEXTBOX_CARET);
    }
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Paint one label into the target surface.
 *
 * @param instance Widget instance being painted.
 * @param surface Target mapped surface.
 * @return Zero on success, or a negative status code on failure.
 */
static long widget_paint_label(const WidgetInstance* instance, const RosGdiSurface* surface) {
    unsigned long text_y;

    if (!instance || !surface) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    (void)widget_paint_dialog_surface(surface);
    text_y = (surface->height > widget_text_height()) ? ((surface->height - widget_text_height()) / 2UL) : 0UL;
    return widget_draw_text_surface(surface, 0UL, text_y, instance->text, WIDGET_DIALOG_TEXT);
}

/*
 * Paint the default visuals for one widget kind.
 *
 * @param instance Widget instance being painted.
 * @return Zero on success, or a negative status code on failure.
 */
static long widget_default_paint(const WidgetInstance* instance) {
    RosWidgetPaintContext context;
    long status;

    if (!instance) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    status = WidgetBeginPaint(instance->hwnd, &context);
    if (status < 0) {
        return status;
    }

    switch (instance->kind) {
    case ROS_WIDGET_KIND_DIALOG:
        status = widget_paint_dialog_surface(&context.surface);
        break;
    case ROS_WIDGET_KIND_BUTTON:
        status = widget_paint_button(instance, &context.surface);
        break;
    case ROS_WIDGET_KIND_TEXTBOX:
        status = widget_paint_textbox(instance, &context.surface);
        break;
    case ROS_WIDGET_KIND_LABEL:
        status = widget_paint_label(instance, &context.surface);
        break;
    default:
        status = ROS_USER_IPC_STATUS_NOT_FOUND;
        break;
    }

    if (WidgetEndPaint(&context) < 0 && status >= 0) {
        status = WIDGET_STATUS_ERROR;
    }

    return status;
}

/*
 * Route widget messages through the framework before handing control back to
 * the caller's optional procedure.
 *
 * @param hwnd Target widget handle.
 * @param message Window message identifier.
 * @param wParam First message payload.
 * @param lParam Second message payload.
 * @return Caller procedure result when present, otherwise zero.
 */
static LRESULT widget_window_proc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    WidgetInstance* instance = widget_find_instance(hwnd);
    WNDPROC user_proc = instance ? instance->user_proc : 0;
    long pointer_x = 0L;
    long pointer_y = 0L;

    if (instance) {
        if (message == WM_PAINT) {
            if (instance->kind != ROS_WIDGET_KIND_DIALOG || user_proc == 0) {
                (void)widget_default_paint(instance);
            }
        }
        else if (message == WM_DESTROY) {
            if (g_widget_focus_hwnd == hwnd) {
                g_widget_focus_hwnd = 0UL;
            }
            widget_release_instance(hwnd);
        }
        else if (message == WM_MOVE) {
            long x = 0L;
            long y = 0L;

            WindowUnpackSignedPair(lParam, &x, &y);
            instance->x = x;
            instance->y = y;
        }
        else if (message == WM_SIZE) {
            long width = 0L;
            long height = 0L;

            WindowUnpackSignedPair(lParam, &width, &height);
            if (width > 0L) {
                instance->width = (unsigned long)width;
            }
            if (height > 0L) {
                instance->height = (unsigned long)height;
            }
        }
        else if (message == WM_MOUSEMOVE) {
            if (!instance->hover_hot) {
                instance->hover_hot = 1;
                (void)WidgetInvalidate(hwnd);
            }
        }
        else if (message == WM_MOUSELEAVE) {
            if (instance->hover_hot || instance->pointer_down) {
                instance->hover_hot = 0;
                instance->pointer_down = 0;
                (void)WidgetInvalidate(hwnd);
            }
        }
        else if (message == WM_LBUTTONDOWN) {
            WindowUnpackSignedPair(lParam, &pointer_x, &pointer_y);
            if (widget_instance_can_focus(instance)) {
                widget_set_focus(hwnd);
            }

            instance->hover_hot = 1;
            if (instance->kind == ROS_WIDGET_KIND_BUTTON) {
                if (!instance->pointer_down) {
                    instance->pointer_down = 1;
                    (void)WidgetInvalidate(hwnd);
                }
            }
            else if (instance->kind == ROS_WIDGET_KIND_TEXTBOX) {
                unsigned long caret_index = widget_textbox_hit_test_caret(instance, pointer_x);

                if (caret_index != instance->caret_index || !instance->has_focus) {
                    widget_textbox_set_caret(instance, caret_index);
                    (void)WidgetInvalidate(hwnd);
                }
            }
        }
        else if (message == WM_LBUTTONUP) {
            if (instance->kind == ROS_WIDGET_KIND_BUTTON && instance->pointer_down) {
                instance->pointer_down = 0;
                (void)WidgetInvalidate(hwnd);
            }
        }
        else if (message == WM_KEYDOWN && instance->kind == ROS_WIDGET_KIND_TEXTBOX && instance->has_focus) {
            if (widget_textbox_apply_key(instance, wParam)) {
                (void)WidgetInvalidate(hwnd);
                if (user_proc) {
                    (void)SendMessage(hwnd, WM_CHANGED, wParam, lParam);
                }
            }
        }
    }

    if (user_proc) {
        return user_proc(hwnd, message, wParam, lParam);
    }
    return 0L;
}

/*
 * Register one framework-owned class definition and mirror it to window.dll.
 *
 * @param class_name Class name to publish.
 * @param kind Default visual behavior.
 * @param user_proc Optional caller procedure.
 * @return Zero on success, or a negative status code on failure.
 */
static long widget_register_class_internal(const char* class_name, unsigned long kind, WNDPROC user_proc) {
    WidgetClassDefinition* slot;
    long status;

    if (!class_name || class_name[0] == '\0') {
        return WIDGET_STATUS_INVALID;
    }

    slot = widget_find_class(class_name);
    if (slot) {
        slot->kind = kind;
        slot->user_proc = user_proc;
        return ROS_USER_IPC_STATUS_OK;
    }

    slot = widget_reserve_class();
    if (!slot) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    status = CreateWindowClass(class_name, widget_window_proc);
    if (status < 0) {
        return status;
    }

    slot->in_use = 1;
    slot->kind = kind;
    slot->user_proc = user_proc;
    widget_copy_text(slot->class_name, sizeof(slot->class_name), class_name);
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Register all built-in framework classes without recursing through the public
 * initialization entrypoints.
 *
 * @return Zero on success, or a negative status code on failure.
 */
static long widget_register_builtins_internal(void) {
    long status;

    status = widget_register_class_internal(ROS_WIDGET_CLASS_DIALOG, ROS_WIDGET_KIND_DIALOG, 0);
    if (status < 0) {
        return status;
    }
    status = widget_register_class_internal(ROS_WIDGET_CLASS_BUTTON, ROS_WIDGET_KIND_BUTTON, 0);
    if (status < 0) {
        return status;
    }
    status = widget_register_class_internal(ROS_WIDGET_CLASS_TEXTBOX, ROS_WIDGET_KIND_TEXTBOX, 0);
    if (status < 0) {
        return status;
    }
    return widget_register_class_internal(ROS_WIDGET_CLASS_LABEL, ROS_WIDGET_KIND_LABEL, 0);
}

/*
 * Ensure the built-in class set exists before creating controls.
 *
 * @return Zero on success, or a negative status code on failure.
 */
static long widget_ensure_initialized(void) {
    return WidgetInitialize();
}

long WidgetInitialize(void) {
    long status;

    if (g_widget_initialized) {
        return ROS_USER_IPC_STATUS_OK;
    }

    status = widget_register_builtins_internal();
    if (status < 0) {
        return status;
    }

    g_widget_initialized = 1;
    return ROS_USER_IPC_STATUS_OK;
}

long WidgetRegisterBuiltinClasses(void) {
    return WidgetInitialize();
}

/*
 * Load one explicit widget text font for the current process so app code can
 * make the selected UI face visible and testable instead of relying on an
 * implicit first-draw decision.
 *
 * @param path Optional absolute VFS path to a preferred `.rtf` file, or a
 * `.font` / legacy `.ttf` path when explicitly requested.
 * @param pixel_height Requested text height, or zero for the framework default.
 * @return Zero on success, or a negative status code on failure.
 */
long WidgetSetFont(const char* path, unsigned long pixel_height) {
    unsigned long effective_height = pixel_height != 0UL ? pixel_height : WIDGET_DEFAULT_FONT_HEIGHT;
    const char* effective_path = (path && path[0] != '\0') ? path : 0;
    long status;

    widget_reset_font_state();
    status = GdiLoadFont(effective_path, effective_height, &g_widget_font);
    if (status < 0L) {
        return status;
    }

    g_widget_font_attempted = 1;
    g_widget_font_loaded = 1;
    return ROS_USER_IPC_STATUS_OK;
}

long WidgetRegisterClass(const char* class_name, unsigned long kind, WNDPROC user_proc) {
    long status = widget_ensure_initialized();

    if (status < 0) {
        return status;
    }

    return widget_register_class_internal(class_name, kind, user_proc);
}

HWND WidgetCreateWindow(const char* class_name, const char* title, HWND parent, long x, long y, unsigned long width, unsigned long height, unsigned long style) {
    WidgetClassDefinition* class_definition;
    WidgetInstance* instance;
    WindowCreateParams params;
    HWND hwnd;

    if (widget_ensure_initialized() < 0) {
        return 0UL;
    }

    class_definition = widget_find_class(class_name);
    if (!class_definition) {
        return 0UL;
    }

    instance = widget_reserve_instance();
    if (!instance) {
        return 0UL;
    }

    params.class_name = class_name;
    params.title = title;
    params.parent = parent;
    params.x = x;
    params.y = y;
    params.width = width;
    params.height = height;
    params.style = style;
    hwnd = CreateWindowEx(&params);
    if (hwnd == 0UL) {
        widget_release_instance_record(instance);
        return 0UL;
    }

    instance->hwnd = hwnd;
    instance->parent = parent;
    instance->kind = class_definition->kind;
    instance->style = style;
    instance->x = x;
    instance->y = y;
    instance->width = width;
    instance->height = height;
    instance->user_proc = class_definition->user_proc;
    widget_copy_text(instance->class_name, sizeof(instance->class_name), class_name);
    widget_copy_text(instance->text, sizeof(instance->text), title);
    if (instance->kind == ROS_WIDGET_KIND_TEXTBOX) {
        widget_textbox_set_caret(instance, widget_text_length(instance->text, sizeof(instance->text)));
    }
    return hwnd;
}

HWND WidgetCreateDialog(const char* class_name, const char* title, long x, long y, unsigned long width, unsigned long height) {
    return WidgetCreateWindow(class_name, title, 0UL, x, y, width, height, ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_DECORATED);
}

HWND WidgetCreateButton(HWND parent, const char* title, long x, long y, unsigned long width, unsigned long height) {
    return WidgetCreateWindow(ROS_WIDGET_CLASS_BUTTON, title, parent, x, y, width, height, ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_CHILD | ROS_WINDOW_STYLE_BORDER);
}

HWND WidgetCreateLabel(HWND parent, const char* title, long x, long y, unsigned long width, unsigned long height) {
    return WidgetCreateWindow(ROS_WIDGET_CLASS_LABEL, title, parent, x, y, width, height, ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_CHILD);
}

HWND WidgetCreateTextBox(HWND parent, const char* title, long x, long y, unsigned long width, unsigned long height) {
    return WidgetCreateWindow(ROS_WIDGET_CLASS_TEXTBOX, title, parent, x, y, width, height, ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_CHILD | ROS_WINDOW_STYLE_BORDER);
}

long WidgetSetText(HWND hwnd, const char* text) {
    WidgetInstance* instance = widget_find_instance(hwnd);

    if (!instance) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    widget_copy_text(instance->text, sizeof(instance->text), text);
    if (instance->kind == ROS_WIDGET_KIND_TEXTBOX) {
        widget_textbox_set_caret(instance, widget_text_length(instance->text, sizeof(instance->text)));
    }
    return WidgetInvalidate(hwnd);
}

long WidgetSetBounds(HWND hwnd, long x, long y, unsigned long width, unsigned long height) {
    WidgetInstance* instance = widget_find_instance(hwnd);
    long status;

    if (!instance) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    status = MoveWindow(hwnd, x, y, width, height);
    if (status < 0L) {
        return status;
    }

    instance->x = x;
    instance->y = y;
    instance->width = width;
    instance->height = height;
    return ROS_USER_IPC_STATUS_OK;
}

const char* WidgetGetText(HWND hwnd) {
    WidgetInstance* instance = widget_find_instance(hwnd);

    if (!instance) {
        return g_widget_empty_text;
    }
    return instance->text;
}

long WidgetBeginPaint(HWND hwnd, RosWidgetPaintContext* context) {
    long status;

    if (!context) {
        return WIDGET_STATUS_INVALID;
    }

    widget_zero_memory(context, sizeof(*context));
    context->hwnd = hwnd;
    status = GdiGetWindowSurface(hwnd, &context->surface);
    if (status < 0) {
        return status;
    }

    return ROS_USER_IPC_STATUS_OK;
}

long WidgetEndPaint(RosWidgetPaintContext* context) {
    long status;

    if (!context || context->hwnd == 0UL) {
        return WIDGET_STATUS_INVALID;
    }

    status = GdiInvalidateRect(context->hwnd, 0UL, 0UL, context->surface.width, context->surface.height);
    if (GdiReleaseWindowSurface(context->hwnd) < 0 && status >= 0) {
        status = WIDGET_STATUS_ERROR;
    }

    widget_zero_memory(context, sizeof(*context));
    return status;
}

long WidgetFillRect(const RosWidgetPaintContext* context, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color) {
    if (!context) {
        return WIDGET_STATUS_INVALID;
    }

    return widget_fill_surface_rect(&context->surface, x, y, width, height, color);
}

long WidgetFrameRect(const RosWidgetPaintContext* context, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long color) {
    if (!context) {
        return WIDGET_STATUS_INVALID;
    }
    if (width == 0UL || height == 0UL) {
        return ROS_USER_IPC_STATUS_OK;
    }

    (void)widget_fill_surface_rect(&context->surface, x, y, width, 1UL, color);
    (void)widget_fill_surface_rect(&context->surface, x, y, 1UL, height, color);
    if (height > 1UL) {
        (void)widget_fill_surface_rect(&context->surface, x, y + height - 1UL, width, 1UL, color);
    }
    if (width > 1UL) {
        (void)widget_fill_surface_rect(&context->surface, x + width - 1UL, y, 1UL, height, color);
    }
    return ROS_USER_IPC_STATUS_OK;
}

long WidgetFillVerticalGradient(const RosWidgetPaintContext* context, unsigned long x, unsigned long y, unsigned long width, unsigned long height, unsigned long top_color, unsigned long bottom_color) {
    unsigned long row;

    if (!context) {
        return WIDGET_STATUS_INVALID;
    }
    if (width == 0UL || height == 0UL) {
        return ROS_USER_IPC_STATUS_OK;
    }

    for (row = 0UL; row < height; ++row) {
        unsigned long factor = (height <= 1UL) ? 255UL : ((row * 255UL) / (height - 1UL));
        unsigned long color = widget_blend_color(top_color, bottom_color, factor);
        (void)widget_fill_surface_rect(&context->surface, x, y + row, width, 1UL, color);
    }

    return ROS_USER_IPC_STATUS_OK;
}

long WidgetDrawText(const RosWidgetPaintContext* context, unsigned long x, unsigned long y, const char* text, unsigned long color) {
    if (!context) {
        return WIDGET_STATUS_INVALID;
    }

    return widget_draw_text_surface(&context->surface, x, y, text, color);
}

long WidgetDrawCenteredText(const RosWidgetPaintContext* context, unsigned long x, unsigned long y, unsigned long width, unsigned long height, const char* text, unsigned long color) {
    unsigned long text_width;
    unsigned long text_height = widget_text_height();
    unsigned long text_x;
    unsigned long text_y;

    if (!context) {
        return WIDGET_STATUS_INVALID;
    }

    text_width = widget_measure_text(text);
    text_x = x + ((width > text_width) ? ((width - text_width) / 2UL) : 2UL);
    text_y = y + ((height > text_height) ? ((height - text_height) / 2UL) : 0UL);
    return widget_draw_text_surface(&context->surface, text_x, text_y, text, color);
}

long WidgetPaintDialogBackground(const RosWidgetPaintContext* context) {
    if (!context) {
        return WIDGET_STATUS_INVALID;
    }

    return widget_paint_dialog_surface(&context->surface);
}

long WidgetInvalidate(HWND hwnd) {
    return PostMessage(hwnd, WM_PAINT, 0UL, 0UL);
}

/*
 * widgets_entry
 *
 * Keep widget class tables and per-window metadata scoped to the current DLL
 * image so every process attach starts from a clean framework state.
 *
 * @param image_base Base address where the DLL is mapped in the current process.
 * @param reason Loader notification reason.
 * @return Non-zero success code for the loader.
 */
int widgets_entry(void* image_base, U32 reason) {
    (void)image_base;

    if ((reason == DLL_REASON_PROCESS_ATTACH) || (reason == DLL_REASON_PROCESS_DETACH)) {
        widget_reset_state();
        return 1;
    }

    return 1;
}