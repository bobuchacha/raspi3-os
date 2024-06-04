#include "ros.h"
#include "printf.h"
#include "device.h"
#include "task.h"
#include "irq.h"            // some code in device irq or arch irq
#include "log.h"
#include "memory.h"

#define _trace          log_info
#define _trace_printf   printf

extern unsigned long ret_from_fork();

/**
 * copy a process
*/
int copy_process(unsigned long clone_flags, unsigned long fn, unsigned long arg, unsigned long stack)
{
	preempt_disable();
	struct task_struct *p;

	p = (struct task_struct *) mem_alloc_page();
	if (!p) {
		return -1;
	}

	struct pt_regs *childregs = task_pt_regs(p);
	memzero((unsigned long)childregs, sizeof(struct pt_regs));
	memzero((unsigned long)&p->cpu_context, sizeof(struct cpu_context));

	if (clone_flags & PF_KTHREAD) {
		p->cpu_context.x19 = fn;
		p->cpu_context.x20 = arg;
	} else {
		struct pt_regs * cur_regs = task_pt_regs(current_task);
		*childregs = *cur_regs;
		childregs->regs[0] = 0;
		childregs->sp = stack + PAGE_SIZE;
		p->stack = stack;
	}
	p->flags = clone_flags;
	p->priority = current_task->priority;
	p->state = TASK_RUNNING;
	p->counter = p->priority;
	p->preempt_count = 1; //disable preemtion until schedule_tail

	p->cpu_context.pc = (unsigned long)ret_from_fork;
	p->cpu_context.sp = (unsigned long)childregs;
	
	int pid = ++nr_tasks;
	p->id = pid;
	_trace_printf("Number of numming task: %d\n", pid);
	tasks[pid] = p;
	preempt_enable();
	return pid;
}


/**
 * our pt_regs are right above the stack
*/
struct pt_regs * task_pt_regs(struct task_struct *tsk){
	unsigned long p = (unsigned long)tsk + THREAD_SIZE - sizeof(struct pt_regs);
	return (struct pt_regs *)p;
}

int move_to_user_mode(unsigned long pc)
{
	_trace("Current task ");
	_trace_printf("%d\n", current_task->id);
	
	struct pt_regs *regs = task_pt_regs(current_task);
	
	_trace("Current task registers ");
	_trace_printf("0x%lX\n", regs);

	// asm volatile("brk #00");

	memzero((unsigned long)regs, sizeof(*regs));
	regs->pc = pc;
	regs->pstate = PSR_MODE_EL0t;
	unsigned long stack = mem_alloc_page(); //allocate new user stack
	if (!stack) {
		return -1;
	}
	regs->sp = stack + PAGE_SIZE;
	current_task->stack = stack;
	return 0;
}