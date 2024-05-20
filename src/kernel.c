#include "mini_uart.h"
#include "printf.h"
#include "utils.h"
#include "debugger/dbg.h"
#include "timer.h"
#include "irq.h"
#include "sched.h"
#include "fork.h"
#include "kprint.h"
#include "memory.h"
#include "fork.h"
#include "sys.h"

void user_process1(char *array)
{
    char buf[2] = {0};
    while (1){
        for (int i = 0; i < 5; i++){
            buf[0] = array[i];
            call_sys_write(buf);
            delay(100000);
        }
    }
}

void user_process(){
    char buf[30];
    memzero(buf, 30);

    tfp_sprintf(buf, "User process started\n\r");
    call_sys_write(buf);
    unsigned long stack = call_sys_malloc();
    if (stack < 0) {
        printf("Error while allocating stack for process 1\n\r");
        return;
    }
    int err = call_sys_clone((unsigned long)&user_process1, (unsigned long)"12345", stack);
    if (err < 0){
        printf("Error while clonning process 1\n\r");
        return;
    }
    stack = call_sys_malloc();
    if (stack < 0) {
        printf("Error while allocating stack for process 1\n\r");
        return;
    }
    err = call_sys_clone((unsigned long)&user_process1, (unsigned long)"abcd", stack);
    if (err < 0){
        printf("Error while clonning process 2\n\r");
        return;
    }
    call_sys_exit();
}

void kernel_process(){
    printf("Kernel process started. EL %d\r\n", get_el());
    int err = move_to_user_mode((unsigned long)&user_process);
    if (err < 0){
        printf("Error while moving process to user mode\n\r");
    }
}

void kernel_main(void)
{

	uart_init();
    init_printf(0, putc);

    kinfo("kernel_main: Initializing IRQ...");
    disable_irq();
    irq_vector_init();

    kinfo("kernel_main: Initializing Timer...");
    timer_init();

    kinfo("kernel_main: Enabling interrupt controllers & timer...");
    enable_interrupt_controller();
    enable_irq();

    kinfo("kernel_main: Kernel is running at EL%d", get_el());
    kprint("\nWELCOME TO ROS\n");

    kinfo("kernel_main: Starting Kernel process...");
    int res = copy_process(PF_KTHREAD, (unsigned long)&kernel_process, 0, 0);
    if (res < 0) {
        printf("error while starting kernel process");
        return;
    }

    while (1){
        schedule();
    }
}
