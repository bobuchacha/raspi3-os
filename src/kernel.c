#include "mini_uart.h"
#include "printf.h"
#include "utils.h"
#include "debugger/dbg.h"
#include "timer.h"
#include "irq.h"
#include "sched.h"
#include "fork.h"
#include "kprint.h"

void process(char *array)
{
    while (1){
        for (int i = 0; i < 5; i++){
            uart_send(array[i]);
            delay(1000000);
        }
    }
}


void kernel_main(void)
{
	uart_init();
    init_printf(0, putc);
    kdebug("WELCOME");
    disable_irq();
    irq_vector_init();
    timer_init();
    enable_interrupt_controller();
    enable_irq();

    int res = copy_process((unsigned long)&process, (unsigned long)"00000");
    if (res != 0) {
        printf("error while starting process 1");
        return;
    }
    res = copy_process((unsigned long)&process, (unsigned long)"11111");
    if (res != 0) {
        printf("error while starting process 2");
        return;
    }
    res = copy_process((unsigned long)&process, (unsigned long)"22222");
    if (res != 0) {
        printf("error while starting process 2");
        return;
    }
    res = copy_process((unsigned long)&process, (unsigned long)".....");
    if (res != 0) {
        printf("error while starting process 2");
        return;
    }


    while (1){
        asm volatile("nop");
        //schedule();
    }
}
