#include "device/raspi3b/mailbox.h"
#include "log.h"
#include "memory.h"
#include "task.h"
#include "touch.h"
#include "utils.h"

#define TAG_GET_TOUCHBUF 0x0004000FU
#define TAG_SET_TOUCHBUF 0x0004801FU

#define TOUCH_NO_UPDATE 99U
#define TOUCH_MAX_POINTS 10U

#define FTS_TOUCH_DOWN 0U
#define FTS_TOUCH_UP 1U
#define FTS_TOUCH_CONTACT 2U

typedef struct ft5406_point
{
    UByte xh;
    UByte xl;
    UByte yh;
    UByte yl;
    UByte reserved[2];
} Ft5406Point;

typedef struct ft5406_buffer
{
    UByte device_mode;
    UByte gesture_id;
    UByte num_points;
    Ft5406Point points[TOUCH_MAX_POINTS];
} Ft5406Buffer;

static volatile Ft5406Buffer *touch_buffer;
static Address touch_buffer_page;
static unsigned long touch_last_poll_tick;
static TouchState touch_state;

static Address touch_phys_to_kernel(Address phys)
{
    return KERNEL_ADDR_SPACE(phys & ~0xC0000000ULL);
}

static UInt touch_phys_to_bus(Address phys)
{
    return (UInt)((phys & 0x3FFFFFFFULL) | 0xC0000000ULL);
}

static int touch_mailbox_simple(UInt tag, UInt request_length, UInt value, UInt *response_value)
{
    mailbox_buffer[0] = 7 * 4;
    mailbox_buffer[1] = MBOX_REQUEST;
    mailbox_buffer[2] = tag;
    mailbox_buffer[3] = 4;
    mailbox_buffer[4] = request_length;
    mailbox_buffer[5] = value;
    mailbox_buffer[6] = MBOX_TAG_LAST;

    if (!mbox_call(MBOX_CH_PROP))
    {
        return -1;
    }

    if (response_value != NULL)
    {
        *response_value = mailbox_buffer[5];
    }

    return 0;
}

int touch_init(void)
{
    UInt firmware_buffer = 0;

    if (touch_state.ready)
    {
        return 0;
    }

    touch_buffer_page = mem_alloc_page();
    if (touch_buffer_page == 0)
    {
        log_warning("Touchscreen init failed: no free page for touch buffer");
        return -1;
    }

    touch_buffer = (volatile Ft5406Buffer *)touch_phys_to_kernel(touch_buffer_page);
    memzero((Address)touch_buffer, PAGE_SIZE);
    touch_buffer->num_points = TOUCH_NO_UPDATE;

    if (touch_mailbox_simple(TAG_SET_TOUCHBUF, 4, touch_phys_to_bus(touch_buffer_page), &firmware_buffer) != 0)
    {
        firmware_buffer = 0;
    }

    if (firmware_buffer == 0)
    {
        if (touch_mailbox_simple(TAG_GET_TOUCHBUF, 0, 0, &firmware_buffer) != 0)
        {
            log_warning("Touchscreen init failed: firmware did not answer touch buffer request");
            return -1;
        }
    }

    if (firmware_buffer == 0)
    {
        log_warning("Touchscreen not detected");
        return -1;
    }

    if ((firmware_buffer & 0x3FFFFFFF) != (touch_buffer_page & 0x3FFFFFFF))
    {
        touch_buffer = (volatile Ft5406Buffer *)touch_phys_to_kernel((Address)firmware_buffer);
    }

    touch_buffer->num_points = TOUCH_NO_UPDATE;
    touch_last_poll_tick = (unsigned long)-1;
    touch_state.ready = true;
    touch_state.pressed = false;
    touch_state.x = 0;
    touch_state.y = 0;
    touch_state.raw_x = 0;
    touch_state.raw_y = 0;
    touch_state.contact_count = 0;

    log_info("Touchscreen ready: buffer=0x%X", firmware_buffer);
    return 0;
}

void touch_poll(void)
{
    Ft5406Buffer snapshot;
    volatile UByte *src;
    UByte *dst;
    unsigned long tick;
    UInt point_count;
    Bool pressed;
    UInt first_x;
    UInt first_y;

    if (!touch_state.ready || touch_buffer == NULL)
    {
        return;
    }

    tick = schedler_get_ticks();
    if (tick == touch_last_poll_tick)
    {
        return;
    }
    touch_last_poll_tick = tick;

    src = (volatile UByte *)touch_buffer;
    dst = (UByte *)&snapshot;
    for (Size index = 0; index < sizeof(snapshot); index++)
    {
        dst[index] = src[index];
    }

    touch_buffer->num_points = TOUCH_NO_UPDATE;

    if (snapshot.num_points == TOUCH_NO_UPDATE)
    {
        return;
    }

    point_count = snapshot.num_points;
    if (point_count > TOUCH_MAX_POINTS)
    {
        point_count = TOUCH_MAX_POINTS;
    }

    pressed = false;
    first_x = 0;
    first_y = 0;

    for (UInt index = 0; index < point_count; index++)
    {
        UInt event_id = (snapshot.points[index].xh >> 6) & 0x3U;
        UInt x = (((UInt)snapshot.points[index].xh & 0x0FU) << 8) | snapshot.points[index].xl;
        UInt y = (((UInt)snapshot.points[index].yh & 0x0FU) << 8) | snapshot.points[index].yl;

        if (event_id == FTS_TOUCH_DOWN || event_id == FTS_TOUCH_CONTACT)
        {
            first_x = x;
            first_y = y;
            pressed = true;
            break;
        }
        if (event_id == FTS_TOUCH_UP)
        {
            continue;
        }
    }

    touch_state.contact_count = point_count;
    touch_state.pressed = pressed;
    if (pressed)
    {
        touch_state.raw_x = first_x;
        touch_state.raw_y = first_y;
        touch_state.x = first_x;
        touch_state.y = first_y;
    }
}

int touch_is_ready(void)
{
    return touch_state.ready;
}

const TouchState *touch_get_state(void)
{
    return touch_state.ready ? &touch_state : NULL;
}