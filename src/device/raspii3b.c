#include "ros.h"
#include "irq.h"
#include "memory.h"
#include "device.h"
#include "timer.h"
#include "log.h"
#include "utils.h"
#include "device/sd.h"

#define _trace          log_info
#define _trace_printf   printf

static unsigned char _putp[1024];

void device_init(){
    // initialize printf to use our uart1 for now
    uart0_init();
    init_printf(_putp, uart0_putc);

    log_info("Initializing IRQ...\n");
    disable_irq();
    irq_vector_init();

    log_info("Initializing Timer...\n");
    timer_init();

    log_info("Enabling interrupt controllers & timer...\n");
    enable_interrupt_controller();
    enable_irq();
    
    // initialize sd card
    if (sd_init() != SD_OK) {
        log_error("SD card initialization error!\n");
    }
}   