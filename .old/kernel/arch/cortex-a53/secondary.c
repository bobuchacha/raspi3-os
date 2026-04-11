#include "ros.h"
#include "boot-handoff.h"
#include "printf.h"
#include "log.h"
#include "secondary.h"
#include "percpu.h"
#include "irq.h"
#include "memory.h"

typedef void (*secondary_program_boot_entry_fn_t)(unsigned int cpu_index);
typedef void (*secondary_publish_boot_entries_fn_t)(void);

static RosBootHandoff* secondary_boot_handoff(void) {
    return (RosBootHandoff*)mem_phys_to_virt((PhysAddr)ROS_BOOT_HANDOFF_PHYS_ADDR);
}

/**
 * wake_secondary_cores
 *
 * Populate the secondary release targets for each non-boot CPU and issue
 * a wake event (`sev`). This programs both:
 *  - the local in-kernel low-memory spin table used by our own boot loop
 *  - the Raspberry Pi firmware armstub release slots at 0xE0/0xE8/0xF0
 *
 * The secondary will branch into the low-memory `secondary_boot`
 * trampoline and continue into `secondary_start` once EL/MMU setup is done.
 *
 * This function is safe to call after low-level platform setup and MMU
 * mapping are present (i.e., from `kernel_main`). It intentionally does
 * minimal work on secondaries: they will run `secondary_start` which
 * currently performs light initialization and parks the core.
 */
void wake_secondary_cores(void) {
    unsigned int cpu;
    RosBootHandoff* handoff = secondary_boot_handoff();
    secondary_program_boot_entry_fn_t program_boot_entry;
    secondary_publish_boot_entries_fn_t publish_boot_entries;

    if (!percpu_multi_cpu_enabled()) {
        log_info("Secondary core wake skipped: USE_MULTI_CPU is disabled");
        return;
    }

    _trace("Waking secondary cores...\n");

    if (!ros_boot_handoff_is_valid(handoff)) {
        log_error("Secondary core wake skipped: boot handoff is unavailable");
        return;
    }

    if (!handoff->secondary_program_boot_entry_fn || !handoff->secondary_publish_boot_entries_fn) {
        log_error("Secondary core wake skipped: loader did not publish SMP wake helpers");
        return;
    }

    handoff->secondary_entry_point = (ULong)secondary_start;
    program_boot_entry = (secondary_program_boot_entry_fn_t)handoff->secondary_program_boot_entry_fn;
    publish_boot_entries = (secondary_publish_boot_entries_fn_t)handoff->secondary_publish_boot_entries_fn;

    // Fill entries for CPU indices 1..3. Index 0 is primary and already running.
    for (cpu = 1; cpu < 4; ++cpu) {
        _trace("Setting secondary release target for CPU %u\n", cpu);
        program_boot_entry(cpu); // set this CPU's low-memory boot entry
    }

    // Publish spin-table writes and wake the parked secondary cores.
    publish_boot_entries(); // ensure visibility, then send SEV
}


/**
 * secondary_start
 *
 * Minimal entry executed on each secondary core after being woken. It
 * performs only a few safe steps:
 *  - identify the CPU index
 *  - log a small message for debugging
 *  - park the core in a low-power wait-for-event loop
 *
 * The function intentionally avoids entering the shared scheduler for now.
 * The next SMP step is true per-CPU runqueues; until then each secondary
 * owns only its CPU-local idle task and stays available for future work.
 */
void secondary_start(void) {
    unsigned long mpidr;
    unsigned int cpu;

    // Read MPIDR to obtain the CPU index (low byte).
    asm volatile("mrs %0, mpidr_el1" : "=r" (mpidr));
    cpu = (unsigned int)(mpidr & 0xFF);

    if (!percpu_multi_cpu_enabled()) {
        while (1) {
            asm volatile("wfe\n");
        }
    }

    // Small trace to indicate the secondary core has started.
    log_info("Secondary core %u: started\n", cpu);
    _trace("Secondary core %u: started\n", cpu);

    // Prepare per-CPU minimal state (stack and idle task) so the scheduler
    // can run on this core. This sets up a kernel stack and inserts a
    // per-CPU `KERNEL IDLE` task visible to the global scheduler table.
    percpu_init_secondary(cpu);

    // Install VBAR_EL1 on this CPU before any EL0 task can issue syscalls or
    // trigger faults here. CPU0 already does this in board bring-up, but the
    // vector base is a per-core register so secondaries must program it too.
    irq_vector_init();

    // Publish this CPU as ready only after its idle context exists.
    percpu_mark_cpu_online(cpu);

    // Bind this CPU's current task slot to its private idle task.
    current_task = percpu_idle_task(cpu);
    current_task_ids[cpu] = current_task ? (int)current_task->id : -1;
    current_task->counter = current_task->priority;

    /*
     * Do not let secondaries enter the shared schedproc dispatcher yet.
     *
     * The current schedproc bridge still keeps one global:
     * - scheduler context,
     * - current-thread handle,
     * - ready queue set.
     *
     * Letting multiple CPUs call `schedler_schedule()` races those structures
     * and produces exactly the observed "unbound thread handle" warning and
     * subsequent boot hang. For now the secondary CPUs stay alive, handle local
     * interrupts, and wait in WFE until the scheduler is upgraded to true
     * per-CPU dispatch state.
     */
    enable_irq();
    while (1) {
        asm volatile("wfe\n");
    }
}
