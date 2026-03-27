#include "ros.h"
#include "printf.h"
#include "log.h"
#include "task.h"
#include "percpu.h"
#include "memory.h"
#include "utils.h"

/* Allocate per-CPU kernel stacks. Each stack is THREAD_SIZE bytes. */
static unsigned char percpu_stacks[MAX_CPUS][THREAD_SIZE] __attribute__((aligned(16)));

/* Per-CPU idle task storage. */
static Task idle_tasks[MAX_CPUS];

/* Bitmask of CPUs that have finished enough setup to run scheduled work. */
static volatile unsigned int percpu_online_mask = 1U;

/* Round-robin cursor used when placing newly created work on secondaries. */
static unsigned int percpu_next_target_cpu = 1U;

/**
 * percpu_mark_cpu_online
 *
 * Record that `cpu_index` has finished secondary bring-up and can accept work.
 */
void percpu_mark_cpu_online(unsigned int cpu_index) {
    if (cpu_index >= MAX_CPUS) return;

    // Publish the CPU as online so later task placement can target it.
    percpu_online_mask |= (1U << cpu_index);
}

/**
 * percpu_cpu_is_online
 *
 * Return whether `cpu_index` is ready for scheduled work.
 */
Bool percpu_cpu_is_online(unsigned int cpu_index) {
    if (cpu_index >= MAX_CPUS) return false;
    return (percpu_online_mask & (1U << cpu_index)) != 0U;
}

/**
 * percpu_select_target_cpu
 *
 * Pick a CPU for a newly created runnable task.
 *
 * Strategy:
 *   - prefer online secondary CPUs in round-robin order
 *   - fall back to CPU0 if no secondary is online yet
 */
unsigned int percpu_select_target_cpu(void) {
    unsigned int start_cpu;

    if (MAX_CPUS <= 1) return 0;

    start_cpu = percpu_next_target_cpu;
    if (start_cpu == 0 || start_cpu >= MAX_CPUS) {
        start_cpu = 1;
    }

    // Walk the online secondaries once and keep the cursor moving so work spreads out.
    for (unsigned int offset = 0; offset < MAX_CPUS - 1; ++offset) {
        unsigned int cpu_index = start_cpu + offset;

        if (cpu_index >= MAX_CPUS) {
            cpu_index = 1 + (cpu_index - MAX_CPUS);
        }
        if (!percpu_cpu_is_online(cpu_index)) {
            continue;
        }

        // Advance the cursor past the CPU we just selected.
        percpu_next_target_cpu = cpu_index + 1;
        if (percpu_next_target_cpu >= MAX_CPUS) {
            percpu_next_target_cpu = 1;
        }
        return cpu_index;
    }

    return 0;
}

/**
 * percpu_kick_cpu
 *
 * Wake sleeping CPUs after a task is queued for one of them.
 */
void percpu_kick_cpu(unsigned int cpu_index) {
    if (cpu_index == 0 || cpu_index >= MAX_CPUS) return;

    // AArch64 SEV wakes all WFE waiters; affinity filtering keeps unrelated CPUs idle.
    asm volatile("sev\n" ::: "memory");
}

/**
 * percpu_idle_task
 *
 * Return the address of the CPU-local idle task.
 *
 * Args:
 *   cpu_index: Logical CPU index.
 *
 * Returns:
 *   Pointer to the idle task for that CPU, or null for an invalid index.
 */
Task* percpu_idle_task(unsigned int cpu_index) {
    if (cpu_index >= MAX_CPUS) return 0;
    return &idle_tasks[cpu_index];
}

/**
 * percpu_stack_top
 *
 * Returns the stack top address for `cpu_index`.
 */
unsigned long percpu_stack_top(unsigned int cpu_index) {
    if (cpu_index >= MAX_CPUS) return 0;
    return (unsigned long)&percpu_stacks[cpu_index][THREAD_SIZE];
}

/**
 * percpu_init_secondary
 *
 * Prepare a minimal kernel `idle` Task for the secondary CPU. This sets up
 * the `cpu_context.sp` so context switches land on the per-CPU stack and
 * inserts the idle task into the global `tasks[]` table so the scheduler
 * can see it. This function intentionally performs only the bare state
 * setup required for a secondary to enter the scheduler.
 *
 * Args:
 *   cpu_index: Logical CPU index (0..MAX_CPUS-1). Index 0 is the primary
 *              CPU and should already have a running task.
 */
void percpu_init_secondary(unsigned int cpu_index) {
    if (cpu_index == 0 || cpu_index >= MAX_CPUS) return;

    Task* t = &idle_tasks[cpu_index];
    // Zero the task struct then initialize key fields.
    for (unsigned int i = 0; i < sizeof(Task) / sizeof(unsigned long); ++i) {
        ((unsigned long*)t)[i] = 0;
    }

    // Initialize basic scheduler-visible fields.
    t->id = cpu_index;
    t->state = TASK_RUNNING;
    t->counter = 0;
    t->priority = PRIORITY_NORMAL;
    t->cpu_affinity = cpu_index;
    t->preempt_count = 0;
    t->flags = PF_KTHREAD;
    t->kernel_stack_page = 0;
    t->mm.pgd = get_pgd();
    t->name = (Buffer)"KERNEL IDLE";

    // Set the saved kernel SP in the task's cpu_context so switching to
    // this task will restore the correct stack pointer.
    t->cpu_context.sp = percpu_stack_top(cpu_index);

    // Keep secondary idle tasks private to their CPUs for now. They are not
    // inserted into the global scheduler task table until true per-CPU
    // runqueues exist.
}
