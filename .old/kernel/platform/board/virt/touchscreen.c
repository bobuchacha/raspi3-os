#include "device/virt.h"
#include "graphics.h"
#include "gui.h"
#include "input.h"
#include "log.h"
#include "memory.h"
#include "touch.h"

#define VIRTIO_MMIO_MAGIC_VALUE 0x000U
#define VIRTIO_MMIO_VERSION 0x004U
#define VIRTIO_MMIO_DEVICE_ID 0x008U
#define VIRTIO_MMIO_VENDOR_ID 0x00CU
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010U
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014U
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020U
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024U
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028U
#define VIRTIO_MMIO_QUEUE_SEL 0x030U
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034U
#define VIRTIO_MMIO_QUEUE_NUM 0x038U
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03CU
#define VIRTIO_MMIO_QUEUE_PFN 0x040U
#define VIRTIO_MMIO_QUEUE_READY 0x044U
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050U
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060U
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064U
#define VIRTIO_MMIO_STATUS 0x070U
#define VIRTIO_MMIO_QUEUE_DESC_LOW 0x080U
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084U
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090U
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094U
#define VIRTIO_MMIO_QUEUE_USED_LOW 0x0A0U
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0A4U

#define VIRTIO_MMIO_CONFIG 0x100U

#define VIRTIO_MAGIC 0x74726976U
#define VIRTIO_VERSION_LEGACY 1U
#define VIRTIO_VERSION_MODERN 2U
#define VIRTIO_DEVICE_ID_INPUT 18U
#define VIRTIO_VENDOR_QEMU 0x554D4551U

#define VIRTIO_STATUS_ACKNOWLEDGE 0x01U
#define VIRTIO_STATUS_DRIVER 0x02U
#define VIRTIO_STATUS_DRIVER_OK 0x04U
#define VIRTIO_STATUS_FEATURES_OK 0x08U
#define VIRTIO_STATUS_FAILED 0x80U

#define VIRTIO_F_VERSION_1 32U

#define VIRTQ_DESC_F_WRITE 2U

#define VIRTIO_INPUT_MMIO_SLOTS 32U
#define VIRTIO_INPUT_MAX_DEVICES 2U
#define VIRTIO_INPUT_QUEUE_SIZE 16U
#define VIRTIO_INPUT_LEGACY_QUEUE_ALIGN 4096U
#define VIRTIO_INPUT_LEGACY_QUEUE_BYTES (VIRTIO_INPUT_LEGACY_QUEUE_ALIGN * 2U)

#define VIRTIO_INPUT_CFG_ID_NAME 0x01U
#define VIRTIO_INPUT_CFG_EV_BITS 0x11U
#define VIRTIO_INPUT_CFG_ABS_INFO 0x12U

#define VIRTIO_INPUT_EVENT_QUEUE_INDEX 0U
#define VIRTIO_INPUT_STATUS_QUEUE_INDEX 1U

#define EV_SYN 0x00U
#define EV_KEY 0x01U
#define EV_REL 0x02U
#define EV_ABS 0x03U

#define SYN_REPORT 0U

#define REL_X 0U
#define REL_Y 1U

#define ABS_X 0U
#define ABS_Y 1U

#define BTN_LEFT 272U
#define BTN_TOUCH 330U

#define KEY_ESC 1U
#define KEY_1 2U
#define KEY_2 3U
#define KEY_3 4U
#define KEY_4 5U
#define KEY_5 6U
#define KEY_6 7U
#define KEY_7 8U
#define KEY_8 9U
#define KEY_9 10U
#define KEY_0 11U
#define KEY_MINUS 12U
#define KEY_EQUAL 13U
#define KEY_BACKSPACE 14U
#define KEY_TAB 15U
#define KEY_Q 16U
#define KEY_W 17U
#define KEY_E 18U
#define KEY_R 19U
#define KEY_T 20U
#define KEY_Y 21U
#define KEY_U 22U
#define KEY_I 23U
#define KEY_O 24U
#define KEY_P 25U
#define KEY_LEFTBRACE 26U
#define KEY_RIGHTBRACE 27U
#define KEY_ENTER 28U
#define KEY_A 30U
#define KEY_S 31U
#define KEY_D 32U
#define KEY_F 33U
#define KEY_G 34U
#define KEY_H 35U
#define KEY_J 36U
#define KEY_K 37U
#define KEY_L 38U
#define KEY_SEMICOLON 39U
#define KEY_APOSTROPHE 40U
#define KEY_GRAVE 41U
#define KEY_LEFTSHIFT 42U
#define KEY_BACKSLASH 43U
#define KEY_Z 44U
#define KEY_X 45U
#define KEY_C 46U
#define KEY_V 47U
#define KEY_B 48U
#define KEY_N 49U
#define KEY_M 50U
#define KEY_COMMA 51U
#define KEY_DOT 52U
#define KEY_SLASH 53U
#define KEY_RIGHTSHIFT 54U
#define KEY_SPACE 57U

#define KEY_KP0 82U
#define KEY_KP1 79U
#define KEY_KP2 80U
#define KEY_KP3 81U
#define KEY_KP4 75U
#define KEY_KP5 76U
#define KEY_KP6 77U
#define KEY_KP7 71U
#define KEY_KP8 72U
#define KEY_KP9 73U
#define KEY_KPDOT 83U
#define KEY_KPENTER 96U

typedef struct __attribute__((packed)) VirtqDesc {
    ULong addr;
    UInt len;
    unsigned short flags;
    unsigned short next;
} VirtqDesc;

typedef struct __attribute__((packed)) VirtqAvail {
    unsigned short flags;
    unsigned short idx;
    unsigned short ring[VIRTIO_INPUT_QUEUE_SIZE];
    unsigned short used_event;
} VirtqAvail;

typedef struct __attribute__((packed)) VirtqUsedElem {
    UInt id;
    UInt len;
} VirtqUsedElem;

typedef struct __attribute__((packed)) VirtqUsed {
    unsigned short flags;
    unsigned short idx;
    VirtqUsedElem ring[VIRTIO_INPUT_QUEUE_SIZE];
    unsigned short avail_event;
} VirtqUsed;

typedef struct __attribute__((packed)) VirtioInputEvent {
    unsigned short type;
    unsigned short code;
    UInt value;
} VirtioInputEvent;

typedef struct __attribute__((packed)) VirtioInputAbsInfo {
    Int min;
    Int max;
    Int fuzz;
    Int flat;
    Int res;
} VirtioInputAbsInfo;

typedef struct __attribute__((packed)) VirtioInputConfig {
    UByte select;
    UByte subsel;
    UByte size;
    UByte reserved[5];
    union {
        char string[128];
        UByte bitmap[128];
        VirtioInputAbsInfo abs;
    } u;
} VirtioInputConfig;

typedef struct VirtioInputQueue {
    unsigned short last_used_idx;
    VirtqDesc desc[VIRTIO_INPUT_QUEUE_SIZE] __attribute__((aligned(4096)));
    VirtqAvail avail __attribute__((aligned(4096)));
    VirtqUsed used __attribute__((aligned(4096)));
    UByte legacy_queue[VIRTIO_INPUT_LEGACY_QUEUE_BYTES] __attribute__((aligned(VIRTIO_INPUT_LEGACY_QUEUE_ALIGN)));
    VirtqDesc* desc_ring;
    VirtqAvail* avail_ring;
    VirtqUsed* used_ring;
} VirtioInputQueue;

typedef struct VirtioInputDevice {
    Bool ready;
    Bool keyboard;
    Bool pointer;
    Bool absolute_pointer;
    Bool relative_pointer;
    Bool shift_down;
    Bool button_down;
    Bool last_touch_pressed;
    Bool have_abs_x;
    Bool have_abs_y;
    Bool pointer_dirty;
    UInt slot_index;
    UInt version;
    Int abs_min_x;
    Int abs_max_x;
    Int abs_min_y;
    Int abs_max_y;
    Int raw_pointer_x;
    Int raw_pointer_y;
    Int pending_rel_x;
    Int pending_rel_y;
    UInt pointer_x;
    UInt pointer_y;
    VirtioInputQueue eventq;
    VirtioInputQueue statusq;
    VirtioInputEvent events[VIRTIO_INPUT_QUEUE_SIZE];
    char name[33];
} VirtioInputDevice;

static VirtioInputDevice virtio_input_devices[VIRTIO_INPUT_MAX_DEVICES];
static UInt virtio_input_device_count;
static Bool virtio_input_initialized;
static Bool virtio_input_touch_ready;
static TouchState virtio_touch_state;
static ULong virtio_input_debug_poll_count[VIRTIO_INPUT_MAX_DEVICES];
static volatile unsigned int virtio_input_poll_lock;

static inline volatile UInt* virtio_input_reg(const VirtioInputDevice* device, UInt offset) {
    return (volatile UInt*)(VA_START + VIRT_VIRTIO_MMIO_BASE + (device->slot_index * VIRT_VIRTIO_MMIO_STRIDE) + offset);
}

static inline UInt virtio_input_read_reg(const VirtioInputDevice* device, UInt offset) {
    return *virtio_input_reg(device, offset);
}

static inline void virtio_input_write_reg(const VirtioInputDevice* device, UInt offset, UInt value) {
    *virtio_input_reg(device, offset) = value;
}

static inline unsigned int virtio_input_poll_try_acquire(volatile unsigned int* lock) {
    unsigned int previous;
    unsigned int status;

    asm volatile(
        "ldaxr %w0, [%2]\n"
        "cbnz %w0, 1f\n"
        "mov %w0, #1\n"
        "stxr %w1, %w0, [%2]\n"
        "cbnz %w1, 2f\n"
        "mov %w0, wzr\n"
        "b 3f\n"
        "1:\n"
        "mov %w1, wzr\n"
        "2:\n"
        "3:\n"
        : "=&r"(previous), "=&r"(status)
        : "r"(lock)
        : "memory");

    return previous == 0U && status == 0U;
}

static inline void virtio_input_poll_release(volatile unsigned int* lock) {
    asm volatile(
        "stlr %w1, [%0]\n"
        :
        : "r"(lock), "r"(0U)
        : "memory");
}

static void virtio_input_debug_log_poll(const VirtioInputDevice* device, const char* phase, UInt interrupt_status, unsigned short used_idx) {
    if (!device || !phase) {
        return;
    }

    log_info("virtio-input: dbg slot=%u phase=%s irq=0x%X used=%u last=%u version=%u ready=%u ptr=%u kbd=%u abs=%u rel=%u",
        device->slot_index,
        phase,
        interrupt_status,
        (UInt)used_idx,
        (UInt)device->eventq.last_used_idx,
        device->version,
        device->ready ? 1U : 0U,
        device->pointer ? 1U : 0U,
        device->keyboard ? 1U : 0U,
        device->absolute_pointer ? 1U : 0U,
        device->relative_pointer ? 1U : 0U);
}

static inline volatile VirtioInputConfig* virtio_input_config(const VirtioInputDevice* device) {
    return (volatile VirtioInputConfig*)(VA_START + VIRT_VIRTIO_MMIO_BASE + (device->slot_index * VIRT_VIRTIO_MMIO_STRIDE) + VIRTIO_MMIO_CONFIG);
}

static void virtio_input_barrier(void) {
    asm volatile("dmb ish" ::: "memory");
}

static void virtio_input_write_addr(const VirtioInputDevice* device, UInt low_offset, UInt high_offset, ULong address) {
    virtio_input_write_reg(device, low_offset, (UInt)(address & 0xFFFFFFFFU));
    virtio_input_write_reg(device, high_offset, (UInt)(address >> 32));
}

static Bool virtio_input_uses_legacy_layout(const VirtioInputDevice* device) {
    return device->version == VIRTIO_VERSION_LEGACY;
}

static VirtqDesc* virtio_input_active_desc_ring(VirtioInputDevice* device, VirtioInputQueue* queue) {
    if (virtio_input_uses_legacy_layout(device)) {
        return (VirtqDesc*)queue->legacy_queue;
    }

    return queue->desc_ring ? queue->desc_ring : queue->desc;
}

static VirtqAvail* virtio_input_active_avail_ring(VirtioInputDevice* device, VirtioInputQueue* queue) {
    if (virtio_input_uses_legacy_layout(device)) {
        return (VirtqAvail*)(queue->legacy_queue + (sizeof(VirtqDesc) * VIRTIO_INPUT_QUEUE_SIZE));
    }

    return queue->avail_ring ? queue->avail_ring : &queue->avail;
}

static VirtqUsed* virtio_input_active_used_ring(VirtioInputDevice* device, VirtioInputQueue* queue) {
    if (virtio_input_uses_legacy_layout(device)) {
        ULong avail_base = (ULong)(queue->legacy_queue + (sizeof(VirtqDesc) * VIRTIO_INPUT_QUEUE_SIZE));
        ULong used_base = (avail_base + sizeof(VirtqAvail) + (VIRTIO_INPUT_LEGACY_QUEUE_ALIGN - 1U)) & ~(ULong)(VIRTIO_INPUT_LEGACY_QUEUE_ALIGN - 1U);

        return (VirtqUsed*)used_base;
    }

    return queue->used_ring ? queue->used_ring : &queue->used;
}

static void virtio_input_config_select(const VirtioInputDevice* device, UByte select, UByte subsel) {
    volatile VirtioInputConfig* config = virtio_input_config(device);

    config->select = select;
    config->subsel = subsel;
    virtio_input_barrier();
}

static UByte virtio_input_query_bitmap(const VirtioInputDevice* device, UByte select, UByte subsel, UByte* out_bitmap) {
    volatile VirtioInputConfig* config = virtio_input_config(device);
    UByte size;

    virtio_input_config_select(device, select, subsel);
    size = config->size;
    if (out_bitmap) {
        memset(out_bitmap, 0, 128);
        for (UInt index = 0; index < size && index < 128U; ++index) {
            out_bitmap[index] = config->u.bitmap[index];
        }
    }

    return size;
}

static Bool virtio_input_query_absinfo(const VirtioInputDevice* device, UByte axis, VirtioInputAbsInfo* out_abs) {
    volatile VirtioInputConfig* config = virtio_input_config(device);

    if (!out_abs) {
        return false;
    }

    virtio_input_config_select(device, VIRTIO_INPUT_CFG_ABS_INFO, axis);
    if (config->size < sizeof(VirtioInputAbsInfo)) {
        memset(out_abs, 0, sizeof(*out_abs));
        return false;
    }

    *out_abs = config->u.abs;
    return true;
}

static void virtio_input_query_name(const VirtioInputDevice* device, char* out_name, UInt capacity) {
    volatile VirtioInputConfig* config = virtio_input_config(device);
    UByte size;

    if (!out_name || capacity == 0U) {
        return;
    }

    virtio_input_config_select(device, VIRTIO_INPUT_CFG_ID_NAME, 0);
    size = config->size;
    if (size >= capacity) {
        size = (UByte)(capacity - 1U);
    }

    for (UInt index = 0; index < size; ++index) {
        out_name[index] = config->u.string[index];
    }
    out_name[size] = '\0';
}

static Bool virtio_input_bitmap_has(const UByte* bitmap, UByte size, UInt bit_index) {
    UInt byte_index = bit_index / 8U;
    UByte bit_mask = (UByte)(1U << (bit_index % 8U));

    return bitmap && byte_index < size && (bitmap[byte_index] & bit_mask) != 0U;
}

static UInt virtio_input_clamp_axis(Int value, UInt limit) {
    if (limit == 0U) {
        return 0U;
    }
    if (value < 0) {
        return 0U;
    }
    if ((UInt)value >= limit) {
        return limit - 1U;
    }

    return (UInt)value;
}

static UInt virtio_input_scale_axis(Int value, Int min_value, Int max_value, UInt limit) {
    Long span;
    Long offset;

    if (limit <= 1U || max_value <= min_value) {
        return 0U;
    }

    if (value < min_value) {
        value = min_value;
    }
    if (value > max_value) {
        value = max_value;
    }

    span = (Long)max_value - (Long)min_value;
    offset = (Long)value - (Long)min_value;
    return (UInt)((offset * (Long)(limit - 1U)) / span);
}

static char virtio_input_map_key(UInt code, Bool shift_down) {
    switch (code) {
    case KEY_Q:
        return shift_down ? 'Q' : 'q';
    case KEY_W:
        return shift_down ? 'W' : 'w';
    case KEY_E:
        return shift_down ? 'E' : 'e';
    case KEY_R:
        return shift_down ? 'R' : 'r';
    case KEY_T:
        return shift_down ? 'T' : 't';
    case KEY_Y:
        return shift_down ? 'Y' : 'y';
    case KEY_U:
        return shift_down ? 'U' : 'u';
    case KEY_I:
        return shift_down ? 'I' : 'i';
    case KEY_O:
        return shift_down ? 'O' : 'o';
    case KEY_P:
        return shift_down ? 'P' : 'p';
    case KEY_A:
        return shift_down ? 'A' : 'a';
    case KEY_S:
        return shift_down ? 'S' : 's';
    case KEY_D:
        return shift_down ? 'D' : 'd';
    case KEY_F:
        return shift_down ? 'F' : 'f';
    case KEY_G:
        return shift_down ? 'G' : 'g';
    case KEY_H:
        return shift_down ? 'H' : 'h';
    case KEY_J:
        return shift_down ? 'J' : 'j';
    case KEY_K:
        return shift_down ? 'K' : 'k';
    case KEY_L:
        return shift_down ? 'L' : 'l';
    case KEY_Z:
        return shift_down ? 'Z' : 'z';
    case KEY_X:
        return shift_down ? 'X' : 'x';
    case KEY_C:
        return shift_down ? 'C' : 'c';
    case KEY_V:
        return shift_down ? 'V' : 'v';
    case KEY_B:
        return shift_down ? 'B' : 'b';
    case KEY_N:
        return shift_down ? 'N' : 'n';
    case KEY_M:
        return shift_down ? 'M' : 'm';
    case KEY_ESC:
        return 27;
    case KEY_1:
        return shift_down ? '!' : '1';
    case KEY_2:
        return shift_down ? '@' : '2';
    case KEY_3:
        return shift_down ? '#' : '3';
    case KEY_4:
        return shift_down ? '$' : '4';
    case KEY_5:
        return shift_down ? '%' : '5';
    case KEY_6:
        return shift_down ? '^' : '6';
    case KEY_7:
        return shift_down ? '&' : '7';
    case KEY_8:
        return shift_down ? '*' : '8';
    case KEY_9:
        return shift_down ? '(' : '9';
    case KEY_0:
        return shift_down ? ')' : '0';
    case KEY_MINUS:
        return shift_down ? '_' : '-';
    case KEY_EQUAL:
        return shift_down ? '+' : '=';
    case KEY_BACKSPACE:
        return 8;
    case KEY_TAB:
        return '\t';
    case KEY_LEFTBRACE:
        return shift_down ? '{' : '[';
    case KEY_RIGHTBRACE:
        return shift_down ? '}' : ']';
    case KEY_ENTER:
    case KEY_KPENTER:
        return '\n';
    case KEY_SEMICOLON:
        return shift_down ? ':' : ';';
    case KEY_APOSTROPHE:
        return shift_down ? '"' : '\'';
    case KEY_GRAVE:
        return shift_down ? '~' : '`';
    case KEY_BACKSLASH:
        return shift_down ? '|' : '\\';
    case KEY_COMMA:
        return shift_down ? '<' : ',';
    case KEY_DOT:
    case KEY_KPDOT:
        return shift_down ? '>' : '.';
    case KEY_SLASH:
        return shift_down ? '?' : '/';
    case KEY_SPACE:
        return ' ';
    case KEY_KP0:
        return '0';
    case KEY_KP1:
        return '1';
    case KEY_KP2:
        return '2';
    case KEY_KP3:
        return '3';
    case KEY_KP4:
        return '4';
    case KEY_KP5:
        return '5';
    case KEY_KP6:
        return '6';
    case KEY_KP7:
        return '7';
    case KEY_KP8:
        return '8';
    case KEY_KP9:
        return '9';
    default:
        return 0;
    }
}

static void virtio_input_forward_key(char ch) {
    if (ch == 0) {
        return;
    }

    (void)input_enqueue_key((unsigned long)(unsigned char)ch);
    gui_key((unsigned long)(unsigned char)ch, 0);
}

static void virtio_input_update_touch_state(UInt x, UInt y, Bool pressed) {
    virtio_touch_state.ready = true;
    virtio_touch_state.x = x;
    virtio_touch_state.y = y;
    virtio_touch_state.raw_x = x;
    virtio_touch_state.raw_y = y;
    virtio_touch_state.pressed = pressed;
    virtio_touch_state.contact_count = pressed ? 1U : 0U;
    virtio_input_touch_ready = true;
}

static void virtio_input_commit_pointer(VirtioInputDevice* device) {
    UInt width;
    UInt height;
    UInt x;
    UInt y;
    Bool have_position = false;
    Bool button_changed;

    if (!device->pointer || !graphics_is_ready()) {
        return;
    }

    button_changed = device->button_down != device->last_touch_pressed;

    width = graphics_width();
    height = graphics_height();
    if (width == 0U || height == 0U) {
        return;
    }

    if (device->absolute_pointer) {
        x = virtio_input_scale_axis(device->raw_pointer_x, device->abs_min_x, device->abs_max_x, width);
        y = virtio_input_scale_axis(device->raw_pointer_y, device->abs_min_y, device->abs_max_y, height);
        have_position = (device->have_abs_x && device->have_abs_y) || button_changed;
    }
    else {
        Int next_x = (Int)device->pointer_x + device->pending_rel_x;
        Int next_y = (Int)device->pointer_y + device->pending_rel_y;

        x = virtio_input_clamp_axis(next_x, width);
        y = virtio_input_clamp_axis(next_y, height);
        have_position = device->pointer_dirty || device->pending_rel_x != 0 || device->pending_rel_y != 0 || button_changed;
    }

    device->pending_rel_x = 0;
    device->pending_rel_y = 0;
    device->pointer_dirty = false;
    if (!have_position) {
        return;
    }

    device->pointer_x = x;
    device->pointer_y = y;
    virtio_input_update_touch_state(x, y, device->button_down);
    (void)gui_pointer_state((unsigned long)x, (unsigned long)y, device->button_down ? 1UL : 0UL);
    device->last_touch_pressed = device->button_down;
}

static void virtio_input_process_event(VirtioInputDevice* device, const VirtioInputEvent* event) {
    UInt type;
    UInt code;
    Int value;
    char mapped_key = 0;

    if (!device || !event) {
        return;
    }

    type = event->type;
    code = event->code;
    value = (Int)event->value;

    log_info("virtio-input: event slot=%u type=%u code=%u value=%d",
        device->slot_index,
        type,
        code,
        value);

    if (device->keyboard && type == EV_KEY) {
        if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
            device->shift_down = value != 0;
            return;
        }

        if (value == 1 || value == 2) {
            mapped_key = virtio_input_map_key(code, device->shift_down);
            virtio_input_forward_key(mapped_key);
            return;
        }
        return;
    }

    if (!device->pointer) {
        return;
    }

    switch (type) {
    case EV_KEY:
        if (code == BTN_LEFT || code == BTN_TOUCH) {
            device->button_down = value != 0;
        }
        break;
    case EV_REL:
        if (code == REL_X) {
            device->pending_rel_x += value;
            device->pointer_dirty = true;
        }
        else if (code == REL_Y) {
            device->pending_rel_y += value;
            device->pointer_dirty = true;
        }
        break;
    case EV_ABS:
        if (code == ABS_X) {
            device->raw_pointer_x = value;
            device->have_abs_x = true;
            device->pointer_dirty = true;
        }
        else if (code == ABS_Y) {
            device->raw_pointer_y = value;
            device->have_abs_y = true;
            device->pointer_dirty = true;
        }
        break;
    case EV_SYN:
        if (code == SYN_REPORT) {
            virtio_input_commit_pointer(device);
        }
        break;
    default:
        break;
    }
}

static void virtio_input_refill_queue(VirtioInputDevice* device, VirtioInputQueue* queue, UInt descriptor_id) {
    VirtqAvail* avail_ring = virtio_input_active_avail_ring(device, queue);

    if (!avail_ring || descriptor_id >= VIRTIO_INPUT_QUEUE_SIZE) {
        return;
    }

    avail_ring->ring[avail_ring->idx % VIRTIO_INPUT_QUEUE_SIZE] = (unsigned short)descriptor_id;
    virtio_input_barrier();
    avail_ring->idx++;
}

static void virtio_input_notify_queue(VirtioInputDevice* device, UInt queue_index) {
    if (!device) {
        return;
    }

    virtio_input_write_reg(device, VIRTIO_MMIO_QUEUE_SEL, queue_index);
    virtio_input_barrier();
    virtio_input_write_reg(device, VIRTIO_MMIO_QUEUE_NOTIFY, queue_index);
}

static int virtio_input_setup_named_queue(VirtioInputDevice* device, VirtioInputQueue* queue, UInt queue_index, UInt status, Bool populate_events) {
    VirtqDesc* desc_ring;
    VirtqAvail* avail_ring;
    VirtqUsed* used_ring;
    UInt queue_limit;
    UInt descriptor_index;

    virtio_input_write_reg(device, VIRTIO_MMIO_QUEUE_SEL, queue_index);
    queue_limit = virtio_input_read_reg(device, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (queue_limit < VIRTIO_INPUT_QUEUE_SIZE) {
        log_error("virtio-input: queue %u too small on slot %u", queue_index, device->slot_index);
        virtio_input_write_reg(device, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
        return -1;
    }

    virtio_input_write_reg(device, VIRTIO_MMIO_QUEUE_NUM, VIRTIO_INPUT_QUEUE_SIZE);
    if (device->version == VIRTIO_VERSION_MODERN) {
        virtio_input_write_addr(device, VIRTIO_MMIO_QUEUE_DESC_LOW, VIRTIO_MMIO_QUEUE_DESC_HIGH,
            mem_virt_to_phys((VirtAddr)&queue->desc));
        virtio_input_write_addr(device, VIRTIO_MMIO_QUEUE_AVAIL_LOW, VIRTIO_MMIO_QUEUE_AVAIL_HIGH,
            mem_virt_to_phys((VirtAddr)&queue->avail));
        virtio_input_write_addr(device, VIRTIO_MMIO_QUEUE_USED_LOW, VIRTIO_MMIO_QUEUE_USED_HIGH,
            mem_virt_to_phys((VirtAddr)&queue->used));
        virtio_input_write_reg(device, VIRTIO_MMIO_QUEUE_READY, 1);
        queue->desc_ring = queue->desc;
        queue->avail_ring = &queue->avail;
        queue->used_ring = &queue->used;
    }
    else {
        ULong queue_base = (ULong)queue->legacy_queue;
        ULong desc_base = queue_base;
        ULong avail_base = desc_base + (sizeof(VirtqDesc) * VIRTIO_INPUT_QUEUE_SIZE);
        ULong used_base = (avail_base + sizeof(VirtqAvail) + (VIRTIO_INPUT_LEGACY_QUEUE_ALIGN - 1U)) & ~(ULong)(VIRTIO_INPUT_LEGACY_QUEUE_ALIGN - 1U);

        queue->desc_ring = (VirtqDesc*)desc_base;
        queue->avail_ring = (VirtqAvail*)avail_base;
        queue->used_ring = (VirtqUsed*)used_base;
        memset((void*)queue->desc_ring, 0, sizeof(VirtqDesc) * VIRTIO_INPUT_QUEUE_SIZE);
        memset((void*)queue->avail_ring, 0, sizeof(VirtqAvail));
        memset((void*)queue->used_ring, 0, sizeof(VirtqUsed));
        virtio_input_write_reg(device, VIRTIO_MMIO_GUEST_PAGE_SIZE, VIRTIO_INPUT_LEGACY_QUEUE_ALIGN);
        virtio_input_write_reg(device, VIRTIO_MMIO_QUEUE_ALIGN, VIRTIO_INPUT_LEGACY_QUEUE_ALIGN);
        virtio_input_write_reg(device, VIRTIO_MMIO_QUEUE_PFN, (UInt)(mem_virt_to_phys((VirtAddr)queue_base) / VIRTIO_INPUT_LEGACY_QUEUE_ALIGN));
    }

    desc_ring = virtio_input_active_desc_ring(device, queue);
    avail_ring = virtio_input_active_avail_ring(device, queue);
    used_ring = virtio_input_active_used_ring(device, queue);
    if (!desc_ring || !avail_ring || !used_ring) {
        return -1;
    }

    queue->desc_ring = desc_ring;
    queue->avail_ring = avail_ring;
    queue->used_ring = used_ring;
    if (populate_events) {
        for (descriptor_index = 0; descriptor_index < VIRTIO_INPUT_QUEUE_SIZE; ++descriptor_index) {
            desc_ring[descriptor_index].addr = mem_virt_to_phys((VirtAddr)&device->events[descriptor_index]);
            desc_ring[descriptor_index].len = sizeof(VirtioInputEvent);
            desc_ring[descriptor_index].flags = VIRTQ_DESC_F_WRITE;
            desc_ring[descriptor_index].next = 0;
            avail_ring->ring[descriptor_index] = (unsigned short)descriptor_index;
        }
        virtio_input_barrier();
        avail_ring->idx = VIRTIO_INPUT_QUEUE_SIZE;
        virtio_input_barrier();
        virtio_input_notify_queue(device, queue_index);
    }
    else {
        memset((void*)desc_ring, 0, sizeof(VirtqDesc) * VIRTIO_INPUT_QUEUE_SIZE);
        avail_ring->idx = 0;
        used_ring->idx = 0;
    }
    return 0;
}

static void virtio_input_classify_device(VirtioInputDevice* device) {
    UByte key_bits[128];
    UByte rel_bits[128];
    UByte abs_bits[128];
    UByte key_size;
    UByte rel_size;
    UByte abs_size;
    VirtioInputAbsInfo abs_info;

    key_size = virtio_input_query_bitmap(device, VIRTIO_INPUT_CFG_EV_BITS, EV_KEY, key_bits);
    rel_size = virtio_input_query_bitmap(device, VIRTIO_INPUT_CFG_EV_BITS, EV_REL, rel_bits);
    abs_size = virtio_input_query_bitmap(device, VIRTIO_INPUT_CFG_EV_BITS, EV_ABS, abs_bits);

    device->keyboard = key_size != 0U && virtio_input_bitmap_has(key_bits, key_size, KEY_A) && virtio_input_bitmap_has(key_bits, key_size, KEY_ENTER);
    device->relative_pointer = rel_size != 0U && virtio_input_bitmap_has(rel_bits, rel_size, REL_X) && virtio_input_bitmap_has(rel_bits, rel_size, REL_Y);
    device->absolute_pointer = abs_size != 0U && virtio_input_bitmap_has(abs_bits, abs_size, ABS_X) && virtio_input_bitmap_has(abs_bits, abs_size, ABS_Y);
    device->pointer = device->relative_pointer || device->absolute_pointer;

    if (device->absolute_pointer) {
        if (virtio_input_query_absinfo(device, ABS_X, &abs_info)) {
            device->abs_min_x = abs_info.min;
            device->abs_max_x = abs_info.max;
        }
        if (virtio_input_query_absinfo(device, ABS_Y, &abs_info)) {
            device->abs_min_y = abs_info.min;
            device->abs_max_y = abs_info.max;
        }
    }

    if (graphics_is_ready()) {
        device->pointer_x = graphics_width() / 2U;
        device->pointer_y = graphics_height() / 2U;
    }
    device->raw_pointer_x = device->abs_min_x;
    device->raw_pointer_y = device->abs_min_y;
    virtio_input_query_name(device, device->name, sizeof(device->name));
}

static int virtio_input_init_device(VirtioInputDevice* device) {
    UInt status = 0;
    UInt features_hi;

    if (virtio_input_read_reg(device, VIRTIO_MMIO_VENDOR_ID) != VIRTIO_VENDOR_QEMU) {
        log_warning("virtio-input: unexpected vendor 0x%X on slot %u", virtio_input_read_reg(device, VIRTIO_MMIO_VENDOR_ID), device->slot_index);
    }

    virtio_input_write_reg(device, VIRTIO_MMIO_STATUS, 0);
    status |= VIRTIO_STATUS_ACKNOWLEDGE;
    virtio_input_write_reg(device, VIRTIO_MMIO_STATUS, status);
    status |= VIRTIO_STATUS_DRIVER;
    virtio_input_write_reg(device, VIRTIO_MMIO_STATUS, status);

    virtio_input_classify_device(device);
    if (!device->keyboard && !device->pointer) {
        return -1;
    }

    if (device->version == VIRTIO_VERSION_MODERN) {
        virtio_input_write_reg(device, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
        features_hi = virtio_input_read_reg(device, VIRTIO_MMIO_DEVICE_FEATURES);
        if ((features_hi & (1U << (VIRTIO_F_VERSION_1 - 32U))) == 0U) {
            log_error("virtio-input: device on slot %u does not offer VERSION_1", device->slot_index);
            virtio_input_write_reg(device, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
            return -1;
        }

        virtio_input_write_reg(device, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
        virtio_input_write_reg(device, VIRTIO_MMIO_DRIVER_FEATURES, 0);
        virtio_input_write_reg(device, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
        virtio_input_write_reg(device, VIRTIO_MMIO_DRIVER_FEATURES, (1U << (VIRTIO_F_VERSION_1 - 32U)));

        status |= VIRTIO_STATUS_FEATURES_OK;
        virtio_input_write_reg(device, VIRTIO_MMIO_STATUS, status);
        if ((virtio_input_read_reg(device, VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0U) {
            log_error("virtio-input: feature negotiation rejected on slot %u", device->slot_index);
            virtio_input_write_reg(device, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
            return -1;
        }
    }

    if (virtio_input_setup_named_queue(device, &device->eventq, VIRTIO_INPUT_EVENT_QUEUE_INDEX, status, true) != 0) {
        virtio_input_write_reg(device, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
        return -1;
    }

    if (virtio_input_setup_named_queue(device, &device->statusq, VIRTIO_INPUT_STATUS_QUEUE_INDEX, status, false) != 0) {
        virtio_input_write_reg(device, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
        return -1;
    }

    status |= VIRTIO_STATUS_DRIVER_OK;
    virtio_input_write_reg(device, VIRTIO_MMIO_STATUS, status);
    virtio_input_barrier();
    virtio_input_notify_queue(device, VIRTIO_INPUT_EVENT_QUEUE_INDEX);
    device->ready = true;
    log_info("virtio-input: %s%s device ready on slot %u (%s)",
        device->keyboard ? "keyboard" : "",
        device->pointer ? (device->keyboard ? "/pointer" : "pointer") : "",
        device->slot_index,
        device->name[0] != '\0' ? device->name : "unnamed");
    return 0;
}

int touch_init(void) {
    UInt slot_index;

    if (virtio_input_initialized) {
        return virtio_input_touch_ready ? 0 : -1;
    }

    memset(&virtio_input_devices, 0, sizeof(virtio_input_devices));
    memset(&virtio_touch_state, 0, sizeof(virtio_touch_state));
    virtio_input_device_count = 0;
    virtio_input_touch_ready = false;
    virtio_input_initialized = true;

    for (slot_index = 0; slot_index < VIRTIO_INPUT_MMIO_SLOTS && virtio_input_device_count < VIRTIO_INPUT_MAX_DEVICES; ++slot_index) {
        UInt version;
        VirtioInputDevice* device;
        volatile UInt* magic_reg = (volatile UInt*)(VA_START + VIRT_VIRTIO_MMIO_BASE + (slot_index * VIRT_VIRTIO_MMIO_STRIDE) + VIRTIO_MMIO_MAGIC_VALUE);
        volatile UInt* version_reg = (volatile UInt*)(VA_START + VIRT_VIRTIO_MMIO_BASE + (slot_index * VIRT_VIRTIO_MMIO_STRIDE) + VIRTIO_MMIO_VERSION);
        volatile UInt* device_id_reg = (volatile UInt*)(VA_START + VIRT_VIRTIO_MMIO_BASE + (slot_index * VIRT_VIRTIO_MMIO_STRIDE) + VIRTIO_MMIO_DEVICE_ID);

        version = *version_reg;
        if (*magic_reg != VIRTIO_MAGIC || (version != VIRTIO_VERSION_LEGACY && version != VIRTIO_VERSION_MODERN) || *device_id_reg != VIRTIO_DEVICE_ID_INPUT) {
            continue;
        }

        device = &virtio_input_devices[virtio_input_device_count];
        memset(device, 0, sizeof(*device));
        device->slot_index = slot_index;
        device->version = version;
        device->abs_max_x = 1;
        device->abs_max_y = 1;
        if (virtio_input_init_device(device) == 0) {
            if (device->pointer) {
                virtio_input_touch_ready = true;
                virtio_touch_state.ready = true;
            }
            virtio_input_device_count++;
        }
    }

    if (virtio_input_device_count == 0U) {
        log_warning("virtio-input: no input devices detected on virt board");
        return -1;
    }

    return virtio_input_touch_ready ? 0 : -1;
}

void touch_poll(void) {
    UInt device_index;

    if (!virtio_input_initialized) {
        return;
    }
    if (!virtio_input_poll_try_acquire(&virtio_input_poll_lock)) {
        return;
    }

    for (device_index = 0; device_index < virtio_input_device_count; ++device_index) {
        VirtioInputDevice* device = &virtio_input_devices[device_index];
        VirtqUsed* used_ring;
        UInt interrupt_status;
        Bool notified = false;

        if (!device->ready) {
            continue;
        }

        interrupt_status = virtio_input_read_reg(device, VIRTIO_MMIO_INTERRUPT_STATUS);
        if (interrupt_status != 0U) {
            virtio_input_write_reg(device, VIRTIO_MMIO_INTERRUPT_ACK, interrupt_status);
        }

        used_ring = virtio_input_active_used_ring(device, &device->eventq);
        if (!used_ring) {
            continue;
        }

        virtio_input_debug_poll_count[device_index]++;

        while (used_ring->idx != device->eventq.last_used_idx) {
            UInt used_slot = device->eventq.last_used_idx % VIRTIO_INPUT_QUEUE_SIZE;
            UInt descriptor_id = used_ring->ring[used_slot].id;

            virtio_input_debug_log_poll(device, "used", interrupt_status, used_ring->idx);

            if (descriptor_id < VIRTIO_INPUT_QUEUE_SIZE) {
                virtio_input_process_event(device, &device->events[descriptor_id]);
                virtio_input_refill_queue(device, &device->eventq, descriptor_id);
                notified = true;
            }
            device->eventq.last_used_idx++;
        }

        if (interrupt_status != 0U) {
            virtio_input_debug_log_poll(device, "irq", interrupt_status, used_ring->idx);
        }
        else if (virtio_input_debug_poll_count[device_index] <= 16U ||
            virtio_input_debug_poll_count[device_index] == 1024U ||
            (virtio_input_debug_poll_count[device_index] % 65536U) == 0U) {
            virtio_input_debug_log_poll(device, "idle", interrupt_status, used_ring->idx);
        }

        if (notified) {
            virtio_input_notify_queue(device, VIRTIO_INPUT_EVENT_QUEUE_INDEX);
        }
    }

    virtio_input_poll_release(&virtio_input_poll_lock);
}

int touch_is_ready(void) {
    return virtio_input_touch_ready ? 1 : 0;
}

const TouchState* touch_get_state(void) {
    return virtio_input_touch_ready ? &virtio_touch_state : NULL;
}