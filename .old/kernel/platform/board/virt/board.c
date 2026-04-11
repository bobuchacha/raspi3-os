#include "device.h"
#include "device/virt.h"
#include "graphics.h"
#include "hal/hal.h"
#include "irq.h"
#include "log.h"
#include "printf.h"
#include "touch.h"

static unsigned char virt_printf_buffer[1024];

static const struct BlockDeviceDriver virtio_blk_driver = {
    .block_read = &virtio_blk_read,
    .block_write = &virtio_blk_write,
};

void device_init(void) {
    uart0_init();
    init_printf(virt_printf_buffer, uart0_putc);
    disable_irq();
    irq_vector_init();
    log_info("virt board: PL011 console ready");

    if (virtio_blk_init() == 0) {
        hal_block_register_device("virtio-blk", 0, &virtio_blk_driver);
    }
    else {
        log_warning("virt board: no virtio block device detected");
    }
}

int device_init_graphics(void) {
    return graphics_init();
}

int device_init_touch(void) {
    return touch_init();
}

void device_poll_touch(void) {
    touch_poll();
}