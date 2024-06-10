//
// Created by Thang Cao on 5/23/24.
//
#include "arch/cortex-a53/boot/uart1.h"
#include "printf.h"
#include "device.h"
#include "irq.h"            // some code in device irq or arch irq
#include "log.h"
#include "task.h"
#include "timer.h"

#define _trace          log_info
#define _trace_printf   printf


    
extern long get_sp_el0();
extern long get_el();

int create_user_process(unsigned long *program_addr, unsigned long program_size);
ulong process_copy_thread(unsigned long flags, void *program_addr, void *arg);

void kernel_process1(ulong arg){
    extern void user_begin, user_end;
    int err = move_to_user_mode(&user_begin, &user_end-&user_begin);
    if (err < 0){
		printf("Error while moving process to user mode\n\r");
	} 
}

void kernel_process2(ulong arg){
    int i = 0;
    while(1){
        kinfo("This is process %d - %d. My SP: 0x%lx\n\n\n",current_task->id, i++, current_task->cpu_context.sp);
        // process_dump_task(current_task->id);
        wait_msec(1000000);
    }
}
extern unsigned long ret_from_fork();

void kernel_main(){
    
    // need to initialize device before output anything
    device_init();

    // init memory management
    log_info("Enabling memory management...\n");
    init_memory_management();

    extern void user_begin, user_end;

    

    create_user_process(&user_begin, &user_end-&user_begin);
//     create_user_process(&user_begin, &user_end-&user_begin);
// create_user_process(&user_begin, &user_end-&user_begin);
    
    while(1){
        // kinfo("Printing from thread %s. Yeilding...", current_task->name);
        
        // cleanup_zombie_processes();
        // schedler_schedule();
        // delay(1000000000);
    }
}