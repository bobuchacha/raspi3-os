#include "arch/cortex-a53/boot/uart1.h"
#include "printf.h"
#include "device.h"
#include "irq.h"            // some code in device irq or arch irq
#include "log.h"
#include "task.h"
#include "timer.h"

void sys_write(char * buf){
	printf("Process %d: %s", current_task->id, buf);
}

int sys_clone(unsigned long stack){
	printf("CLONE");
}

unsigned long sys_malloc(ulong x){
	// process_dump_task_struct(current_task);
	printf("0x%lX\n", x);
}

void sys_exit(ULong result){
	exit_current_process(result);
}

int sys_get_param(int param){
	printf("SYS GET PARAM %d\n\n", param);
	// if (param == 0) return 0xDEAD;
	return 0xDEAD;
}

void * const sys_call_table[] = {sys_write, sys_malloc, sys_clone, sys_exit, sys_get_param};