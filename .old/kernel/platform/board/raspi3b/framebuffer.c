#include "device/raspi3b/mailbox.h"
#include "graphics.h"
#include "log.h"
#include "memory.h"

#define FRAMEBUFFER_WIDTH 1280U
#define FRAMEBUFFER_HEIGHT 720U
#define FRAMEBUFFER_DEPTH 32U
#define FRAMEBUFFER_ALIGNMENT 4096U

#define TAG_SET_PHYSICAL_SIZE 0x00048003U
#define TAG_SET_VIRTUAL_SIZE 0x00048004U
#define TAG_SET_VIRTUAL_OFFSET 0x00048009U
#define TAG_SET_DEPTH 0x00048005U
#define TAG_SET_PIXEL_ORDER 0x00048006U
#define TAG_ALLOCATE_BUFFER 0x00040001U
#define TAG_GET_PITCH 0x00040008U

static unsigned int framebuffer_width_value;
static unsigned int framebuffer_height_value;
static unsigned int framebuffer_pitch_value;
static unsigned int framebuffer_is_rgb_value;
static Address framebuffer_address_value;
static int framebuffer_ready;

static unsigned int framebuffer_clip_width(unsigned int x, unsigned int width)
{
    if (x >= framebuffer_width_value)
    {
        return 0;
    }
    if (width > framebuffer_width_value - x)
    {
        return framebuffer_width_value - x;
    }
    return width;
}

static unsigned int framebuffer_clip_height(unsigned int y, unsigned int height)
{
    if (y >= framebuffer_height_value)
    {
        return 0;
    }
    if (height > framebuffer_height_value - y)
    {
        return framebuffer_height_value - y;
    }
    return height;
}

int graphics_init(void)
{
    mailbox_buffer[0] = 35 * 4;
    mailbox_buffer[1] = MBOX_REQUEST;

    mailbox_buffer[2] = TAG_SET_PHYSICAL_SIZE;
    mailbox_buffer[3] = 8;
    mailbox_buffer[4] = 8;
    mailbox_buffer[5] = FRAMEBUFFER_WIDTH;
    mailbox_buffer[6] = FRAMEBUFFER_HEIGHT;

    mailbox_buffer[7] = TAG_SET_VIRTUAL_SIZE;
    mailbox_buffer[8] = 8;
    mailbox_buffer[9] = 8;
    mailbox_buffer[10] = FRAMEBUFFER_WIDTH;
    mailbox_buffer[11] = FRAMEBUFFER_HEIGHT;

    mailbox_buffer[12] = TAG_SET_VIRTUAL_OFFSET;
    mailbox_buffer[13] = 8;
    mailbox_buffer[14] = 8;
    mailbox_buffer[15] = 0;
    mailbox_buffer[16] = 0;

    mailbox_buffer[17] = TAG_SET_DEPTH;
    mailbox_buffer[18] = 4;
    mailbox_buffer[19] = 4;
    mailbox_buffer[20] = FRAMEBUFFER_DEPTH;

    mailbox_buffer[21] = TAG_SET_PIXEL_ORDER;
    mailbox_buffer[22] = 4;
    mailbox_buffer[23] = 4;
    mailbox_buffer[24] = 1;

    mailbox_buffer[25] = TAG_ALLOCATE_BUFFER;
    mailbox_buffer[26] = 8;
    mailbox_buffer[27] = 8;
    mailbox_buffer[28] = FRAMEBUFFER_ALIGNMENT;
    mailbox_buffer[29] = 0;

    mailbox_buffer[30] = TAG_GET_PITCH;
    mailbox_buffer[31] = 4;
    mailbox_buffer[32] = 4;
    mailbox_buffer[33] = 0;

    mailbox_buffer[34] = MBOX_TAG_LAST;

    if (!mbox_call(MBOX_CH_PROP) || mailbox_buffer[20] != FRAMEBUFFER_DEPTH || mailbox_buffer[28] == 0 || mailbox_buffer[33] == 0)
    {
        framebuffer_ready = 0;
        framebuffer_address_value = 0;
        log_warning("Framebuffer init failed");
        return -1;
    }

    framebuffer_width_value = mailbox_buffer[5];
    framebuffer_height_value = mailbox_buffer[6];
    framebuffer_pitch_value = mailbox_buffer[33];
    framebuffer_is_rgb_value = mailbox_buffer[24] ? 1U : 0U;
    framebuffer_address_value = KERNEL_ADDR_SPACE((Address)(mailbox_buffer[28] & 0x3FFFFFFF));
    framebuffer_ready = 1;

    log_info("Framebuffer ready: %ux%u pitch=%u addr=0x%lX", framebuffer_width_value, framebuffer_height_value, framebuffer_pitch_value, framebuffer_address_value);
    return 0;
}

int graphics_is_ready(void)
{
    return framebuffer_ready;
}

unsigned int graphics_width(void)
{
    return framebuffer_width_value;
}

unsigned int graphics_height(void)
{
    return framebuffer_height_value;
}

unsigned int graphics_pitch(void)
{
    return framebuffer_pitch_value;
}

unsigned int graphics_is_rgb(void)
{
    return framebuffer_is_rgb_value;
}

Address graphics_framebuffer(void)
{
    return framebuffer_address_value;
}

void graphics_draw_pixel(unsigned int x, unsigned int y, unsigned int color)
{
    unsigned int *pixel;

    if (!framebuffer_ready || x >= framebuffer_width_value || y >= framebuffer_height_value)
    {
        return;
    }

    pixel = (unsigned int *)(framebuffer_address_value + ((Address)y * framebuffer_pitch_value) + ((Address)x * 4));
    *pixel = color;
}

void graphics_fill_rect(unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned int color)
{
    unsigned int clipped_width;
    unsigned int clipped_height;

    if (!framebuffer_ready || width == 0 || height == 0)
    {
        return;
    }

    clipped_width = framebuffer_clip_width(x, width);
    clipped_height = framebuffer_clip_height(y, height);
    for (unsigned int row = 0; row < clipped_height; row++)
    {
        unsigned int *pixel = (unsigned int *)(framebuffer_address_value + ((Address)(y + row) * framebuffer_pitch_value) + ((Address)x * 4));
        for (unsigned int col = 0; col < clipped_width; col++)
        {
            pixel[col] = color;
        }
    }
}

void graphics_clear(unsigned int color)
{
    graphics_fill_rect(0, 0, framebuffer_width_value, framebuffer_height_value, color);
}