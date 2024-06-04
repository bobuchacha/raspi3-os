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

void process(register char *array)
{
    kdebug("Current Task: %d. Running at EL%d\n", current_task->id, get_el());
    
    
    int i = 0;
    // asm volatile("mov x8, 0xdead; svc #0xFFF3");
    while (1){
        i++;
        printf(array);
        wait_cycles(50000000);
    }
}

extern unsigned long user_process;

void kernel_process(){
    unsigned long real_user_addess = (unsigned long)(&user_process) & 0xFFFFFFFF;

	printf("Kernel process started. EL %d\r\n", get_el());
    kdebug("I got address of our user_process at 0x%lx\n\n", &user_process);
    kdebug("In memory, our user_process is at 0x%lx\n\n", real_user_addess);

    asm volatile ("___breakpoint:");
	int err = move_to_user_mode(real_user_addess);
	if (err < 0){
		printf("Error while moving process to user mode\n\r");
	} 
    // // while (1);
}


void kernel_main(){
    
    // need to initialize device before output anything
    device_init();

    // init memory management
    log_info("Enabling memory management...\n");
    init_memory_management();

    int res = copy_process(PF_KTHREAD, &kernel_process, 0, 0);
    if (!res) log_error("Can not start kernel process\n\n");

    // res = copy_process(PF_KTHREAD, &process, "Hello WOrld\n", 0);
    // if (!res) log_error("Can not start kernel process\n\n");

    // res = copy_process(PF_KTHREAD, &process, "Thang Cao\n", 0);
    // if (!res) log_error("Can not start kernel process\n\n");

    while(1){
        //  schedler_schedule();
    }
}