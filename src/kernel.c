#include "mini_uart.h"
#include "uart0.h"
#include "printf.h"
#include "utils.h"
#include "debugger/dbg.h"
#include "timer.h"
#include "irq.h"
#include "sched.h"
#include "fork.h"
#include "kprint.h"
#include "mailbox.h"

unsigned char * msg = "Welcome from Assembly";



void kernel_main(void)
{
	uart_init();
    uart0_init();

    init_printf(0, uart0_putc);
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

    // generate a service call
    asm volatile ("svc #0");

    while (1){
        schedule();
    }
}
