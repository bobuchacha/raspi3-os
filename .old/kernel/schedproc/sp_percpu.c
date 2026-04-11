/**
 * sp_percpu.c
 *
 * Per-CPU idle task and stack support. The current scheduler context is still
 * single-CPU, but the kernel already queries per-CPU idle tasks and stacks for
 * diagnostics and fallback scheduling. This file implements the minimal per-CPU
 * state needed to support those queries and to prepare for the next SMP step of
 * true per-CPU run queues.
 */

#include "log.h"
#include "memory.h"
#include "percpu.h"
#include "ros.h"
#include "task.h"
#include "utils.h"

extern unsigned long start_thread_context(void);

/*
 * The current kernel still boots only on CPU0 by default, but several
 * subsystems already query per-CPU idle tasks and stacks. Keep that support in
 * kernel/schedproc so the new scheduler/process module owns the remaining
 * scheduler-adjacent compatibility code.
 */

 /* Statically reserved kernel stacks used by the per-CPU idle tasks. */
static unsigned char percpu_stacks[MAX_CPUS][THREAD_SIZE] __attribute__((aligned(16)));
/* Legacy-visible idle task descriptors, one per CPU slot. */
static Task idle_tasks[MAX_CPUS];
/* Bitmask telling which CPUs have finished enough setup to accept work. */
static volatile unsigned int percpu_online_mask = 1U;
/* Round-robin cursor used when selecting a secondary target CPU. */
static unsigned int percpu_next_target_cpu = 1U;

/* Report whether this build should wake and schedule onto secondary CPUs. */
Bool percpu_multi_cpu_enabled(void) {
#if USE_MULTI_CPU
    return true;
#else
    return false;
#endif
}

/*
 * percpu_idle_loop
 *
 * Run a minimal kernel housekeeping loop from a dedicated per-CPU kernel-thread
 * context. This gives the scheduler a clean EL1 stack to land on when it
 * cannot safely hand control from one user task directly into another task's
 * saved syscall frame, such as during `sys_exit()`.
 */
static void percpu_idle_loop(Pointer arg) {
    unsigned int cpu_index = (unsigned int)(unsigned long)arg;

    while (1) {
        schedler_schedule();

        if (current_task == &idle_tasks[cpu_index]) {
#if defined(ROS_BOARD_VIRT)
            asm volatile("yield\n" ::: "memory");
#else
            asm volatile("wfe\n");
#endif
        }
    }
}

/* Mark one CPU as online so future work placement may target it. */
void percpu_mark_cpu_online(unsigned int cpu_index) {
    if (!percpu_multi_cpu_enabled() && cpu_index != 0) {
        return;
    }

    if (cpu_index < MAX_CPUS) {
        percpu_online_mask |= (1U << cpu_index);
    }
}

/* Return whether the requested CPU currently accepts scheduled work. */
Bool percpu_cpu_is_online(unsigned int cpu_index) {
    if (cpu_index >= MAX_CPUS) {
        return false;
    }

    return (percpu_online_mask & (1U << cpu_index)) != 0U;
}

/* Choose a secondary CPU in round-robin order, or fall back to CPU0. */
unsigned int percpu_select_target_cpu(void) {
    unsigned int start_cpu;

    if (!percpu_multi_cpu_enabled()) {
        return 0;
    }

    if (MAX_CPUS <= 1) {
        return 0;
    }

    start_cpu = percpu_next_target_cpu;
    if (start_cpu == 0 || start_cpu >= MAX_CPUS) {
        start_cpu = 1;
    }

    for (unsigned int offset = 0; offset < MAX_CPUS - 1; ++offset) {
        unsigned int cpu_index = start_cpu + offset;

        if (cpu_index >= MAX_CPUS) {
            cpu_index = 1 + (cpu_index - MAX_CPUS);
        }
        if (!percpu_cpu_is_online(cpu_index)) {
            continue;
        }

        percpu_next_target_cpu = cpu_index + 1;
        if (percpu_next_target_cpu >= MAX_CPUS) {
            percpu_next_target_cpu = 1;
        }
        return cpu_index;
    }

    return 0;
}

/* Send a broad wake event so sleeping secondaries re-check their run queues. */
void percpu_kick_cpu(unsigned int cpu_index) {
    if (!percpu_multi_cpu_enabled()) {
        return;
    }

    if (cpu_index == 0 || cpu_index >= MAX_CPUS) {
        return;
    }

    asm volatile("sev\n" ::: "memory");
}

/* Return the idle task descriptor assigned to one CPU slot. */
Task* percpu_idle_task(unsigned int cpu_index) {
    if (cpu_index >= MAX_CPUS) {
        return 0;
    }

    return &idle_tasks[cpu_index];
}

/* Return the top-of-stack address for the selected per-CPU idle stack. */
unsigned long percpu_stack_top(unsigned int cpu_index) {
    if (cpu_index >= MAX_CPUS) {
        return 0;
    }

    return (unsigned long)&percpu_stacks[cpu_index][THREAD_SIZE];
}

/*
 * percpu_init_idle_task
 *
 * Build one CPU-local idle task descriptor. The task is visible to the rest of
 * the kernel for diagnostics and fallback scheduling, but it is not registered
 * with the schedproc module because the current scheduler context is still
 * single-CPU.
 */
void percpu_init_idle_task(unsigned int cpu_index) {
    Task* task;

    if (cpu_index >= MAX_CPUS) {
        return;
    }

    task = &idle_tasks[cpu_index];
    memzero((Address)task, sizeof(*task));
    task->id = -1L - (long)cpu_index;
    task->state = TASK_READY;
    task->counter = 0;
    task->priority = PRIORITY_NORMAL;
    task->cpu_affinity = cpu_index;
    task->preempt_count = 1;
    task->flags = PF_KTHREAD;
    task->process = processes[0];
    task->mm.pgd = get_pgd();
    task->name = (Buffer)"KERNEL IDLE";
    task->cpu_context.x19 = (unsigned long)&percpu_idle_loop;
    task->cpu_context.x20 = (unsigned long)cpu_index;
    task->cpu_context.pc = (unsigned long)start_thread_context;
    task->cpu_context.sp = percpu_stack_top(cpu_index);
}

/* Initialize the secondary CPU idle task after that CPU reaches C code. */
void percpu_init_secondary(unsigned int cpu_index) {
    if (!percpu_multi_cpu_enabled()) {
        return;
    }

    if (cpu_index == 0 || cpu_index >= MAX_CPUS) {
        return;
    }

    percpu_init_idle_task(cpu_index);
    idle_tasks[cpu_index].state = TASK_RUNNING;
}
