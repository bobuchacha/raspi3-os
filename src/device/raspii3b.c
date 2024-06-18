#include <ros.h>
#include "irq.h"
#include "memory.h"
#include "device.h"
#include "timer.h"
#include "log.h"
#include "utils.h"
#include "device/sd.h"
#include "../hal/hal.h"

static unsigned char _putp[1024];

void device_init(){
    // initialize printf to use our uart1 for now
    uart0_init();
    init_printf(_putp, uart0_putc);

    _trace("Initializing IRQ...\n");
    disable_irq();
    irq_vector_init();

    _trace("Initializing Timer...\n");
    timer_init();

    _trace("Enabling interrupt controllers & timer...\n");
    enable_interrupt_controller();
    enable_irq();

    // initialize sd card
    _trace("Initialize SD Card");
    if (sd_init() != SD_OK) {
        log_error("SD card initialization error!\n");
    }
    else{
        BlockDeviceDriver sdcard_driver = {
            .block_read = sd_block_read,
            .block_write = sd_block_write
        };
        hal_block_register_device("sdcard", 0, &sdcard_driver);
    }

}   