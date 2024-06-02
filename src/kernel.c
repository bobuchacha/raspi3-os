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

void process(register char *array)
{
    kdebug("Current Task: %d. MY array is at 0x%lX", current_task->id, array);
    
    
    asm volatile("svc #0");

    kdebug("MY array is at 0x%lX", array);
    int i = 0;
    // asm volatile("mov x8, 0xdead; svc #0xFFF3");
    while (1){
        i++;
        uart0_puts(array);
        if (i==10) {asm volatile("svc #0"); i = 0;}
        wait_cycles(50000000);
    }
}


void kernel_main(){
    
    // need to initialize device before output anything
    device_init();

    // init memory management
    log_info("Enabling memory management...\n");
    init_memory_management();

    int res = copy_process((unsigned long)&process, (unsigned long)"12345\n", PRIORITY_MEDIUM);
	if (res != 0) {
		printf("error while starting process 1");
		return;
	}
	res = copy_process((unsigned long)&process, (unsigned long)"abcde\n", PRIORITY_NORMAL);
	if (res != 0) {
		printf("error while starting process 2");
		return;
	}
    
   struct task_struct *p = tasks[0];

   printf("Init task info: \n");
   printf(" - State: %s\n", p->state == TASK_RUNNING ? "Running" : "Sleep");

    while(1){
        //  schedler_schedule();
    }
}