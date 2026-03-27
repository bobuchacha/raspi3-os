#include "ros.h"
#include "printf.h"
#include "device.h"
#include "task.h"
#include "irq.h" // some code in device irq or arch irq
#include "log.h"
#include "memory.h"
#include "utils.h"
#include "percpu.h"
#include "arch/cortex-a53/dbg.h"

static struct task_struct init_task = INIT_TASK;
struct task_struct* current_tasks[NR_CPUS] = {
	&(init_task),
};
struct task_struct* tasks[NR_TASKS] = {
	&(init_task),
};
int nr_tasks = 1;
static unsigned long schedler_ticks = 0;

static int schedler_task_matches_cpu(const struct task_struct* task, unsigned int cpu) {
	return task && task->cpu_affinity == cpu;
}

/**
 * Wake tasks whose sleep deadline has expired.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Nothing. Sleeping tasks whose `wakeup_tick` is due become runnable again.
 */
static void schedler_wake_sleeping_tasks(void) {
	// Scan the global task table and reactivate any task whose wakeup deadline has passed.
	for (int i = 0; i < NR_TASKS; i++) {
		struct task_struct* task = tasks[i];
		if (!task || task->state != TASK_SLEEPING) {
			continue;
		}
		if (task->wakeup_tick > schedler_ticks) {
			continue;
		}
		task->wakeup_tick = 0;
		task->state = TASK_RUNNING;
		if (task->counter <= 0) {
			task->counter = task->priority;
		}
	}
}

// void print_current_task_id(){
// 	printf("Current Task: %d (%s)", current_task->id, current_task->name);
// }

/**
 * Increment the current task's preemption disable nesting level.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Nothing. Scheduling stays suppressed until matching `preempt_enable` calls unwind.
 */
void preempt_disable(void) {
	// _trace("Preempt disabled ");
	current_task->preempt_count++;
	// _trace_p("current [%d] preempt count: %d\n", current_task->id, current_task->preempt_count);
}

/**
 * Decrement the current task's preemption disable nesting level.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Nothing. The task becomes preemptible again when the nesting level reaches zero.
 */
void preempt_enable(void) {
	// _trace("Preempt enabled ");
	current_task->preempt_count--;
	// _trace_p("current [%d] preempt count: %d\n", current_task->id, current_task->preempt_count);
}

/**
 * Core scheduler loop that selects the next runnable task.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Nothing. Control resumes in the chosen task after `cpu_switch_to` completes.
 */
void _schedule(void) {
	// _trace("scheduling...\n");
	dbg_wait_if_paused();

	// Prevent nested scheduling while this routine searches for the next runnable task.
	preempt_disable();

	unsigned int cpu = task_cpu_index();
	int next, c;
	int has_runnable;
	struct task_struct* p;
	struct task_struct* fallback;
	struct task_struct* idle_task;

	idle_task = percpu_idle_task(cpu);
	fallback = schedler_task_matches_cpu(current_task, cpu) && current_task->state == TASK_RUNNING
		? current_task
		: (schedler_task_matches_cpu(idle_task, cpu) && idle_task->state == TASK_RUNNING ? idle_task : 0);
	while (1) {
		// Pick the runnable task with the largest remaining counter.
		c = -1;
		next = 0;
		has_runnable = 0;
		for (int i = 0; i < NR_TASKS; i++) {
			p = tasks[i];
			if (!schedler_task_matches_cpu(p, cpu)) {
				continue;
			}
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
				has_runnable = 1;
				c = p->counter;
				// kprint("              --> New c %d\n", c);
				next = i;
			}
			else if (p && p->state == TASK_RUNNING) {
				has_runnable = 1;
			}
		}

		if (c > 0) {
			break;
		}

		if (!has_runnable) {
			if (fallback) {
				fallback->counter = fallback->priority;
			}
			break;
		}

		// Refill timeslices when every runnable task has exhausted its current counter.
		for (int i = 0; i < NR_TASKS; i++) {
			p = tasks[i];
			if (schedler_task_matches_cpu(p, cpu) && p->state == TASK_RUNNING) {
				p->counter = (p->counter >> 1) + p->priority;
				// p->counter = (p->counter + 1) + p->priority;
			}
		}
	}

	// Hand control to the best runnable task selected by the loop above.
	schedler_switch_to(c > 0 ? tasks[next] : fallback);
	// _trace("Switch completed!\n");
	preempt_enable();
}

/**
 * Force the current task to yield and ask the scheduler for a replacement.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Nothing. Control continues once this task is scheduled again later.
 */
void schedler_schedule(void) {
	// Zero the remaining budget so the scheduler will pick another runnable task now.
	dbg_wait_if_paused();
	current_task->counter = 0;
	_schedule();
}

/**
 * Report the global scheduler tick count.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Monotonic tick count incremented by the timer interrupt path.
 */
unsigned long schedler_get_ticks(void) {
	return schedler_ticks;
}

/**
 * Put the current task to sleep for a given number of scheduler ticks.
 *
 * Args:
 *   ticks: Number of ticks to sleep.
 *
 * Returns:
 *   Nothing. The caller resumes after the timer path wakes the task.
 */
void schedler_sleep_ticks(unsigned long ticks) {
	if (ticks == 0) {
		return;
	}

	// Record the wakeup deadline and move the task out of the runnable set.
	preempt_disable();
	current_task->wakeup_tick = schedler_ticks + ticks;
	current_task->state = TASK_SLEEPING;
	preempt_enable();

	// Yield immediately so another runnable task can use the CPU.
	schedler_schedule();
}

/**
 * Switch kernel execution from the current task to the selected next task.
 *
 * Args:
 *   next: Task that should become current.
 *
 * Returns:
 *   Nothing. CPU state is exchanged by the architecture-specific switch routine.
 */
void schedler_switch_to(struct task_struct* next) {
	// _trace("Switching task from %d to %d",  current_task->id, next->id);

	if (current_task == next)
		return;
	struct task_struct* prev = current_task;
	current_task = next;
	// Switch to the next task's page tables before restoring its CPU context.
	set_pgd(next->mm.pgd);
	// process_dump_task_struct(current_task);
	// _trace("Switching to ");
	// _trace_printf("next: 0x%lX from 0x%lX\n\n", next, prev);
	cpu_switch_to(prev, next);
}

/**
 * Complete the post-fork scheduler bookkeeping after a context switch.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Nothing. Preemption is re-enabled for the newly scheduled task.
 */
void schedule_tail(void) {
	// _trace("Schedule tail called\n");
	preempt_enable();
}

/**
 * Consume one timer tick and drive scheduler wakeup and preemption decisions.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Nothing. May trigger a reschedule when the current task exhausts its slice.
 */
void schedler_timer_tick() {
	dbg_wait_if_paused();
	if (task_cpu_index() == 0) {
		// Advance time on the boot CPU so sleep accounting has a single timekeeper.
		++schedler_ticks;

		// Move due sleeping tasks back into the runnable set before considering preemption.
		schedler_wake_sleeping_tasks();
	}

	// Charge the running task for the tick that just elapsed.
	--current_task->counter;
	if (current_task->counter > 0 || current_task->preempt_count > 0) {
		return;
	}

	// Force a reschedule once the task's timeslice is gone and preemption is allowed.
	current_task->counter = 0;
	enable_irq();
	_schedule();
	disable_irq();
}