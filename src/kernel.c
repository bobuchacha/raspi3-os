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

void user_process(){

//    asm volatile("svc #0");
    printf("This is from user process %d, running at EL%d\n\n", current_task->id, get_el());
}

void kernel_process(){
	printf("Kernel process started. EL %d\r\n", get_el());
    // asm volatile ("___breakpoint:");
	int err = move_to_user_mode((unsigned long)&user_process);
	// if (err < 0){
	// 	printf("Error while moving process to user mode\n\r");
	// } 
    while (1);
}


extern long get_sp();
extern void pushl(long);
extern long popl();
extern void push_pair(long, long);
extern long pop_pair();

void test_push_pop(){
    kdebug("Current SP is 0x%lX", get_sp());
    pushl(0x1234000056780000);
    kdebug("SP after pushl 0x1234000056780000 is 0x%lX", get_sp());
    pushl(0xDEADCABD12345678);
    kdebug("SP after pushl 0xDEADCABD12345678 is 0x%lX", get_sp());
    long i = popl();
    kdebug("SP after popl is 0x%lX, value of the pop is 0x%lX", get_sp(), i);
    i = popl();
    kdebug("SP after popl is 0x%lX, value of the pop is 0x%lX", get_sp(), i);

    printf("-------- PAIR --------- \n");
    push_pair(0x1234123412341234, 0x5678567856785678);
    kdebug("SP after push_pair is 0x%lX", get_sp());
    i = pop_pair();
    long x1;
    asm volatile ("mov %0, x1" : "=r"(x1) ::);
    kdebug("SP after pop_pair is 0x%lX, value of the pop is 0x%lX and 0x%lX", get_sp(), i, x1);

    asm volatile("brk #0");

    printf("OK");
}

void kernel_main(){
    
    // need to initialize device before output anything
    device_init();

    // init memory management
    log_info("Enabling memory management...\n");
    init_memory_management();

    int res = copy_process(PF_KTHREAD, &kernel_process, 0, 0);
    if (!res) log_error("Can not start kernel process\n\n");

    res = copy_process(PF_KTHREAD, &process, "Hello WOrld\n", 0);
    if (!res) log_error("Can not start kernel process\n\n");

    res = copy_process(PF_KTHREAD, &process, "Thang Cao\n", 0);
    if (!res) log_error("Can not start kernel process\n\n");

    while(1){
        //  schedler_schedule();
    }
}