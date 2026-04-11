/*
 * kernel/gui.c
 *
 * Kernel-side GUI transport for the framebuffer-backed desktop.
 *
 * This file is intentionally small in scope. It does not implement a window
 * manager or retain high-level widgets. Instead it provides the low-level GUI
 * ABI that user space talks to:
 * 1. user space asks for display information,
 * 2. user space streams pixels into the framebuffer,
 * 3. user space polls raw keyboard/pointer events,
 * 4. the kernel keeps a software cursor visible by saving and restoring the
 *    pixels underneath it.
 *
 * If you are new to the file, read it in this order:
 * - input queue helpers,
 * - framebuffer and cursor helpers,
 * - public ABI entry points at the bottom.
 */

#include "graphics.h"
#include "gui.h"
#include "log.h"
#include "memory.h"
#include "timer.h"
#include "user-exe.h"

 /* The raw input path uses a fixed ring buffer so event capture never allocates. */
#define GUI_INPUT_QUEUE_MAX 128U

/* The software cursor is a 24x24 bitmap drawn directly into the framebuffer. */
#define GUI_POINTER_BITMAP_WIDTH 12U
#define GUI_POINTER_BITMAP_HEIGHT 18U

/*
 * Saved framebuffer pixels underneath the current cursor image.
 *
 * Because the cursor is composited in software, we must preserve the old
 * background before drawing it. On the next move or redraw we restore this
 * saved block first, then capture the new background, then draw the cursor
 * again at the new location.
 */
typedef struct GuiPointerOverlayStruct {
    unsigned int x;      /* Left edge of the saved rectangle in framebuffer coordinates. */
    unsigned int y;      /* Top edge of the saved rectangle in framebuffer coordinates. */
    unsigned int width;  /* Visible width after clipping against the screen edge. */
    unsigned int height; /* Visible height after clipping against the screen edge. */
    unsigned int active; /* Non-zero once background[] contains valid saved pixels. */
    unsigned int background[GUI_POINTER_BITMAP_WIDTH * GUI_POINTER_BITMAP_HEIGHT];
} GuiPointerOverlay;

/*
 * Cursor outline rows, encoded as 24-bit masks.
 *
 * Bit 23 maps to the left-most pixel and bit 0 maps to the right-most pixel.
 * We use GCC-style binary literals here because they are much easier to tweak
 * visually than hex when adjusting the cursor silhouette.
 */
 // static const unsigned int g_gui_pointer_outline[GUI_POINTER_BITMAP_HEIGHT] = {
 //     0b100000000000000000000000U,
 //     0b110000000000000000000000U,
 //     0b101000000000000000000000U,
 //     0b100100000000000000000000U,
 //     0b100010000000000000000000U,
 //     0b100001000000000000000000U,
 //     0b100000100000000000000000U,
 //     0b100000010000000000000000U,
 //     0b100000001000000000000000U,
 //     0b100000000100000000000000U,
 //     0b100000000010000000000000U,
 //     0b100000000001000000000000U,
 //     0b100000000000100000000000U,
 //     0b100000000000010000000000U,
 //     0b100000000000001000000000U,
 //     0b100000000000000100000000U,
 //     0b100000000100000000000000U,
 //     0b000001001000000000000000U,
 //     0b000000101000000000000000U,
 //     0b000000010100000000000000U,
 //     0b000000001010000000000000U,
 //     0b000000000101000000000000U,
 //     0b000000000011000000000000U,
 //     0b000000000001000000000000U,
 // };

 // /* Cursor fill rows for the same 24x24 arrow shape. */
 // static const unsigned int g_gui_pointer_fill[GUI_POINTER_BITMAP_HEIGHT] = {
 //     0b000000000000000000000000U,
 //     0b000000000000000000000000U,
 //     0b010000000000000000000000U,
 //     0b011000000000000000000000U,
 //     0b011100000000000000000000U,
 //     0b011110000000000000000000U,
 //     0b011111000000000000000000U,
 //     0b011111100000000000000000U,
 //     0b011111110000000000000000U,
 //     0b011111111000000000000000U,
 //     0b011111111100000000000000U,
 //     0b011111111110000000000000U,
 //     0b011111111111000000000000U,
 //     0b011111111111100000000000U,
 //     0b011111111111110000000000U,
 //     0b011111111111111000000000U,
 //     0b011111111000000000000000U,
 //     0b000000110000000000000000U,
 //     0b000000010000000000000000U,
 //     0b000000001000000000000000U,
 //     0b000000000100000000000000U,
 //     0b000000000010000000000000U,
 //     0b000000000000000000000000U,
 //     0b000000000000000000000000U,
 // };

static const unsigned int g_gui_pointer_outline[GUI_POINTER_BITMAP_HEIGHT] = {
   0b100000000000U,
   0b110000000000U,
   0b101000000000U,
   0b100100000000U,
   0b100010000000U,
   0b100001000000U,
   0b100000100000U,
   0b100000010000U,
   0b100000001000U,
   0b100000000100U,
   0b100000000010U,
   0b100111111110U,
   0b111000000011U,
   0b110000000000U,
   0b100000000000U,
   0b000000000000U,
   0b000000000000U,
   0b000000000000U,
};

static const unsigned int g_gui_pointer_fill[GUI_POINTER_BITMAP_HEIGHT] = {
   0b000000000000U,
   0b010000000000U,
   0b011000000000U,
   0b011100000000U,
   0b011110000000U,
   0b011111000000U,
   0b011111100000U,
   0b011111110000U,
   0b011111111000U,
   0b011111111100U,
   0b011111111110U,
   0b011100000000U,
   0b001000000010U,
   0b010000000000U,
   0b000000000000U,
   0b000000000000U,
   0b000000000000U,
   0b000000000000U,
};


/* Ring-buffer storage for raw input events that user space polls later. */
static GuiInputEvent g_gui_input_queue[GUI_INPUT_QUEUE_MAX];
/* Index of the next event to return to the caller. */
static unsigned int g_gui_input_head = 0U;
/* Index of the next free slot for a newly captured event. */
static unsigned int g_gui_input_tail = 0U;
/* Number of valid events currently stored in the ring buffer. */
static unsigned int g_gui_input_count = 0U;
/* Retained pointer position/button state exposed through the raw GUI ABI. */
static GuiPointerState g_gui_pointer_state = { (unsigned int)GUI_POINTER_HIDDEN, (unsigned int)GUI_POINTER_HIDDEN, 0U, 0U };
/* Saved background pixels for the currently drawn cursor image. */
static GuiPointerOverlay g_gui_pointer_overlay;
/* Lazily allocated shared-input page exposed to multiple user processes. */
static Address g_gui_shared_input_page = 0;
static GuiSharedInputRegion* g_gui_shared_input_region_ptr = 0;

typedef char GuiSharedInputRegionPageCheck[(sizeof(GuiSharedInputRegion) <= PAGE_SIZE) ? 1 : -1];

static void gui_shared_input_reset_consumer(GuiSharedInputConsumer* consumer) {
    if (!consumer) {
        return;
    }

    consumer->pid = 0U;
    consumer->flags = 0U;
    consumer->head_sequence = 0UL;
    consumer->drop_count = 0UL;
    consumer->last_seen_msec = 0U;
    consumer->reserved = 0U;
}

static void gui_shared_input_initialize_region(GuiSharedInputRegion* region) {
    UByte* bytes = (UByte*)region;

    if (!region) {
        return;
    }

    for (unsigned int index = 0U; index < PAGE_SIZE; index++) {
        bytes[index] = 0U;
    }

    region->magic = GUI_SHARED_INPUT_MAGIC;
    region->version = GUI_SHARED_INPUT_VERSION;
    region->max_consumers = GUI_SHARED_INPUT_MAX_CONSUMERS;
    region->capacity = GUI_SHARED_INPUT_CAPACITY;
    region->record_size = sizeof(GuiSharedInputRecord);
    region->last_pointer_state = g_gui_pointer_state;
    for (unsigned int index = 0U; index < GUI_SHARED_INPUT_MAX_CONSUMERS; index++) {
        gui_shared_input_reset_consumer(&region->consumers[index]);
    }
}

static GuiSharedInputRegion* gui_shared_input_ensure_region(void) {
    if (g_gui_shared_input_region_ptr) {
        return g_gui_shared_input_region_ptr;
    }

    g_gui_shared_input_page = mem_alloc_page();
    if (!g_gui_shared_input_page) {
        return 0;
    }

    g_gui_shared_input_region_ptr = (GuiSharedInputRegion*)mem_phys_to_virt(g_gui_shared_input_page);
    gui_shared_input_initialize_region(g_gui_shared_input_region_ptr);
    return g_gui_shared_input_region_ptr;
}

static void gui_shared_input_fill_view(const GuiSharedInputConsumer* consumer, unsigned int consumer_index, GuiSharedInputView* view) {
    if (!consumer || !view) {
        return;
    }

    view->version = GUI_SHARED_INPUT_VERSION;
    view->flags = consumer->flags;
    view->view_address = USER_SHARED_INPUT_VIEW_BASE;
    view->view_size = sizeof(GuiSharedInputRegion);
    view->consumer_index = consumer_index;
    view->initial_head_sequence = consumer->head_sequence;
}

static void gui_shared_input_publish_event(unsigned int type, unsigned int x, unsigned int y, unsigned int key, unsigned int buttons) {
    GuiSharedInputRegion* region = g_gui_shared_input_region_ptr;
    GuiSharedInputRecord* record;
    ULong sequence;

    if (!region) {
        return;
    }

    sequence = region->tail_sequence;
    record = &region->records[sequence % GUI_SHARED_INPUT_CAPACITY];
    record->sequence = sequence;
    record->uptime_msec = (unsigned int)(get_system_timer() / 1000UL);
    record->reserved0 = 0U;
    record->event.version = GUI_INPUT_EVENT_VERSION;
    record->event.type = type;
    record->event.x = x;
    record->event.y = y;
    record->event.key = key;
    record->event.buttons = buttons;
    record->event.reserved = 0U;
    record->reserved1 = 0U;

    if (type == GUI_INPUT_EVENT_KEY_DOWN || type == GUI_INPUT_EVENT_KEY_UP) {
        region->key_event_count++;
    }
    else {
        region->pointer_event_count++;
    }
    if (sequence >= GUI_SHARED_INPUT_CAPACITY) {
        region->overflow_count++;
    }

    region->last_pointer_state = g_gui_pointer_state;
    region->produced_count = sequence + 1UL;
    asm volatile("dmb ishst" ::: "memory");
    region->tail_sequence = sequence + 1UL;
}

/* Reset the input ring buffer metadata. Old payload bytes can stay untouched. */
static void gui_queue_reset(void) {
    g_gui_input_head = 0U;
    g_gui_input_tail = 0U;
    g_gui_input_count = 0U;
}

/* Walk one slot backwards in the ring buffer, wrapping at index zero. */
static unsigned int gui_queue_prev_index(unsigned int index) {
    return index == 0U ? (GUI_INPUT_QUEUE_MAX - 1U) : (index - 1U);
}

/*
 * Pointer motion is high frequency, so we collapse the newest move event into
 * the previous move event when possible. Button edges and key edges are never
 * coalesced because those transitions are semantically important.
 */
static int gui_queue_try_coalesce_pointer_move(unsigned int x, unsigned int y, unsigned int buttons) {
    GuiInputEvent* event;
    unsigned int index;

    if (g_gui_input_count == 0U) {
        return 0;
    }

    index = gui_queue_prev_index(g_gui_input_tail);
    event = &g_gui_input_queue[index];
    if (event->type != GUI_INPUT_EVENT_POINTER_MOVE) {
        return 0;
    }

    event->x = x;
    event->y = y;
    event->buttons = buttons;
    return 1;
}

/*
 * If the queue is full, prefer dropping the oldest pointer-move sample instead
 * of losing a key press or button transition. This keeps the queue responsive
 * without sacrificing the events that define interaction edges.
 */
static int gui_queue_discard_oldest_pointer_move(void) {
    unsigned int index = g_gui_input_head;

    for (unsigned int offset = 0U; offset < g_gui_input_count; offset++) {
        GuiInputEvent* event = &g_gui_input_queue[index];

        if (event->type == GUI_INPUT_EVENT_POINTER_MOVE) {
            unsigned int next_index = (index + 1U) % GUI_INPUT_QUEUE_MAX;

            for (unsigned int remaining = offset; remaining + 1U < g_gui_input_count; remaining++) {
                *event = g_gui_input_queue[next_index];
                index = next_index;
                event = &g_gui_input_queue[index];
                next_index = (next_index + 1U) % GUI_INPUT_QUEUE_MAX;
            }

            g_gui_input_tail = gui_queue_prev_index(g_gui_input_tail);
            g_gui_input_count--;
            return 1;
        }

        index = (index + 1U) % GUI_INPUT_QUEUE_MAX;
    }

    return 0;
}

/*
 * Append one input event to the ring buffer.
 *
 * The queue policy is:
 * - coalesce consecutive pointer moves,
 * - when full, discard the oldest pointer move if one exists,
 * - otherwise drop the new event rather than corrupt queue state.
 */
static void gui_queue_push(unsigned int type, unsigned int x, unsigned int y, unsigned int key, unsigned int buttons) {
    GuiInputEvent* event;

    gui_shared_input_publish_event(type, x, y, key, buttons);

    if (type == GUI_INPUT_EVENT_POINTER_MOVE && gui_queue_try_coalesce_pointer_move(x, y, buttons)) {
        return; /* The newest pointer location is now stored in the previous move event. */
    }

    if (g_gui_input_count >= GUI_INPUT_QUEUE_MAX) {
        if (!gui_queue_discard_oldest_pointer_move()) {
            return; /* Keep queue integrity if there is nothing safe to evict. */
        }
    }

    event = &g_gui_input_queue[g_gui_input_tail];
    event->version = GUI_INPUT_EVENT_VERSION;
    event->type = type;
    event->x = x;
    event->y = y;
    event->key = key;
    event->buttons = buttons;
    event->reserved = 0U;
    g_gui_input_tail = (g_gui_input_tail + 1U) % GUI_INPUT_QUEUE_MAX;
    g_gui_input_count++;
}

/* Return a typed pointer to the first pixel in framebuffer row y. */
static unsigned int* gui_framebuffer_row(unsigned int y) {
    return (unsigned int*)(graphics_framebuffer() + ((Address)y * graphics_pitch()));
}

/* Clip the cursor width so we never draw or save past the right edge. */
static unsigned int gui_pointer_clip_width(unsigned int x) {
    unsigned int width = graphics_width();

    if (x >= width) {
        return 0U;
    }
    if (GUI_POINTER_BITMAP_WIDTH > width - x) {
        return width - x;
    }
    return GUI_POINTER_BITMAP_WIDTH;
}

/* Clip the cursor height so we never draw or save past the bottom edge. */
static unsigned int gui_pointer_clip_height(unsigned int y) {
    unsigned int height = graphics_height();

    if (y >= height) {
        return 0U;
    }
    if (GUI_POINTER_BITMAP_HEIGHT > height - y) {
        return height - y;
    }
    return GUI_POINTER_BITMAP_HEIGHT;
}

/* Test one pixel inside a bitmap row, interpreting bit 23 as the left-most pixel. */
static int gui_pointer_mask_contains(const unsigned int* bitmap, unsigned int row, unsigned int column) {
    return (bitmap[row] & (1U << ((GUI_POINTER_BITMAP_WIDTH - 1U) - column))) != 0U;
}

/* Mark the saved cursor background as empty. */
static void gui_pointer_overlay_reset(void) {
    g_gui_pointer_overlay.x = 0U;
    g_gui_pointer_overlay.y = 0U;
    g_gui_pointer_overlay.width = 0U;
    g_gui_pointer_overlay.height = 0U;
    g_gui_pointer_overlay.active = 0U;
}

/*
 * Put the saved framebuffer pixels back where the cursor used to be.
 *
 * This is always the first step before moving the pointer or before user space
 * writes pixels underneath the current cursor image.
 */
static void gui_pointer_restore_background(void) {
    if (!g_gui_pointer_overlay.active || !graphics_is_ready()) {
        return;
    }

    for (unsigned int row = 0U; row < g_gui_pointer_overlay.height; row++) {
        unsigned int* destination = gui_framebuffer_row(g_gui_pointer_overlay.y + row) + g_gui_pointer_overlay.x;
        unsigned int* source = &g_gui_pointer_overlay.background[row * GUI_POINTER_BITMAP_WIDTH];

        for (unsigned int column = 0U; column < g_gui_pointer_overlay.width; column++) {
            destination[column] = source[column];
        }
    }

    gui_pointer_overlay_reset(); /* The saved background has been consumed. */
}

/*
 * Save the pixels underneath the new cursor position so they can be restored
 * later when the cursor moves away.
 */
static void gui_pointer_capture_background(unsigned int x, unsigned int y) {
    unsigned int width;
    unsigned int height;

    gui_pointer_overlay_reset();
    if (!graphics_is_ready()) {
        return;
    }

    width = gui_pointer_clip_width(x);
    height = gui_pointer_clip_height(y);
    if (width == 0U || height == 0U) {
        return;
    }

    g_gui_pointer_overlay.x = x;
    g_gui_pointer_overlay.y = y;
    g_gui_pointer_overlay.width = width;
    g_gui_pointer_overlay.height = height;
    g_gui_pointer_overlay.active = 1U;

    for (unsigned int row = 0U; row < height; row++) {
        unsigned int* source = gui_framebuffer_row(y + row) + x;
        unsigned int* destination = &g_gui_pointer_overlay.background[row * GUI_POINTER_BITMAP_WIDTH];

        for (unsigned int column = 0U; column < width; column++) {
            destination[column] = source[column];
        }
    }
}

/*
 * Draw the cursor bitmap over the saved background.
 *
 * The important detail is the draw order: fill first, outline second. The old
 * outline-first order let overlapping fill pixels erase the border, which made
 * the cursor shape look wrong even when the bitmap masks themselves were valid.
 */
static void gui_pointer_draw_overlay(void) {
    unsigned int fill_color = g_gui_pointer_state.pressed ? 0x00FF00FFU : 0x00FFFFFFU;
    unsigned int outline_color = 0x00FF0000U;

    if (!g_gui_pointer_overlay.active || !graphics_is_ready()) {
        return;
    }

    for (unsigned int row = 0U; row < g_gui_pointer_overlay.height; row++) {
        unsigned int* destination = gui_framebuffer_row(g_gui_pointer_overlay.y + row) + g_gui_pointer_overlay.x;

        for (unsigned int column = 0U; column < g_gui_pointer_overlay.width; column++) {
            int fill = gui_pointer_mask_contains(g_gui_pointer_fill, row, column);
            int outline = gui_pointer_mask_contains(g_gui_pointer_outline, row, column);

            if (fill) {
                destination[column] = fill_color;
            }
            if (outline) {
                destination[column] = outline_color;
            }
        }
    }
}

/*
 * Fully recomposite the software cursor from retained state.
 *
 * The sequence matters:
 * 1. restore the old background,
 * 2. stop if the pointer is hidden,
 * 3. capture the new background,
 * 4. draw the cursor on top of it.
 */
static void gui_pointer_refresh_overlay(void) {
    gui_pointer_restore_background(); /* Remove the old cursor image first. */
    if (!g_gui_pointer_state.visible) {
        return;
    }

    gui_pointer_capture_background(g_gui_pointer_state.x, g_gui_pointer_state.y); /* Save the new background block. */
    gui_pointer_draw_overlay(); /* Draw the cursor over the saved block. */
}

/* Check whether a scanline write overlaps the currently drawn cursor rectangle. */
static int gui_pointer_overlay_intersects(unsigned int x, unsigned int y, unsigned int width, unsigned int height) {
    if (!g_gui_pointer_overlay.active || width == 0U || height == 0U) {
        return 0;
    }

    return x < g_gui_pointer_overlay.x + g_gui_pointer_overlay.width &&
        x + width > g_gui_pointer_overlay.x &&
        y < g_gui_pointer_overlay.y + g_gui_pointer_overlay.height &&
        y + height > g_gui_pointer_overlay.y;
}

/* Return whether the board framebuffer backend is ready for GUI traffic. */
int gui_is_ready(void) {
    return graphics_is_ready();
}

/* Reset both the raw input queue and the retained software cursor state. */
void gui_reset(void) {
    gui_pointer_restore_background(); /* Put the framebuffer back before clearing state. */
    gui_queue_reset(); /* Forget any queued raw input events. */
    if (graphics_is_ready() && graphics_width() != 0U && graphics_height() != 0U) {
        g_gui_pointer_state.x = graphics_width() / 2U;
        g_gui_pointer_state.y = graphics_height() / 2U;
        g_gui_pointer_state.visible = 1U;
    }
    else {
        g_gui_pointer_state.x = (unsigned int)GUI_POINTER_HIDDEN;
        g_gui_pointer_state.y = (unsigned int)GUI_POINTER_HIDDEN;
        g_gui_pointer_state.visible = 0U;
    }
    g_gui_pointer_state.pressed = 0U;
    gui_pointer_overlay_reset(); /* Leave no stale saved background behind. */
    if (g_gui_shared_input_region_ptr) {
        gui_shared_input_initialize_region(g_gui_shared_input_region_ptr);
    }
    gui_pointer_refresh_overlay();
}

/* Queue one keyboard event, snapshotting the current pointer position with it. */
long gui_key(unsigned long key, unsigned long unused) {
    (void)unused;

    gui_queue_push(
        GUI_INPUT_EVENT_KEY_DOWN,
        g_gui_pointer_state.visible ? g_gui_pointer_state.x : 0U,
        g_gui_pointer_state.visible ? g_gui_pointer_state.y : 0U,
        (unsigned int)key,
        g_gui_pointer_state.pressed);
    return 0;
}

/*
 * Update retained pointer state, synthesize input events, and redraw the
 * software cursor.
 *
 * This function is the main state machine in the file. It handles three cases:
 * - the pointer leaves the desktop and becomes hidden,
 * - the pointer moves while still visible,
 * - the primary button changes state.
 */
long gui_pointer_state(unsigned long x, unsigned long y, unsigned long pressed) {
    unsigned int next_buttons = pressed ? 1U : 0U;
    unsigned int previous_buttons = g_gui_pointer_state.pressed;
    unsigned int previous_visible = g_gui_pointer_state.visible;
    unsigned int previous_x = g_gui_pointer_state.x;
    unsigned int previous_y = g_gui_pointer_state.y;

    /*
     * A hidden sentinel means "pointer left the desktop" rather than "pointer
     * moved to a valid off-screen coordinate". Emit the necessary edge events,
     * then remove the cursor overlay and mark the pointer as invisible.
     */
    if (x == GUI_POINTER_HIDDEN || y == GUI_POINTER_HIDDEN) {
        if (previous_visible && previous_buttons != 0U && next_buttons == 0U) {
            gui_queue_push(GUI_INPUT_EVENT_POINTER_UP, previous_x, previous_y, 0U, 0U);
        }
        if (previous_visible) {
            gui_queue_push(GUI_INPUT_EVENT_POINTER_LEAVE, previous_x, previous_y, 0U, next_buttons);
        }
        g_gui_pointer_state.x = (unsigned int)GUI_POINTER_HIDDEN;
        g_gui_pointer_state.y = (unsigned int)GUI_POINTER_HIDDEN;
        g_gui_pointer_state.pressed = next_buttons;
        g_gui_pointer_state.visible = 0U;
        gui_pointer_refresh_overlay(); /* Erase the old cursor image from the framebuffer. */
        return 0;
    }

    /* Clamp incoming coordinates so the cursor anchor never points outside the screen. */
    if (graphics_is_ready()) {
        unsigned int width = graphics_width();
        unsigned int height = graphics_height();

        if (width != 0U && x >= width) {
            x = width - 1U;
        }
        if (height != 0U && y >= height) {
            y = height - 1U;
        }
    }

    g_gui_pointer_state.x = (unsigned int)x;
    g_gui_pointer_state.y = (unsigned int)y;
    g_gui_pointer_state.visible = 1U;
    g_gui_pointer_state.pressed = next_buttons;

    if (!previous_visible || previous_x != (unsigned int)x || previous_y != (unsigned int)y) {
        gui_queue_push(GUI_INPUT_EVENT_POINTER_MOVE, (unsigned int)x, (unsigned int)y, 0U, next_buttons);
    }
    if (previous_buttons == 0U && next_buttons != 0U) {
        gui_queue_push(GUI_INPUT_EVENT_POINTER_DOWN, (unsigned int)x, (unsigned int)y, 0U, next_buttons);
    }
    else if (previous_buttons != 0U && next_buttons == 0U) {
        gui_queue_push(GUI_INPUT_EVENT_POINTER_UP, (unsigned int)x, (unsigned int)y, 0U, next_buttons);
    }

    gui_pointer_refresh_overlay(); /* Move the software cursor to match the retained state. */
    return 0;
}

/* Convenience wrapper that preserves the current button state. */
long gui_pointer(unsigned long x, unsigned long y) {
    return gui_pointer_state(x, y, g_gui_pointer_state.pressed);
}

/* Report that the GUI path supports USB-backed input devices. */
long gui_usb_enabled(unsigned long unused0, unsigned long unused1) {
    (void)unused0;
    (void)unused1;
    return 1;
}

/* No extra GUI control operations are implemented in this transport yet. */
long gui_control(unsigned long command, unsigned long value) {
    (void)command;
    (void)value;
    return -1;
}

/* Fill the caller's display-info structure from the active framebuffer backend. */
long gui_get_display_info(GuiDisplayInfo* info) {
    if (!info || !graphics_is_ready()) {
        return -1;
    }

    info->version = GUI_DISPLAY_INFO_VERSION;
    info->width = graphics_width();
    info->height = graphics_height();
    info->pitch = graphics_pitch();
    info->pixel_format = graphics_is_rgb() ? GUI_PIXEL_FORMAT_XRGB8888 : GUI_PIXEL_FORMAT_XBGR8888;
    return 0;
}

/*
 * Copy one scanline of pixels into the framebuffer.
 *
 * If the caller is writing through the software cursor, we must temporarily
 * remove the cursor, apply the scanline update, then capture the new pixels and
 * redraw the cursor on top. That keeps the saved background coherent.
 */
long gui_present_scanline(unsigned int x, unsigned int y, unsigned int pixel_count, const unsigned int* pixels) {
    int refresh_pointer;
    unsigned int width;
    unsigned int* destination;

    if (!pixels || !graphics_is_ready()) {
        return -1;
    }

    width = graphics_width();
    if (y >= graphics_height() || x >= width) {
        return -1;
    }
    if (pixel_count > width - x) {
        pixel_count = width - x;
    }

    refresh_pointer = gui_pointer_overlay_intersects(x, y, pixel_count, 1U);
    if (refresh_pointer) {
        gui_pointer_restore_background(); /* Reveal the real framebuffer pixels before the write. */
    }

    destination = (unsigned int*)(graphics_framebuffer() + ((Address)y * graphics_pitch())) + x;
    for (unsigned int index = 0U; index < pixel_count; index++) {
        destination[index] = pixels[index];
    }

    if (refresh_pointer) {
        gui_pointer_capture_background(g_gui_pointer_state.x, g_gui_pointer_state.y); /* Re-snapshot the updated background. */
        gui_pointer_draw_overlay(); /* Put the cursor back on top of the new content. */
    }
    return 0;
}

/* Return the next queued input event, or a versioned NONE record if empty. */
long gui_get_input_event(GuiInputEvent* event) {
    if (!event) {
        return -1;
    }

    if (g_gui_input_count == 0U) {
        event->version = GUI_INPUT_EVENT_VERSION;
        event->type = GUI_INPUT_EVENT_NONE;
        event->x = 0U;
        event->y = 0U;
        event->key = 0U;
        event->buttons = 0U;
        event->reserved = 0U;
        return 0;
    }

    *event = g_gui_input_queue[g_gui_input_head];
    g_gui_input_head = (g_gui_input_head + 1U) % GUI_INPUT_QUEUE_MAX;
    g_gui_input_count--;
    return 0;
}

/* Return the retained pointer state without modifying it. */
long gui_get_pointer_state(GuiPointerState* state) {
    if (!state) {
        return -1;
    }

    *state = g_gui_pointer_state;
    return 0;
}

GuiSharedInputRegion* gui_shared_input_region(void) {
    return g_gui_shared_input_region_ptr;
}

long gui_shared_input_attach_consumer(unsigned long pid, GuiSharedInputView* view) {
    GuiSharedInputRegion* region;

    if (pid == 0UL || !view) {
        return -1;
    }

    region = gui_shared_input_ensure_region();
    if (!region) {
        return -1;
    }

    for (unsigned int index = 0U; index < GUI_SHARED_INPUT_MAX_CONSUMERS; index++) {
        GuiSharedInputConsumer* consumer = &region->consumers[index];

        if (consumer->pid == (unsigned int)pid) {
            gui_shared_input_fill_view(consumer, index, view);
            return 0;
        }
    }

    for (unsigned int index = 0U; index < GUI_SHARED_INPUT_MAX_CONSUMERS; index++) {
        GuiSharedInputConsumer* consumer = &region->consumers[index];

        if (consumer->pid != 0U) {
            continue;
        }

        consumer->pid = (unsigned int)pid;
        consumer->head_sequence = region->tail_sequence;
        consumer->drop_count = 0UL;
        consumer->last_seen_msec = (unsigned int)(get_system_timer() / 1000UL);
        gui_shared_input_fill_view(consumer, index, view);
        return 0;
    }

    return -1;
}

long gui_shared_input_release_consumer(unsigned long pid) {
    GuiSharedInputRegion* region = g_gui_shared_input_region_ptr;

    if (!region || pid == 0UL) {
        return -1;
    }

    for (unsigned int index = 0U; index < GUI_SHARED_INPUT_MAX_CONSUMERS; index++) {
        GuiSharedInputConsumer* consumer = &region->consumers[index];

        if (consumer->pid == (unsigned int)pid) {
            gui_shared_input_reset_consumer(consumer);
            return 0;
        }
    }

    return 0;
}

Address gui_shared_input_page_phys(unsigned int index) {
    if (index != 0U) {
        return 0;
    }

    return g_gui_shared_input_page;
}

unsigned int gui_shared_input_page_count(void) {
    return 1U;
}