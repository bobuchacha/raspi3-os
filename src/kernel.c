//
// Created by Thang Cao on 5/23/24.
//
#include "arch/cortex-a53/boot/uart1.h"
#include "printf.h"
#include "device/raspi3b.h"
#include "irq.h"            // some code in device irq or arch irq
#include "log.h"

static unsigned char _putp[1024];

void kernel_main(){

    // initialize printf to use our uart1 for now
    uart0_init();
    init_printf(0, uart0_putc);
    log_info("Initialize kernel...\n");

    log_info("Initializing IRQ...\n");
    disable_irq();
    irq_vector_init();

    log_info("Initializing Timer...\n");
    timer_init();

    log_info("Enabling interrupt controllers & timer...\n");
    enable_interrupt_controller();
    enable_irq();
    
    log_info("Enabling memory management...\n");
    init_memory_management();

    kinfo("kernel_main: Kernel is running at EL%d\n", get_el());
    kprint("\nWELCOME TO ROS\n");

//    kdump(&kernel_main);

    while(1);
}