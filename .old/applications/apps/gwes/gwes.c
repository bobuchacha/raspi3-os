#include "user_runtime.h"
#include "app/windowkit.h"
#include "logger.h"

#define GWES_BACKGROUND 0x000B1220U
#define GWES_TICK_MSEC 20UL
#define GWES_SHARED_INPUT_LOG_PERIOD_MSEC 100UL
#define GWES_SHARED_INPUT_IDLE_LOG_PERIOD_MSEC 2000UL
#define GWES_RAW_LOG_MAX 8U
#define GWES_FONT_SAMPLE_GRID 4U

typedef struct GwesFontStyleStruct {
    int32_t glyphWidth;
    int32_t glyphHeight;
    int32_t advance;
    int32_t lineHeight;
} GwesFontStyle;

typedef struct GwesRawEventSampleStruct {
    RosKernelGuiInputEvent event;
    uint32_t sequence;
} GwesRawEventSample;

typedef struct GwesStateStruct {
    WindowKitContext ui;
    HWND desktopWindow;
    HWND headlineLabel;
    HWND architecturePanel;
    HWND pingButton;
    HWND textBox;
    HWND monitorWindow;
    HWND monitorPanel;
    RosKernelGuiSharedInputRegion* sharedInput;
    uint32_t sharedInputConsumerIndex;
    int sharedInputAttached;
    unsigned int clickCount;
    unsigned int dispatchCount;
    unsigned int pollCount;
    unsigned int idlePollCount;
    unsigned int rawEventCount;
    unsigned int rawPointerCount;
    unsigned int rawKeyCount;
    unsigned int lastQueueDepth;
    uint32_t lastMessage;
    HWND lastTarget;
    RosKernelGuiInputEvent lastRawEvent;
    GwesRawEventSample rawLog[GWES_RAW_LOG_MAX];
    uint32_t rawLogNext;
    uint32_t rawLogCount;
    unsigned long sharedInputLastLogMsec;
    unsigned long frameDeadlineMsec;
    uint64_t sharedInputLastLogPointerCount;
    int shuttingDown;
} GwesState;

static GwesState g_gwes_state;
static const GwesFontStyle g_gwes_heading_font = { 8, 12, 9, 16 };
static const GwesFontStyle g_gwes_body_font = { 7, 11, 8, 14 };

static void gwes_shared_input_barrier(void) {
    asm volatile("dmb ishld" ::: "memory");
}

static int gwes_shared_input_acquire(void) {
    writeLog("gwes_shared_input_acquire\n");

    RosKernelGuiSharedInputView view;

    if (controlGui(ROS_KERNEL_GUI_CONTROL_SHARED_INPUT_ACQUIRE, (unsigned long)&view) != 0) {
        return -1;
    }
    if (view.version != ROS_KERNEL_GUI_SHARED_INPUT_VERSION ||
        view.view_address == 0ULL ||
        view.consumer_index >= ROS_KERNEL_GUI_SHARED_INPUT_MAX_CONSUMERS ||
        view.view_size < sizeof(RosKernelGuiSharedInputRegion)) {
        return -1;
    }

    g_gwes_state.sharedInput = (RosKernelGuiSharedInputRegion*)(uintptr_t)view.view_address;
    g_gwes_state.sharedInputConsumerIndex = view.consumer_index;
    g_gwes_state.sharedInputAttached = 1;
    app_log_info("GWES", "shared input attached consumer=%lu addr=%lu size=%lu",
        (unsigned long)view.consumer_index,
        (unsigned long)view.view_address,
        (unsigned long)view.view_size);
    return 0;
}

static void gwes_shared_input_release(void) {
    if (!g_gwes_state.sharedInputAttached) {
        return;
    }

    (void)controlGui(ROS_KERNEL_GUI_CONTROL_SHARED_INPUT_RELEASE, 0UL);
    g_gwes_state.sharedInput = 0;
    g_gwes_state.sharedInputConsumerIndex = 0U;
    g_gwes_state.sharedInputAttached = 0;
}

static void gwes_shared_input_log_mouse_probe(void) {
    static uint64_t last_key_event_count;
    static uint64_t last_produced_count;
    static uint32_t last_pointer_x;
    static uint32_t last_pointer_y;
    static uint32_t last_pointer_pressed;
    static uint32_t last_pointer_visible;
    static int have_snapshot;
    volatile RosKernelGuiSharedInputRegion* region;
    volatile RosKernelGuiSharedInputConsumer* consumer;
    const char* phase;
    unsigned long now;
    unsigned long min_period;
    uint64_t pointer_event_count;
    uint64_t tail_sequence;
    uint64_t head_sequence;
    uint64_t drop_count;
    uint64_t produced_count;
    uint64_t key_event_count;
    uint64_t overflow_count;
    uint32_t pointer_x;
    uint32_t pointer_y;
    uint32_t pointer_pressed;
    uint32_t pointer_visible;
    int changed;

    if (!g_gwes_state.sharedInputAttached || !g_gwes_state.sharedInput) {
        return;
    }

    region = (volatile RosKernelGuiSharedInputRegion*)g_gwes_state.sharedInput;
    consumer = &region->consumers[g_gwes_state.sharedInputConsumerIndex];

    gwes_shared_input_barrier();
    pointer_event_count = region->pointer_event_count;
    tail_sequence = region->tail_sequence;
    head_sequence = consumer->head_sequence;
    drop_count = consumer->drop_count;
    produced_count = region->produced_count;
    key_event_count = region->key_event_count;
    overflow_count = region->overflow_count;
    pointer_x = region->last_pointer_state.x;
    pointer_y = region->last_pointer_state.y;
    pointer_pressed = region->last_pointer_state.pressed;
    pointer_visible = region->last_pointer_state.visible;

    changed = !have_snapshot ||
        pointer_event_count != g_gwes_state.sharedInputLastLogPointerCount ||
        key_event_count != last_key_event_count ||
        produced_count != last_produced_count ||
        pointer_x != last_pointer_x ||
        pointer_y != last_pointer_y ||
        pointer_pressed != last_pointer_pressed ||
        pointer_visible != last_pointer_visible;

    now = getUptimeMs();
    min_period = changed ? GWES_SHARED_INPUT_LOG_PERIOD_MSEC : GWES_SHARED_INPUT_IDLE_LOG_PERIOD_MSEC;
    if (g_gwes_state.sharedInputLastLogMsec != 0UL &&
        (now - g_gwes_state.sharedInputLastLogMsec) < min_period) {
        return;
    }

    g_gwes_state.sharedInputLastLogMsec = now;
    phase = changed ? (pointer_event_count != g_gwes_state.sharedInputLastLogPointerCount ? "mouse" : "state") : "idle";
    g_gwes_state.sharedInputLastLogPointerCount = pointer_event_count;
    last_key_event_count = key_event_count;
    last_produced_count = produced_count;
    last_pointer_x = pointer_x;
    last_pointer_y = pointer_y;
    last_pointer_pressed = pointer_pressed;
    last_pointer_visible = pointer_visible;
    have_snapshot = 1;

    app_log_info("GWES", "shm %s ptr=%lu key=%lu tail=%lu head=%lu produced=%lu x=%lu y=%lu pressed=%lu visible=%lu drops=%lu overflow=%lu",
        phase,
        (unsigned long)pointer_event_count,
        (unsigned long)key_event_count,
        (unsigned long)tail_sequence,
        (unsigned long)head_sequence,
        (unsigned long)produced_count,
        (unsigned long)pointer_x,
        (unsigned long)pointer_y,
        (unsigned long)pointer_pressed,
        (unsigned long)pointer_visible,
        (unsigned long)drop_count,
        (unsigned long)overflow_count);
}

static const char* gwes_message_name(uint32_t message) {
    switch (message) {
    case WM_NULL:
        return "WM_NULL";
    case WM_CREATE:
        return "WM_CREATE";
    case WM_DESTROY:
        return "WM_DESTROY";
    case WM_REPAINT:
        return "WM_REPAINT";
    case WM_MOUSEMOVE:
        return "WM_MOUSEMOVE";
    case WM_MOUSELEAVE:
        return "WM_MOUSELEAVE";
    case WM_LBUTTONDOWN:
        return "WM_LBUTTONDOWN";
    case WM_LBUTTONUP:
        return "WM_LBUTTONUP";
    case WM_MOUSECLICKED:
        return "WM_MOUSECLICKED";
    case WM_KEYDOWN:
        return "WM_KEYDOWN";
    case WM_CLOSE:
        return "WM_CLOSE";
    case WM_MOVE:
        return "WM_MOVE";
    case WM_SIZE:
        return "WM_SIZE";
    case WM_CHANGED:
        return "WM_CHANGED";
    case WM_PAINT:
        return "WM_PAINT";
    default:
        return "WM_UNKNOWN";
    }
}

static const char* gwes_input_event_name(uint32_t type) {
    switch (type) {
    case ROS_KERNEL_GUI_INPUT_EVENT_POINTER_MOVE:
        return "MOVE";
    case ROS_KERNEL_GUI_INPUT_EVENT_POINTER_DOWN:
        return "DOWN";
    case ROS_KERNEL_GUI_INPUT_EVENT_POINTER_UP:
        return "UP";
    case ROS_KERNEL_GUI_INPUT_EVENT_POINTER_LEAVE:
        return "LEAVE";
    case ROS_KERNEL_GUI_INPUT_EVENT_KEY_DOWN:
        return "KEYDOWN";
    case ROS_KERNEL_GUI_INPUT_EVENT_KEY_UP:
        return "KEYUP";
    default:
        return "NONE";
    }
}

static const char* gwes_class_name(uint32_t classId) {
    switch (classId) {
    case WINDOWKIT_CLASS_WINDOW:
        return "WINDOW";
    case WINDOWKIT_CLASS_LABEL:
        return "LABEL";
    case WINDOWKIT_CLASS_BUTTON:
        return "BUTTON";
    case WINDOWKIT_CLASS_TEXTBOX:
        return "TEXTBOX";
    case WINDOWKIT_CLASS_LIST:
        return "LIST";
    case WINDOWKIT_CLASS_PANEL:
        return "PANEL";
    default:
        return "NODE";
    }
}

static uint32_t gwes_decode_color(uint32_t encoded) {
    if (g_windowkit_display.pixelFormat != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
        return encoded;
    }

    return ((encoded & 0x0000FFU) << 16) | (encoded & 0x00FF00U) | ((encoded & 0x00FF0000U) >> 16);
}

static uint32_t gwes_blend_color(uint32_t base, uint32_t top, uint32_t alpha) {
    uint32_t inverse = 255U - alpha;
    uint32_t red = ((((base >> 16) & 0xFFU) * inverse) + (((top >> 16) & 0xFFU) * alpha) + 127U) / 255U;
    uint32_t green = ((((base >> 8) & 0xFFU) * inverse) + (((top >> 8) & 0xFFU) * alpha) + 127U) / 255U;
    uint32_t blue = (((base & 0xFFU) * inverse) + ((top & 0xFFU) * alpha) + 127U) / 255U;

    return (red << 16) | (green << 8) | blue;
}

static void gwes_dc_blend_pixel(const WindowKitDC* dc, int32_t x, int32_t y, uint32_t color, uint32_t alpha) {
    int32_t absolute_x;
    int32_t absolute_y;
    int32_t band_start;
    int32_t band_end;
    uint32_t pitch_pixels;
    uint32_t* pixel;

    if (!dc || !g_windowkit_display.pixels || alpha == 0U) {
        return;
    }
    if (x < 0 || y < 0 || x >= (int32_t)dc->bounds.width || y >= (int32_t)dc->bounds.height) {
        return;
    }

    absolute_x = (int32_t)dc->bounds.x + x;
    absolute_y = (int32_t)dc->bounds.y + y;
    if (absolute_x < 0 || absolute_x >= (int32_t)g_windowkit_display.width) {
        return;
    }

    band_start = (int32_t)g_windowkit_display.bandY;
    band_end = band_start + (int32_t)g_windowkit_display.bandHeight;
    if (absolute_y < band_start || absolute_y >= band_end) {
        return;
    }

    pitch_pixels = g_windowkit_display.pitch / sizeof(uint32_t);
    pixel = g_windowkit_display.pixels + ((unsigned long)(absolute_y - band_start) * pitch_pixels) + (unsigned long)absolute_x;
    if (alpha >= 255U) {
        *pixel = windowKitEncodeColor(&g_windowkit_display, color);
        return;
    }

    *pixel = windowKitEncodeColor(&g_windowkit_display, gwes_blend_color(gwes_decode_color(*pixel), color, alpha));
}

static uint32_t gwes_glyph_row_bits(uint16_t bits, unsigned int row) {
    return (bits >> ((WINDOWKIT_FONT_HEIGHT - 1U - row) * WINDOWKIT_FONT_WIDTH)) & ((1U << WINDOWKIT_FONT_WIDTH) - 1U);
}

static uint32_t gwes_glyph_coverage(uint16_t bits, int32_t pixel_x, int32_t pixel_y, const GwesFontStyle* style) {
    uint32_t hits = 0U;
    uint32_t grid_width;
    uint32_t grid_height;

    if (!style || style->glyphWidth <= 0 || style->glyphHeight <= 0) {
        return 0U;
    }

    grid_width = (uint32_t)style->glyphWidth * GWES_FONT_SAMPLE_GRID;
    grid_height = (uint32_t)style->glyphHeight * GWES_FONT_SAMPLE_GRID;
    for (uint32_t sample_y = 0U; sample_y < GWES_FONT_SAMPLE_GRID; sample_y++) {
        uint32_t source_y = ((((uint32_t)pixel_y * GWES_FONT_SAMPLE_GRID) + sample_y) * WINDOWKIT_FONT_HEIGHT) / grid_height;
        uint32_t row_bits = gwes_glyph_row_bits(bits, source_y);

        for (uint32_t sample_x = 0U; sample_x < GWES_FONT_SAMPLE_GRID; sample_x++) {
            uint32_t source_x = ((((uint32_t)pixel_x * GWES_FONT_SAMPLE_GRID) + sample_x) * WINDOWKIT_FONT_WIDTH) / grid_width;

            if ((row_bits & (1U << (WINDOWKIT_FONT_WIDTH - 1U - source_x))) != 0U) {
                hits++;
            }
        }
    }

    return ((hits * 255U) + ((GWES_FONT_SAMPLE_GRID * GWES_FONT_SAMPLE_GRID) / 2U)) /
        (GWES_FONT_SAMPLE_GRID * GWES_FONT_SAMPLE_GRID);
}

static void gwes_dc_draw_glyph_scaled(const WindowKitDC* dc, int32_t x, int32_t y, char ch, uint32_t color, uint32_t alpha, const GwesFontStyle* style) {
    uint16_t bits = windowKitGlyphBits(ch);

    if (!dc || !style || alpha == 0U) {
        return;
    }

    for (int32_t row = 0; row < style->glyphHeight; row++) {
        for (int32_t column = 0; column < style->glyphWidth; column++) {
            uint32_t coverage = gwes_glyph_coverage(bits, column, row, style);

            if (coverage != 0U) {
                gwes_dc_blend_pixel(dc, x + column, y + row, color, (coverage * alpha) / 255U);
            }
        }
    }
}

static int32_t gwes_dc_draw_text_scaled(const WindowKitDC* dc, int32_t x, int32_t y, const char* text, uint32_t color, const GwesFontStyle* style) {
    int32_t cursor_x = x;
    int32_t cursor_y = y;

    if (!dc || !text || !style) {
        return x;
    }

    while (*text != '\0') {
        if (*text == '\n') {
            cursor_x = x;
            cursor_y += style->lineHeight;
        }
        else {
            gwes_dc_draw_glyph_scaled(dc, cursor_x + 1, cursor_y + 1, *text, 0x00000000U, 96U, style);
            gwes_dc_draw_glyph_scaled(dc, cursor_x, cursor_y, *text, color, 255U, style);
            cursor_x += style->advance;
        }
        text++;
    }

    return cursor_x;
}

static unsigned int gwes_top_level_count(void) {
    unsigned int count = 0U;

    for (uint32_t index = 0U; index < g_gwes_state.ui.nodeCount; index++) {
        const WindowKitNode* node = &g_gwes_state.ui.nodes[index];

        if (node->handle != WINDOWKIT_NULL_HWND && node->parent == WINDOWKIT_NULL_HWND) {
            count++;
        }
    }

    return count;
}

static char* gwes_append_node_label(char* cursor, HWND hwnd) {
    const WindowKitNode* node = windowKitFindNodeConst(&g_gwes_state.ui, hwnd);

    if (!cursor) {
        return 0;
    }
    if (!node) {
        return appendText(cursor, "NONE");
    }

    cursor = appendText(cursor, node->text[0] != '\0' ? node->text : gwes_class_name(node->classId));
    cursor = appendText(cursor, " ID=");
    return appendUnsignedLong(cursor, hwnd);
}

static void gwes_format_raw_event(char* buffer, unsigned long size, const RosKernelGuiInputEvent* event, uint32_t sequence) {
    char* cursor;

    if (!buffer || size == 0UL) {
        return;
    }
    if (!event || sequence == 0U || event->type == ROS_KERNEL_GUI_INPUT_EVENT_NONE) {
        windowKitCopyText(buffer, size, "WAITING FOR INPUT");
        return;
    }

    cursor = buffer;
    cursor = appendUnsignedLong(cursor, sequence);
    cursor = appendText(cursor, " ");
    cursor = appendText(cursor, gwes_input_event_name(event->type));
    if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_KEY_DOWN || event->type == ROS_KERNEL_GUI_INPUT_EVENT_KEY_UP) {
        cursor = appendText(cursor, " KEY=");
        cursor = appendUnsignedLong(cursor, event->key);
    }
    else {
        cursor = appendText(cursor, " X=");
        cursor = appendUnsignedLong(cursor, event->x);
        cursor = appendText(cursor, " Y=");
        cursor = appendUnsignedLong(cursor, event->y);
        cursor = appendText(cursor, " B=");
        cursor = appendUnsignedLong(cursor, event->buttons);
    }
    *cursor = '\0';
}

static void gwes_record_raw_event(const RosKernelGuiInputEvent* event) {
    GwesRawEventSample* sample;

    if (!event || event->type == ROS_KERNEL_GUI_INPUT_EVENT_NONE) {
        return;
    }

    g_gwes_state.rawEventCount++;
    if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_KEY_DOWN || event->type == ROS_KERNEL_GUI_INPUT_EVENT_KEY_UP) {
        g_gwes_state.rawKeyCount++;
    }
    else {
        g_gwes_state.rawPointerCount++;
    }

    g_gwes_state.lastRawEvent = *event;
    sample = &g_gwes_state.rawLog[g_gwes_state.rawLogNext];
    sample->event = *event;
    sample->sequence = g_gwes_state.rawEventCount;
    g_gwes_state.rawLogNext = (g_gwes_state.rawLogNext + 1U) % GWES_RAW_LOG_MAX;
    if (g_gwes_state.rawLogCount < GWES_RAW_LOG_MAX) {
        g_gwes_state.rawLogCount++;
    }
}

static void gwes_draw_flow_connector(const WindowKitDC* dc, int32_t center_x, int32_t top, uint32_t color) {
    windowKitDcFillRect(dc, center_x, top, 2, 10, color);
    windowKitDcFillRect(dc, center_x - 3, top + 10, 8, 2, color);
    windowKitDcFillRect(dc, center_x - 2, top + 12, 6, 2, color);
    windowKitDcFillRect(dc, center_x - 1, top + 14, 4, 2, color);
    windowKitDcFillRect(dc, center_x, top + 16, 2, 2, color);
}

static void gwes_draw_lane(const WindowKitDC* dc, int32_t top, uint32_t accent_color, const char* title, const char* body) {
    int32_t width = (int32_t)dc->bounds.width - 16;

    windowKitDcFillRect(dc, 8, top, width, 32, 0x000F172AU);
    windowKitDcFillRect(dc, 8, top, 4, 32, accent_color);
    windowKitDcFrameRect(dc, 8, top, width, 32, 1, accent_color);
    (void)gwes_dc_draw_text_scaled(dc, 20, top + 6, title, 0x00F8FAFCU, &g_gwes_heading_font);
    (void)gwes_dc_draw_text_scaled(dc, 182, top + 8, body, 0x00CBD5E1U, &g_gwes_body_font);
}

static void gwes_draw_metric_chip(const WindowKitDC* dc, int32_t x, int32_t y, int32_t width, uint32_t accent_color, const char* label, unsigned int value) {
    char value_text[32];
    char* cursor = value_text;

    cursor = appendUnsignedLong(cursor, value);
    *cursor = '\0';

    windowKitDcFillRect(dc, x, y, width, 42, 0x000F172AU);
    windowKitDcFrameRect(dc, x, y, width, 42, 1, accent_color);
    (void)gwes_dc_draw_text_scaled(dc, x + 8, y + 6, label, 0x0094A3B8U, &g_gwes_body_font);
    (void)gwes_dc_draw_text_scaled(dc, x + 8, y + 20, value_text, 0x00F8FAFCU, &g_gwes_heading_font);
}

static void gwes_update_dashboard(void) {
    windowKitInvalidate(&g_gwes_state.ui);
}

static int gwes_get_message_legacy(WindowKitContext* context, MSG* message) {
    if (!context || !message) {
        return -1;
    }

    if (windowKitPeekMessage(context, message) > 0) {
        return 1;
    }

    for (;;) {
        RosKernelGuiInputEvent event;

        g_gwes_state.pollCount++;
        if (guiPollInputEvent(&event) != 0 || event.type == ROS_KERNEL_GUI_INPUT_EVENT_NONE) {
            g_gwes_state.idlePollCount++;
            g_gwes_state.lastQueueDepth = windowKitQueuedMessageCount(context);
            return 0;
        }

        gwes_record_raw_event(&event);
        windowKitTranslateInputEvent(context, &event);
        g_gwes_state.lastQueueDepth = windowKitQueuedMessageCount(context);
        gwes_update_dashboard();
        if (windowKitPeekMessage(context, message) > 0) {
            return 1;
        }
    }
}

static int gwes_get_message(WindowKitContext* context, MSG* message) {
    volatile RosKernelGuiSharedInputRegion* region;
    volatile RosKernelGuiSharedInputConsumer* consumer;

    if (!context || !message) {
        return -1;
    }

    if (!g_gwes_state.sharedInputAttached || !g_gwes_state.sharedInput) {
        return gwes_get_message_legacy(context, message);
    }

    if (windowKitPeekMessage(context, message) > 0) {
        return 1;
    }

    region = (volatile RosKernelGuiSharedInputRegion*)g_gwes_state.sharedInput;
    consumer = &region->consumers[g_gwes_state.sharedInputConsumerIndex];

    g_gwes_state.pollCount++;
    gwes_shared_input_barrier();
    for (;;) {
        uint64_t tail = region->tail_sequence;
        uint64_t oldest = tail > ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY ? (tail - ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY) : 0ULL;
        uint64_t head = consumer->head_sequence;

        if (head < oldest) {
            consumer->drop_count += oldest - head;
            consumer->head_sequence = oldest;
            head = oldest;
        }
        if (head >= tail) {
            consumer->last_seen_msec = (uint32_t)getUptimeMs();
            g_gwes_state.idlePollCount++;
            g_gwes_state.lastQueueDepth = windowKitQueuedMessageCount(context);
            return 0;
        }

        {
            volatile RosKernelGuiSharedInputRecord* record = &region->records[head % ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY];

            if (record->sequence != head) {
                consumer->last_seen_msec = (uint32_t)getUptimeMs();
                g_gwes_state.lastQueueDepth = windowKitQueuedMessageCount(context);
                return 0;
            }

            gwes_record_raw_event((const RosKernelGuiInputEvent*)&record->event);
            windowKitTranslateInputEvent(context, (const RosKernelGuiInputEvent*)&record->event);
            consumer->head_sequence = head + 1ULL;
            consumer->last_seen_msec = (uint32_t)getUptimeMs();
            g_gwes_state.lastQueueDepth = windowKitQueuedMessageCount(context);
            gwes_update_dashboard();
            if (windowKitPeekMessage(context, message) > 0) {
                return 1;
            }
        }

        gwes_shared_input_barrier();
    }
}

static long gwes_root_proc(WindowKitContext* context, HWND hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) {
    (void)hwnd;
    (void)wParam;
    (void)lParam;

    if (message == WM_CLOSE && !g_gwes_state.shuttingDown) {
        g_gwes_state.shuttingDown = 1;
        gwes_shared_input_release();
        windowKitDestroyAll(context);
        (void)windowKitUpdateWindow(context);
        exitProcess(0);
    }

    return 0;
}

static long gwes_ping_proc(WindowKitContext* context, HWND hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) {
    (void)context;
    (void)hwnd;
    (void)wParam;
    (void)lParam;

    if (message == WM_MOUSECLICKED) {
        g_gwes_state.clickCount++;
        gwes_update_dashboard();
    }

    return 0;
}

static long gwes_textbox_proc(WindowKitContext* context, HWND hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) {
    (void)context;
    (void)hwnd;
    (void)wParam;
    (void)lParam;

    if (message == WM_CHANGED) {
        gwes_update_dashboard();
    }

    return 0;
}

static long gwes_architecture_proc(WindowKitContext* context, HWND hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) {
    char raw_line[256];
    char listener_line[128];
    char queue_line[160];
    char route_line[256];
    char* cursor;
    (void)wParam;
    (void)lParam;

    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        WindowKitDC* dc = windowKitBeginPaint(context, hwnd, &paint);

        if (!dc) {
            return 0;
        }

        gwes_format_raw_event(raw_line, sizeof(raw_line), &g_gwes_state.lastRawEvent, g_gwes_state.rawEventCount);

        cursor = listener_line;
        cursor = appendText(cursor, "POLL=");
        cursor = appendUnsignedLong(cursor, g_gwes_state.pollCount);
        cursor = appendText(cursor, " RAW=");
        cursor = appendUnsignedLong(cursor, g_gwes_state.rawEventCount);
        cursor = appendText(cursor, " POINTER=");
        cursor = appendUnsignedLong(cursor, g_gwes_state.rawPointerCount);
        cursor = appendText(cursor, " KEY=");
        cursor = appendUnsignedLong(cursor, g_gwes_state.rawKeyCount);
        *cursor = '\0';

        cursor = queue_line;
        cursor = appendText(cursor, "PENDING=");
        cursor = appendUnsignedLong(cursor, g_gwes_state.lastQueueDepth);
        cursor = appendText(cursor, " IDLE=");
        cursor = appendUnsignedLong(cursor, g_gwes_state.idlePollCount);
        cursor = appendText(cursor, " QUEUE=");
        cursor = appendUnsignedLong(cursor, windowKitQueuedMessageCount(&g_gwes_state.ui));
        cursor = appendText(cursor, " DISPATCH=");
        cursor = appendUnsignedLong(cursor, g_gwes_state.dispatchCount);
        *cursor = '\0';

        cursor = route_line;
        cursor = appendText(cursor, "LAST=");
        cursor = appendText(cursor, gwes_message_name(g_gwes_state.lastMessage));
        cursor = appendText(cursor, " TARGET=");
        cursor = gwes_append_node_label(cursor, g_gwes_state.lastTarget);
        *cursor = '\0';

        windowKitDcFillRect(dc, 0, 0, (int32_t)paint.bounds.width, (int32_t)paint.bounds.height, 0x000B1220U);
        windowKitDcFrameRect(dc, 0, 0, (int32_t)paint.bounds.width, (int32_t)paint.bounds.height, 1, paint.accentColor);

        gwes_draw_lane(dc, 10, 0x001D4ED8U, "INPUT DEVICE", "VIRTIO POINTER AND KEYBOARD");
        gwes_draw_flow_connector(dc, 286, 42, 0x0064748BU);
        gwes_draw_lane(dc, 56, 0x000F766EU, "GUI C RING BUFFER", raw_line);
        gwes_draw_flow_connector(dc, 286, 88, 0x0064748BU);
        gwes_draw_lane(dc, 102, 0x0038BDF8U, "GWES LISTENER", listener_line);
        gwes_draw_flow_connector(dc, 286, 134, 0x0064748BU);
        gwes_draw_lane(dc, 148, 0x00B45309U, "WINDOWKIT QUEUE", queue_line);
        gwes_draw_flow_connector(dc, 286, 180, 0x0064748BU);
        gwes_draw_lane(dc, 194, 0x000EA5E9U, "PAINT AND PRESENT", route_line);
        windowKitEndPaint(context, hwnd, &paint);
    }

    return 0;
}

static long gwes_monitor_proc(WindowKitContext* context, HWND hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) {
    (void)wParam;
    (void)lParam;

    if (message == WM_PAINT) {
        char line[256];
        char* cursor;
        int32_t line_y;
        const WindowKitNode* text_node = windowKitFindNodeConst(&g_gwes_state.ui, g_gwes_state.textBox);
        PAINTSTRUCT paint;
        WindowKitDC* dc = windowKitBeginPaint(context, hwnd, &paint);

        if (!dc) {
            return 0;
        }

        windowKitDcFillRect(dc, 0, 0, (int32_t)paint.bounds.width, (int32_t)paint.bounds.height, 0x00111B2EU);
        windowKitDcFrameRect(dc, 0, 0, (int32_t)paint.bounds.width, (int32_t)paint.bounds.height, 1, paint.accentColor);

        gwes_draw_metric_chip(dc, 10, 10, 84, 0x001D4ED8U, "POLL", g_gwes_state.pollCount);
        gwes_draw_metric_chip(dc, 102, 10, 84, 0x000F766EU, "RAW", g_gwes_state.rawEventCount);
        gwes_draw_metric_chip(dc, 194, 10, 84, 0x00B45309U, "DISPATCH", g_gwes_state.dispatchCount);

        cursor = line;
        cursor = appendText(cursor, "FOCUS=");
        cursor = gwes_append_node_label(cursor, windowKitFocusedWindow(&g_gwes_state.ui));
        *cursor = '\0';
        (void)gwes_dc_draw_text_scaled(dc, 12, 64, line, 0x00E2E8F0U, &g_gwes_body_font);

        cursor = line;
        cursor = appendText(cursor, "HOVER=");
        cursor = gwes_append_node_label(cursor, windowKitHoveredWindow(&g_gwes_state.ui));
        *cursor = '\0';
        (void)gwes_dc_draw_text_scaled(dc, 12, 78, line, 0x00CBD5E1U, &g_gwes_body_font);

        cursor = line;
        cursor = appendText(cursor, "PENDING=");
        cursor = appendUnsignedLong(cursor, windowKitQueuedMessageCount(&g_gwes_state.ui));
        cursor = appendText(cursor, " IDLE=");
        cursor = appendUnsignedLong(cursor, g_gwes_state.idlePollCount);
        cursor = appendText(cursor, " LAST=");
        cursor = appendText(cursor, gwes_message_name(g_gwes_state.lastMessage));
        *cursor = '\0';
        (void)gwes_dc_draw_text_scaled(dc, 12, 92, line, 0x00CBD5E1U, &g_gwes_body_font);

        cursor = line;
        cursor = appendText(cursor, "TEXT=");
        cursor = appendText(cursor, text_node ? text_node->text : "");
        *cursor = '\0';
        (void)gwes_dc_draw_text_scaled(dc, 12, 106, line, 0x00CBD5E1U, &g_gwes_body_font);

        windowKitDcFillRect(dc, 8, 124, (int32_t)paint.bounds.width - 16, (int32_t)paint.bounds.height - 132, 0x000F172AU);
        windowKitDcFrameRect(dc, 8, 124, (int32_t)paint.bounds.width - 16, (int32_t)paint.bounds.height - 132, 1, 0x000F766EU);
        (void)gwes_dc_draw_text_scaled(dc, 16, 132, "RAW RING BUFFER", 0x00F8FAFCU, &g_gwes_heading_font);

        line_y = 150;
        if (g_gwes_state.rawLogCount == 0U) {
            (void)gwes_dc_draw_text_scaled(dc, 16, line_y, "WAITING FOR INPUT", 0x0094A3B8U, &g_gwes_body_font);
        }
        else {
            for (uint32_t log_index = 0U; log_index < g_gwes_state.rawLogCount; log_index++) {
                uint32_t index = (g_gwes_state.rawLogNext + GWES_RAW_LOG_MAX - 1U - log_index) % GWES_RAW_LOG_MAX;
                uint32_t color = log_index == 0U ? 0x00F8FAFCU : 0x00CBD5E1U;

                if (line_y + g_gwes_body_font.glyphHeight > (int32_t)paint.bounds.height - 8) {
                    break;
                }
                gwes_format_raw_event(line, sizeof(line), &g_gwes_state.rawLog[index].event, g_gwes_state.rawLog[index].sequence);
                (void)gwes_dc_draw_text_scaled(dc, 16, line_y, line, color, &g_gwes_body_font);
                line_y += g_gwes_body_font.lineHeight;
            }
        }

        windowKitEndPaint(context, hwnd, &paint);
    }

    return 0;
}

static long gwes_noop_proc(WindowKitContext* context, HWND hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) {
    (void)context;
    (void)hwnd;
    (void)message;
    (void)wParam;
    (void)lParam;
    return 0;
}

static void gwes_init_ui(void) {
    RosKernelGuiWidgetRect desktopRect = { 44U, 44U, 620U, 392U };
    RosKernelGuiWidgetRect monitorRect = { 692U, 60U, 320U, 340U };

    windowKitInitContext(&g_gwes_state.ui, GWES_BACKGROUND);
    g_gwes_state.desktopWindow = windowKitCreateWindow(
        &g_gwes_state.ui,
        WINDOWKIT_NULL_HWND,
        WINDOWKIT_CLASS_WINDOW,
        "GWES SHELL",
        desktopRect,
        WINDOWKIT_STYLE_VISIBLE |
        WINDOWKIT_STYLE_ACTIVE |
        WINDOWKIT_STYLE_MOVABLE |
        WINDOWKIT_STYLE_RESIZABLE |
        WINDOWKIT_STYLE_CLOSE_BUTTON,
        gwes_root_proc);
    windowKitSetMinSize(&g_gwes_state.ui, g_gwes_state.desktopWindow, 520U, 360U);
    windowKitSetColors(&g_gwes_state.ui, g_gwes_state.desktopWindow, 0x00111B2EU, 0x00F8FAFCU, 0x001D4ED8U);

    g_gwes_state.headlineLabel = windowKitCreateWindow(
        &g_gwes_state.ui,
        g_gwes_state.desktopWindow,
        WINDOWKIT_CLASS_LABEL,
        "RAW INPUT RING BUFFER TO WM DISPATCH",
        (RosKernelGuiWidgetRect) {
        16U, 16U, 540U, 18U
    },
        WINDOWKIT_STYLE_VISIBLE,
        gwes_noop_proc);
    windowKitSetColors(&g_gwes_state.ui, g_gwes_state.headlineLabel, 0U, 0x00E2E8F0U, 0U);

    g_gwes_state.architecturePanel = windowKitCreateWindow(
        &g_gwes_state.ui,
        g_gwes_state.desktopWindow,
        WINDOWKIT_CLASS_PANEL,
        "",
        (RosKernelGuiWidgetRect) {
        16U, 44U, 580U, 238U
    },
        WINDOWKIT_STYLE_VISIBLE | WINDOWKIT_STYLE_BORDER,
        gwes_architecture_proc);
    windowKitSetColors(&g_gwes_state.ui, g_gwes_state.architecturePanel, 0x00161F2BU, 0x00F8FAFCU, 0x0064748BU);

    g_gwes_state.pingButton = windowKitCreateWindow(
        &g_gwes_state.ui,
        g_gwes_state.desktopWindow,
        WINDOWKIT_CLASS_BUTTON,
        "DISPATCH PING",
        (RosKernelGuiWidgetRect) {
        16U, 300U, 144U, 30U
    },
        WINDOWKIT_STYLE_VISIBLE | WINDOWKIT_STYLE_ENABLED | WINDOWKIT_STYLE_BORDER,
        gwes_ping_proc);
    windowKitSetColors(&g_gwes_state.ui, g_gwes_state.pingButton, 0x001D4ED8U, 0x00F8FAFCU, 0x0094A3B8U);

    g_gwes_state.textBox = windowKitCreateWindow(
        &g_gwes_state.ui,
        g_gwes_state.desktopWindow,
        WINDOWKIT_CLASS_TEXTBOX,
        "TYPE HERE",
        (RosKernelGuiWidgetRect) {
        184U, 300U, 196U, 30U
    },
        WINDOWKIT_STYLE_VISIBLE | WINDOWKIT_STYLE_ENABLED | WINDOWKIT_STYLE_BORDER,
        gwes_textbox_proc);
    windowKitSetColors(&g_gwes_state.ui, g_gwes_state.textBox, 0x000F172AU, 0x00F8FAFCU, 0x0038BDF8U);

    g_gwes_state.monitorWindow = windowKitCreateWindow(
        &g_gwes_state.ui,
        WINDOWKIT_NULL_HWND,
        WINDOWKIT_CLASS_WINDOW,
        "MESSAGE MONITOR",
        monitorRect,
        WINDOWKIT_STYLE_VISIBLE |
        WINDOWKIT_STYLE_MOVABLE |
        WINDOWKIT_STYLE_RESIZABLE |
        WINDOWKIT_STYLE_CLOSE_BUTTON,
        gwes_root_proc);
    windowKitSetMinSize(&g_gwes_state.ui, g_gwes_state.monitorWindow, 300U, 310U);
    windowKitSetColors(&g_gwes_state.ui, g_gwes_state.monitorWindow, 0x00111B2EU, 0x00F8FAFCU, 0x000F766EU);

    g_gwes_state.monitorPanel = windowKitCreateWindow(
        &g_gwes_state.ui,
        g_gwes_state.monitorWindow,
        WINDOWKIT_CLASS_PANEL,
        "",
        (RosKernelGuiWidgetRect) {
        4U, 4U, 288U, 264U
    },
        WINDOWKIT_STYLE_VISIBLE | WINDOWKIT_STYLE_BORDER,
        gwes_monitor_proc);
    windowKitSetColors(&g_gwes_state.ui, g_gwes_state.monitorPanel, 0x00161F2BU, 0x00F8FAFCU, 0x000F766EU);

    gwes_update_dashboard();
}

int AppMain(void) {
    app_log_trace("GWES", "Application is Starting");

    if (openSharedLibrary(ROS_GUI_PATH) == 0UL) {
        writeLine("gwes.exe: failed to load gui.dll");
        exitProcess(1);
    }

    if (guiInit() != 0) {
        writeLine("gwes.exe: gui init failed");
        exitProcess(1);
    }

    (void)gwes_shared_input_acquire();

    gwes_init_ui();
    (void)windowKitUpdateWindow(&g_gwes_state.ui);
    writeLine("gwes.exe: userspace window manager ready");

    for (;;) {
        MSG message;
        unsigned long now;
        int dispatched = 0;

        while (gwes_get_message(&g_gwes_state.ui, &message) > 0) {
            dispatched = 1;
            g_gwes_state.dispatchCount++;
            g_gwes_state.lastMessage = message.message;
            g_gwes_state.lastTarget = message.hwnd;
            g_gwes_state.lastQueueDepth = windowKitQueuedMessageCount(&g_gwes_state.ui);
            gwes_update_dashboard();
            (void)windowKitDispatchMessage(&g_gwes_state.ui, &message);
            gwes_shared_input_log_mouse_probe();
        }

        now = getUptimeMs();
        if (!dispatched && g_gwes_state.frameDeadlineMsec != 0UL && now < g_gwes_state.frameDeadlineMsec) {
            unsigned long sleepMsec = g_gwes_state.frameDeadlineMsec - now;

            if (sleepMsec > GWES_TICK_MSEC) {
                sleepMsec = GWES_TICK_MSEC;
            }
            (void)sleepMs(sleepMsec);
            continue;
        }

        g_gwes_state.frameDeadlineMsec = now + GWES_TICK_MSEC;
        g_gwes_state.lastQueueDepth = windowKitQueuedMessageCount(&g_gwes_state.ui);
        gwes_update_dashboard();
        (void)windowKitUpdateWindow(&g_gwes_state.ui);
        gwes_shared_input_log_mouse_probe();
    }
}