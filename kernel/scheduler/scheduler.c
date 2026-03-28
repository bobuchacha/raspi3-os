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

static Process init_process = { INIT_PROCESS };

// Keep the bootstrap task in THREAD_SIZE-aligned, page-sized storage because
// task_pt_regs() computes frames relative to a full task stack page.
typedef union bootstrap_task_page {
	struct task_struct task;
	unsigned char stack[THREAD_SIZE];
} BootstrapTaskPage;

static BootstrapTaskPage init_task_page __attribute__((aligned(THREAD_SIZE))) = {
	.task = INIT_TASK(&init_process)
};

#define init_task (init_task_page.task)
struct task_struct* current_tasks[NR_CPUS] = {
	&(init_task),
};
int current_task_ids[NR_CPUS] = {
	0,
};
struct task_struct* tasks[NR_TASKS] = {
	&(init_task),
};
Process* processes[NR_PROCESSES] = {
	&(init_process),
};
int nr_tasks = 1;
int nr_processes = 1;
static unsigned long schedler_ticks = 0;
static volatile unsigned int scheduler_spinlock = 0;

static void schedler_bootstrap_state(void) {
	if (!init_process.main_thread) {
		init_process.main_thread = &init_task;
		init_process.main_thread_id = 0;
		init_process.thread_count = 1;
		init_task.process = &init_process;
	}
}

static inline unsigned int scheduler_spin_try_acquire(volatile unsigned int* lock) {
	unsigned int previous;
	unsigned int status;

	asm volatile(
		"ldaxr %w0, [%2]\n"
		"cbnz %w0, 1f\n"
		"mov %w0, #1\n"
		"stxr %w1, %w0, [%2]\n"
		"cbnz %w1, 2f\n"
		"mov %w0, wzr\n"
		"b 3f\n"
		"1:\n"
		"mov %w1, wzr\n"
		"2:\n"
		"3:\n"
		: "=&r"(previous), "=&r"(status)
		: "r"(lock)
		: "memory");

	return previous == 0U && status == 0U;
}

static inline void scheduler_spin_release(volatile unsigned int* lock) {
	asm volatile(
		"stlr %w1, [%0]\n"
		:
	: "r"(lock), "r"(0U)
		: "memory");
}

void scheduler_lock_acquire(void) {
	while (!scheduler_spin_try_acquire(&scheduler_spinlock)) {
		asm volatile("yield\n");
	}
}

void scheduler_lock_release(void) {
	scheduler_spin_release(&scheduler_spinlock);
}

int current_task_should_return_to_user(void) {
	schedler_bootstrap_state();
	return current_task && !(current_task->flags & PF_KTHREAD);
}

static int schedler_task_matches_cpu(const struct task_struct* task, unsigned int cpu) {
	return task && task->cpu_affinity == cpu;
}

static int schedler_task_is_ready(const struct task_struct* task) {
	return task && task->state == TASK_READY;
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
		task->state = TASK_READY;
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
	schedler_bootstrap_state();

	// Prevent nested scheduling while this routine searches for the next runnable task.
	preempt_disable();

	unsigned int cpu = task_cpu_index();
	int next, c;
	int has_ready;
	struct task_struct* p;
	struct task_struct* fallback;
	struct task_struct* idle_task;
	struct task_struct* next_task;

	idle_task = percpu_idle_task(cpu);
	fallback = schedler_task_matches_cpu(current_task, cpu) && (current_task->state == TASK_RUNNING || current_task->state == TASK_READY)
		? current_task
		: (schedler_task_matches_cpu(idle_task, cpu) && (idle_task->state == TASK_RUNNING || idle_task->state == TASK_READY) ? idle_task : 0);
	while (1) {
		// Pick the runnable task with the largest remaining counter.
		c = -1;
		next = 0;
		has_ready = 0;
		scheduler_lock_acquire();
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
			if (schedler_task_is_ready(p) && p->counter > c) {
				has_ready = 1;
				c = p->counter;
				next = i;
			}
			else if (schedler_task_is_ready(p)) {
				has_ready = 1;
			}
		}

		if (c > 0) {
			scheduler_lock_release();
			break;
		}

		if (!has_ready) {
			if (fallback) {
				fallback->counter = fallback->priority;
			}
			scheduler_lock_release();
			break;
		}

		// Refill timeslices when every runnable task has exhausted its current counter.
		for (int i = 0; i < NR_TASKS; i++) {
			p = tasks[i];
			if (schedler_task_matches_cpu(p, cpu) && p->state == TASK_READY) {
				p->counter = (p->counter >> 1) + p->priority;
			}
		}
		scheduler_lock_release();
	}

	next_task = c > 0 ? tasks[next] : fallback;
	if (!next_task) {
		next_task = idle_task;
	}
	if (next_task && next_task->state == TASK_READY) {
		scheduler_lock_acquire();
		next_task->state = TASK_RUNNING;
		scheduler_lock_release();
	}

	// Hand control to the best runnable task selected by the loop above.
	schedler_switch_to(next_task);
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
	schedler_bootstrap_state();
	scheduler_lock_acquire();
	if (current_task->state == TASK_RUNNING) {
		current_task->state = TASK_READY;
	}
	current_task->counter = 0;
	scheduler_lock_release();
	enable_irq();
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
	schedler_bootstrap_state();

	// Record the wakeup deadline and move the task out of the runnable set.
	preempt_disable();
	scheduler_lock_acquire();
	current_task->wakeup_tick = schedler_ticks + ticks;
	current_task->state = TASK_SLEEPING;
	scheduler_lock_release();
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
	unsigned int cpu = task_cpu_index();
	// _trace("Switching task from %d to %d",  current_task->id, next->id);

	if (current_task == next) {
		current_task_ids[cpu] = current_task ? (int)current_task->id : -1;
		if (current_task && current_task->state == TASK_READY) {
			current_task->state = TASK_RUNNING;
		}
		return;
	}
	struct task_struct* prev = current_task;
	current_task = next;
	current_task_ids[cpu] = next ? (int)next->id : -1;
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
	schedler_bootstrap_state();
	if (task_cpu_index() == 0) {
		scheduler_lock_acquire();
		// Advance time on the boot CPU so sleep accounting has a single timekeeper.
		++schedler_ticks;

		// Move due sleeping tasks back into the runnable set before considering preemption.
		schedler_wake_sleeping_tasks();
		scheduler_lock_release();
	}

	// Charge the running task for the tick that just elapsed.
	--current_task->counter;
	if (current_task->counter > 0 || current_task->preempt_count > 0) {
		return;
	}

	// Force a reschedule once the task's timeslice is gone and preemption is allowed.
	scheduler_lock_acquire();
	if (current_task->state == TASK_RUNNING) {
		current_task->state = TASK_READY;
	}
	current_task->counter = 0;
	scheduler_lock_release();
	enable_irq();
	_schedule();
}
