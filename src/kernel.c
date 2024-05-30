//
// Created by Thang Cao on 5/23/24.
//
#include "arch/cortex-a53/boot/uart1.h"
#include "printf.h"
#include "device/raspi3b.h"
#include "irq.h"            // some code in device irq or arch irq
#include "log.h"

#define _trace          log_info
#define _trace_printf   printf

void kernel_main(){
    
    // need to initialize device before output anything
    device_init();

    // init memory management
    log_info("Enabling memory management...\n");
    init_memory_management();

    //out put some info
    kinfo("kernel_main: Kernel is running at EL%d\n", get_el());
    kprint("\nWELCOME TO ROS\n");

    kmalloc(100);
    kmalloc(200);
    kmalloc(512);
    mem_heap_dump(100);

    while(1);
}