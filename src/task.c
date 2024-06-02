#include "ros.h"
#include "printf.h"
#include "device.h"
#include "task.h"
#include "irq.h"            // some code in device irq or arch irq
#include "log.h"
#include "memory.h"

extern unsigned long ret_from_fork();

/**
 * copy a process
*/
int copy_process(unsigned long fn, unsigned long arg, int priority)
{
	preempt_disable();
	struct task_struct *p;

	p = (struct task_struct *) mem_alloc_page();
	if (!p) return 1;

	p->priority = priority;
	p->state = TASK_RUNNING;
	p->counter = p->priority;
	p->preempt_count = 1; //disable preemtion until schedule_tail

	p->cpu_context.x19 = fn;
	p->cpu_context.x20 = arg;
	p->cpu_context.pc = (unsigned long)ret_from_fork;
	p->cpu_context.sp = (unsigned long)p + THREAD_SIZE;
	p->id = nr_tasks++;
	tasks[p->id] = p;	
	preempt_enable();
	return 0;
}