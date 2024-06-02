#include "ros.h"
#include "printf.h"
#include "device.h"
#include "task.h"
#include "irq.h"            // some code in device irq or arch irq
#include "log.h"
#include "memory.h"

static struct task_struct init_task = INIT_TASK;
struct task_struct *current_task = &(init_task);
struct task_struct *tasks[NR_TASKS] = {&(init_task), };
int  nr_tasks = 1;

void preempt_disable(void)
{
	current_task->preempt_count++;
}

void preempt_enable(void)
{
	current_task->preempt_count--;
}

/**
 * the main scheduler alogrithm. Select the next task to switch to
*/
void _schedule(void)
{
	preempt_disable();
	
	int next,c;
	struct task_struct * p;
	while (1) {
		c = -1;
		next = 0;
		for (int i = 0; i < NR_TASKS; i++){
			p = tasks[i];
			if (p) {
				// kdebug("For block 1: Checking task %d", i);
				// kprint("              --> %s Counter %d, priority: %d, preempt: %d, current c %d\n", 
				// 	p->state == TASK_RUNNING ? "Running" : "Sleep", 
				// 	p->counter, 
				// 	p->priority, 
				// 	p->preempt_count, 
				// 	c
				// 	);
			}
			if (p && p->state == TASK_RUNNING && p->counter > c) {
				c = p->counter;
				// kprint("              --> New c %d\n", c);
				next = i;
			}
		}
		
		if (c) {
			break;
		}
		for (int i = 0; i < NR_TASKS; i++) {
			p = tasks[i];
			if (p) {
				p->counter = (p->counter >> 1) + p->priority;
				// p->counter = (p->counter + 1) + p->priority;
			}
		}
	}
	
	schedler_switch_to(tasks[next]);
	preempt_enable();
	
}

/**
 * This is called from current task to end its execution and ask cpu to switch to the next one
*/
void schedler_schedule(void)
{
	current_task->counter = 0;
	_schedule();
}

/**
 * Switch CPU to next task
*/
void schedler_switch_to(struct task_struct * next) 
{
	if (current_task == next) 
		return;
	struct task_struct * prev = current_task;
	current_task = next;
	cpu_switch_to(prev, next);
}

/**
 * @brief A function that called from ret_from_fork in scheduler.S
 * @param void
 **/
void schedule_tail(void) {
	preempt_enable();
}


/**
 * An additional timer tick callee, this function is called from kernel timer_tick() function and 
 * it handles scheduling process
*/
void schedler_timer_tick()
{
    --current_task->counter;
	if (current_task->counter>0 || current_task->preempt_count >0) {
		return;
	}
    
	current_task->counter=0;
	enable_irq();
	_schedule();
	disable_irq();
}