#include "device.h"
#include "device/sd.h"
#include "boot-handoff.h"
#include "graphics.h"
#include "hal/hal.h"
#include "irq.h"
#include "log.h"
#include "memory.h"
#include "touch.h"
#include "timer.h"
#include "utils.h"
#include <ros.h>

static unsigned char put_buffer[1024];

static const struct BlockDeviceDriver sdcard_driver = {
    .block_read = &sd_block_read,
    .block_write = &sd_block_write,
};

static RosBootHandoff* device_boot_handoff(void)
{
    return (RosBootHandoff*)mem_phys_to_virt((PhysAddr)ROS_BOOT_HANDOFF_PHYS_ADDR);
}

void device_init()
{
    RosBootHandoff* handoff = device_boot_handoff();
    Bool uart0_ready = ros_boot_handoff_is_valid(handoff) && (handoff->flags & ROS_BOOT_HANDOFF_FLAG_UART0_READY);
    Bool sd_ready = ros_boot_handoff_is_valid(handoff) && (handoff->flags & ROS_BOOT_HANDOFF_FLAG_SD_READY);

    if (!uart0_ready)
    {
        uart0_init();
    }
    init_printf(put_buffer, uart0_putc);

    _trace("Initializing IRQ...\n");
    disable_irq();
    irq_vector_init();

    _trace("Initializing Timer...\n");
    timer_init();

    _trace("Enabling interrupt controllers & timer...\n");
    enable_interrupt_controller();
    enable_irq();

    _trace("Initialize SD Card");
    if (sd_ready ? (sd_try_import_handoff(handoff) != SD_OK) : (sd_init() != SD_OK))
    {
        log_error("SD card initialization error!\n");
    }
    else
    {
        _trace("Address of block sd device driver 0x%lx, 0x%lx, 0x%lx",
               &sdcard_driver, sdcard_driver.block_read, sdcard_driver.block_write);
        hal_block_register_device("sdcard", 0, &sdcard_driver);
    }
}

int device_init_graphics(void)
{
    return graphics_init();
}

int device_init_touch(void)
{
    return touch_init();
}

void device_poll_touch(void)
{
    touch_poll();
}
