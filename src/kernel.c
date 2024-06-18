//
// Created by Thang Cao on 5/23/24.
//
#include "../include/arch/cortex-a53/boot/uart1.h"
#include "../include/printf.h"
#include "../include/device.h"
#include "../include/irq.h"            // some code in device irq or arch irq
#include "../include/log.h"
#include "../include/task.h"
#include "../include/timer.h"
#include "../include/memory.h"
#include "hal/hal.h"

void kernel_load_user_elf(){
    _trace("Starting process\n");

    _trace("Opening file\n");    

    _trace("file opened %d\n", 0);

   while(1);
}

void do_test();

void kernel_main(){
    hal_init();    
    
    // need to initialize device before output anything
    device_init();

    // init memory management
    log_info("Enabling memory management...\n");
    init_memory_management();

    // test
    do_test();

    // create thread
    process_copy_thread(PF_KTHREAD, (Address)&kernel_load_user_elf, 0);

//    extern void *user_begin, *user_end;
//    create_user_process((Address)(&user_begin), &user_end-&user_begin);

    while(1){
         kinfo("Printing from thread %s. Yeilding...", current_task->name);
        
        // cleanup_zombie_processes();
        schedler_schedule();
        // delay(1000000000);
    }
}