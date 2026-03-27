#ifndef PERCPU_H
#define PERCPU_H

#include "task.h"

/* Number of supported CPU cores (matches the boot secondary slots). */
#define MAX_CPUS NR_CPUS

/* Initialize per-CPU data for the given secondary CPU index. */
void percpu_init_secondary(unsigned int cpu_index);

/* Mark a CPU as online once it is ready to accept scheduled work. */
void percpu_mark_cpu_online(unsigned int cpu_index);

/* Report whether a CPU has completed enough bring-up to run tasks. */
Bool percpu_cpu_is_online(unsigned int cpu_index);

/* Choose a target CPU for a newly created runnable task. */
unsigned int percpu_select_target_cpu(void);

/* Wake CPUs waiting in WFE so they can notice newly queued work. */
void percpu_kick_cpu(unsigned int cpu_index);

/* Get top-of-stack pointer for a CPU's kernel stack. */
unsigned long percpu_stack_top(unsigned int cpu_index);

/* Return the per-CPU idle task used during secondary-core bring-up. */
Task* percpu_idle_task(unsigned int cpu_index);

#endif // PERCPU_H
