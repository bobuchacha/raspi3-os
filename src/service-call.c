#include "arch/cortex-a53/boot/uart1.h"
#include "printf.h"
#include "device.h"
#include "irq.h"            // some code in device irq or arch irq
#include "log.h"
#include "task.h"
#include "timer.h"

void sys_write(char * buf){
	printf(buf);
}

int sys_clone(unsigned long stack){
}

unsigned long sys_malloc(){
	
}

void sys_exit(){
	// exit_process();
}

void * const sys_call_table[] = {sys_write, sys_malloc, sys_clone, sys_exit};