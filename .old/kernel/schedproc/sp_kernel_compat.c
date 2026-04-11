/**
 * sp_kernel_compat.c
 *
 * Kernel compatibility layer that bridges the legacy kernel task/process model to
 * the new schedproc scheduler/process-manager abstraction.
 *
 * This file owns the one-time bootstrap handoff and the ongoing binding table that
 * connects legacy kernel objects to their new schedproc handles. The rest of the
 * kernel still consumes:
 * - `Task *` for architecture context switching,
 * - `Process *` for loader and shell metadata,
 * - global `tasks[]` / `processes[]` tables for lookups.
 */

#define LOG_ENABLE_TRACE 0

#include "arch/cortex-a53/dbg.h"
#include "arch/cortex-a53/mmu.h"
#include "device.h"
#include "input.h"
#include "log.h"
#include "memory.h"
#include "module.h"
#include "my-loader/ldr_kernel.h"
#include "percpu.h"
#include "printf.h"
#include "sp_api.h"
#include "sp_process.h"
#include "sp_scheduler.h"
#include "sp_thread.h"
#include "task.h"
#include "utils.h"

 /*
  * start_thread_context
  *
  * Assembly trampoline that:
  * - runs `schedule_tail` in the freshly scheduled task,
  * - invokes a kernel-thread entry point when `x19 != 0`,
  * - or returns to EL0 when the task is now a user thread.
  *
  * The compatibility layer still uses this trampoline because the kernel task
  * ABI and `Task.cpu_context` layout remain architecture-owned.
  */
extern unsigned long start_thread_context(void);

/*
 * Keep the bootstrap task in page-sized storage because `task_pt_regs()`
 * derives the trap frame location from the task's dedicated stack page.
 */
typedef union BootstrapTaskPageStruct {
    struct task_struct task;
    unsigned char      page[THREAD_SIZE];
} BOOTSTRAP_TASK_PAGE;

/*
 * One binding entry joins the legacy kernel-visible task/process objects to
 * the new scheduler/process-manager handles.
 *
 * The rest of the kernel still consumes:
 * - `Task *` for architecture context switching,
 * - `Process *` for loader and shell metadata,
 * - global `tasks[]` / `processes[]` tables for lookups.
 *
 * The schedproc module owns:
 * - runnable/sleep/current queue state,
 * - scheduler tick bookkeeping,
 * - abstract process/thread state transitions.
 *
 * This table keeps both worlds connected without scattering bridge code across
 * unrelated kernel subsystems.
 *
 * Example:
 * - `tasks[5]` is what old kernel code can see.
 * - `g_schedproc_bindings[5].threadHandle` is the new schedproc thread handle.
 * - this table says those two things are really the same running thread.
 */
typedef struct KernelSchedBindingStruct {
    PSP_THREAD threadHandle;
    PSP_PROCESS processHandle;
} KERNEL_SCHED_BINDING;

#define TASK_DUMP_SEPARATOR "\x1b[2;37m-------------------------------------------------------------------------------\x1b[0m\n"

static KERNEL_SCHED_BINDING* schedproc_binding_for_task(const Task* task);
static void schedproc_sync_task_state(Task* task);

static Process init_process = { INIT_PROCESS };
static BOOTSTRAP_TASK_PAGE init_task_page __attribute__((aligned(THREAD_SIZE))) = {
    .task = INIT_TASK(&init_process)
};

#define init_task (init_task_page.task)

struct task_struct* current_tasks[NR_CPUS] = {
    &init_task,
};
int current_task_ids[NR_CPUS] = {
    0,
};
struct task_struct* tasks[NR_TASKS] = {
    &init_task,
};
Process* processes[NR_PROCESSES] = {
    &init_process,
};
int nr_tasks = 1;
int nr_processes = 1;

/*
 * Global schedproc runtime used by the current uniprocessor kernel.
 *
 * Important fields:
 *   g_schedproc_context: Active scheduler/process-manager context.
 *   g_schedproc_ticks: Kernel-visible monotonic scheduler tick count.
 *   g_schedproc_bindings: Slot-indexed bridge from legacy ids to `SP_*` handles.
 *   g_scheduler_spinlock: Very small scheduler lock used around shared task tables.
 *   g_schedproc_initialized: Guards one-time bootstrap.
 */
static PSP_CONTEXT g_schedproc_context;
static unsigned long g_schedproc_ticks;
static KERNEL_SCHED_BINDING g_schedproc_bindings[NR_TASKS];
static long g_schedproc_exit_status[NR_PROCESSES];
static long g_schedproc_exit_parent[NR_PROCESSES];
static Bool g_schedproc_exit_status_valid[NR_PROCESSES];
static volatile unsigned int g_scheduler_spinlock;
static Bool g_schedproc_initialized;
static Task* g_schedproc_housekeeper_task;
static Bool g_schedproc_housekeeper_started;

static void schedproc_clear_exit_status(long pid) {
    if (pid < 0 || pid >= NR_PROCESSES) {
        return;
    }

    g_schedproc_exit_status[pid] = 0;
    g_schedproc_exit_parent[pid] = -1;
    g_schedproc_exit_status_valid[pid] = false;
}

/*
 * Record one child exit result and wake a parent that is blocked in
 * `process_wait()` for that exact child.
 *
 * This turns child waiting into an event-driven handoff instead of a polling
 * loop driven by timer wakeups.
 */
static void schedproc_publish_exit_status(long pid, long parent_pid, long result) {
    Task* parentTask = 0;
    KERNEL_SCHED_BINDING* parentBinding = 0;

    if (pid < 0 || pid >= NR_PROCESSES) {
        return;
    }

    g_schedproc_exit_status[pid] = result;
    g_schedproc_exit_parent[pid] = parent_pid;
    g_schedproc_exit_status_valid[pid] = true;

    if (parent_pid <= 0 || parent_pid >= NR_PROCESSES) {
        return;
    }

    parentTask = process_main_thread(parent_pid);
    if (!parentTask || parentTask->state != TASK_BLOCKED) {
        return;
    }

    parentBinding = schedproc_binding_for_task(parentTask);
    if (!parentBinding || !parentBinding->threadHandle) {
        return;
    }

    if (sp_unblock_thread(g_schedproc_context, parentBinding->threadHandle, true) == SP_OK) {
        schedproc_sync_task_state(parentTask);
        percpu_kick_cpu(parentTask->cpu_affinity);
    }
}

static Bool schedproc_try_consume_exit_status(long pid, long parent_pid, long* result) {
    if (pid < 0 || pid >= NR_PROCESSES || !result) {
        return false;
    }
    if (!g_schedproc_exit_status_valid[pid]) {
        return false;
    }
    if (g_schedproc_exit_parent[pid] != parent_pid) {
        return false;
    }

    *result = g_schedproc_exit_status[pid];
    schedproc_clear_exit_status(pid);
    return true;
}

/*
 * schedproc_housekeeper_main
 *
 * Dedicated kernel-thread reaper used as the safe landing point for user
 * `sys_exit()` handoff. Unlike the bootstrap `init_task`, this thread has its
 * own task storage, stack, and schedproc binding, so it can safely:
 * - reclaim zombie processes,
 * - run module idle work,
 * - and yield back into the scheduler.
 */
static void schedproc_housekeeper_main(Pointer arg) {
    (void)arg;

    while (1) {
        device_poll_touch();
        input_poll();
        cleanup_zombie_processes();
        module_run_idle_loops();
        schedler_schedule();

        if (current_task == g_schedproc_housekeeper_task) {
#if defined(ROS_BOARD_VIRT)
            asm volatile("yield\n" ::: "memory");
#else
            asm volatile("wfe\n");
#endif
        }
    }
}

void schedproc_start_housekeeper(void) {
    Process* process;
    Task* task;
    long pid;

    init_schedler();

    if (g_schedproc_housekeeper_started) {
        return;
    }

    pid = (long)process_create_main_thread(PF_KTHREAD, (Address)&schedproc_housekeeper_main, 0);
    if (pid < 0) {
        log_warning("Unable to start schedproc housekeeper thread");
        return;
    }

    task = process_main_thread(pid);
    process = process_lookup(pid);
    if (!task || !process) {
        log_warning("Housekeeper task bookkeeping missing for pid %ld", pid);
        return;
    }

    strncpy(process->name, "sched-reaper", sizeof(process->name) - 1);
    process->name[sizeof(process->name) - 1] = '\0';
    strncpy(task->thread_name, "sched-reaper", sizeof(task->thread_name) - 1);
    task->thread_name[sizeof(task->thread_name) - 1] = '\0';
    task->name = (Buffer)task->thread_name;
    task->priority = PRIORITY_NORMAL;
    g_schedproc_housekeeper_task = task;
    g_schedproc_housekeeper_started = true;
}

/*
 * schedproc_previous_exception_mode
 *
 * During exception handling, `SPSR_EL1` captures the mode of the interrupted
 * context. The timer IRQ path runs through the same C helper for both:
 * - EL0 interrupts, where it is safe to switch to another user task because
 *   the destination task already owns a valid saved `pt_regs` frame, and
 * - EL1 interrupts, where the active kernel stack contains the interrupted
 *   kernel return frame and must stay paired with the current task until the
 *   handler returns.
 *
 * Returning the low mode bits lets the compatibility scheduler distinguish
 * those cases without spreading raw system-register reads across the file.
 */
static unsigned long schedproc_previous_exception_mode(void) {
    unsigned long spsr;

    asm volatile("mrs %0, spsr_el1" : "=r"(spsr));
    return (spsr & 0xFUL);
}

/*
 * schedproc_kernel_pgd_template
 *
 * Fresh kernel threads must not inherit the caller's active user address
 * space. If they did, a later `exec_user_program()` in that child would reset
 * and rebuild the very same page tables that the parent task is still using.
 *
 * Instead, bootstrap kernel threads start from the stable kernel-only TTBR0
 * template captured on the init task after paging comes up. Once a bootstrap
 * thread commits a real user image, `process_map_page()` allocates task-private
 * page tables as needed.
 */
static Address schedproc_kernel_pgd_template(void) {
    if (init_task.mm.pgd != 0) {
        return init_task.mm.pgd;
    }

    return get_pgd();
}

/*
 * sp_kernel_heap_alloc / sp_kernel_heap_free
 *
 * Heap callbacks supplied to the schedproc module so it can allocate its
 * internal context, process boxes, and thread descriptors from the kernel heap.
 */
static void* sp_kernel_heap_alloc(size_t size) {
    return (void*)kmalloc((int)(size ? size : 1u));
}

static void sp_kernel_heap_free(void* memory) {
    if (memory) {
        kfree((Address)memory);
    }
}

/*
 * sp_kernel_get_tick_count
 *
 * Expose the legacy kernel tick counter to the schedproc module. The module
 * also keeps its own `nowTick`, but this callback lets future extensions query
 * the kernel's visible timebase without reaching back into scheduler globals.
 */
static uint64_t sp_kernel_get_tick_count(void) {
    return (uint64_t)g_schedproc_ticks;
}

/*
 * sp_kernel_log_line
 *
 * Lightweight adapter used by schedproc when it wants to emit a preformatted
 * line. The current module core rarely calls this, but wiring it now keeps the
 * integration contract complete.
 */
static void sp_kernel_log_line(int level, const char* message) {
    (void)level;

    if (message && message[0] != '\0') {
        log_info("%s", message);
    }
}

/*
 * schedproc_priority_from_task
 *
 * Translate the kernel's small legacy priority range into the schedproc
 * scheduler's "smaller number means higher priority" convention.
 */
static uint8_t schedproc_priority_from_task(long legacyPriority) {
    long clamped = legacyPriority;

    if (clamped < PRIORITY_NORMAL) {
        clamped = PRIORITY_NORMAL;
    }
    if (clamped > PRIORITY_REAL_TIME) {
        clamped = PRIORITY_REAL_TIME;
    }

    return (uint8_t)(PRIORITY_REAL_TIME - clamped);
}

/*
 * schedproc_quantum_from_task
 *
 * Preserve the kernel's historical intuition that a stronger priority usually
 * receives a slightly larger execution budget per dispatch.
 */
static uint32_t schedproc_quantum_from_task(const Task* task) {
    long legacyPriority = task ? task->priority : PRIORITY_NORMAL;

    if (legacyPriority < 1) {
        legacyPriority = 1;
    }

    return (uint32_t)legacyPriority;
}

/*
 * schedproc_idle_priority / schedproc_idle_quantum
 *
 * The bootstrap `init_task` is the kernel's idle / housekeeping loop, not a
 * normal round-robin peer for user programs. If it shares the same priority as
 * ordinary tasks, schedproc will legitimately rotate into task 0 on quantum
 * expiry, and that extra EL0 -> idle -> EL0 bounce is exactly where the
 * current compatibility bridge is still fragile.
 *
 * Give task 0 the lowest schedproc priority and the smallest quantum so it
 * only runs when no real work is ready, while keeping the legacy `Task`
 * priority fields unchanged for the rest of the kernel.
 */
static uint8_t schedproc_idle_priority(void) {
    return (uint8_t)SP_MAX_PRIORITY;
}

static uint32_t schedproc_idle_quantum(void) {
    return 1u;
}

/*
 * Choose the initial CPU ownership for a newly created task.
 *
 * Secondary-core wake-up is available behind USE_MULTI_CPU, but the schedproc
 * core still owns only one global current-thread slot and one shared ready
 * queue set. Until that scheduler state becomes truly per-CPU, all runnable
 * work must remain owned by the calling CPU so CPU0 is the only dispatcher.
 */
static unsigned int schedproc_select_initial_cpu_affinity(void) {
    return task_cpu_index();
}

/*
 * scheduler_spin_try_acquire / scheduler_spin_release
 *
 * Tiny AArch64 spin lock primitives used by the compatibility layer when it
 * mutates the shared `tasks[]`, `processes[]`, and binding tables.
 */
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

/*
 * process_allocate_thread_slot
 *
 * Reuse the historical rule that slot 0 belongs to the bootstrap kernel task,
 * so newly created tasks start from index 1.
 */
static int process_allocate_thread_slot(void) {
    for (int index = 1; index < NR_TASKS; index++) {
        if (!tasks[index]) {
            return index;
        }
    }

    return -1;
}

/*
 * process_allocate_box
 *
 * Allocate one kernel-visible `Process` descriptor that remains the rest of
 * the kernel's source of truth for names, parent pid, launch args, and shell
 * introspection.
 */
static Process* process_allocate_box(long pid, Process* parent) {
    Process* process = (Process*)kmalloc(sizeof(Process));

    if (!process) {
        return 0;
    }

    memzero((Address)process, sizeof(*process));
    schedproc_clear_exit_status(pid);
    process->id = pid;
    process->parent_process_id = parent ? parent->id : 0;
    process->state = PROCESS_ACTIVE;
    process->result = 0;
    process->main_thread_id = pid;
    return process;
}

/*
 * process_attach_thread
 *
 * Attach one kernel `Task` to one kernel-visible `Process`. This is separate
 * from the schedproc module because the loader, shell, and syscall code still
 * inspect `Task.process`, `Process.thread_count`, and process metadata fields
 * directly.
 */
static void process_attach_thread(Task* task, Process* process) {
    if (!task) {
        return;
    }

    task->process = process;
    if (!task->thread_name[0]) {
        task->name = (Buffer)(process && process->name[0] ? process->name : "");
    }

    if (process) {
        process->thread_count++;
        if (!process->main_thread) {
            process->main_thread = task;
            process->main_thread_id = task->id;
        }
    }
}

/*
 * schedproc_binding_for_task / schedproc_binding_for_process
 *
 * Fast slot-indexed accessors into the bridge table.
 */
static KERNEL_SCHED_BINDING* schedproc_binding_for_task(const Task* task) {
    if (!task || task->id < 0 || task->id >= NR_TASKS) {
        return 0;
    }

    return &g_schedproc_bindings[task->id];
}

static PSP_PROCESS schedproc_binding_for_process(const Process* process) {
    if (!process || process->id < 0 || process->id >= NR_PROCESSES) {
        return 0;
    }

    return g_schedproc_bindings[process->id].processHandle;
}

/*
 * schedproc_find_task_by_thread
 *
 * Resolve the legacy `Task *` that owns one schedproc thread handle. The scan
 * is cheap because the kernel supports at most `NR_TASKS` slots.
 */
static Task* schedproc_find_task_by_thread(PSP_THREAD threadHandle) {
    if (!threadHandle) {
        return 0;
    }

    for (int index = 0; index < NR_TASKS; index++) {
        if (tasks[index] && g_schedproc_bindings[index].threadHandle == threadHandle) {
            return tasks[index];
        }
    }

    return 0;
}

/*
 * schedproc_map_thread_state
 *
 * Translate schedproc thread states into the historical `TaskState` enum so
 * shell output and legacy code paths keep seeing familiar state names.
 */
static TaskState schedproc_map_thread_state(SP_THREAD_STATE state) {
    switch (state) {
    case SP_THREAD_RUNNABLE:
    case SP_THREAD_CREATED:
    case SP_THREAD_SUSPENDED:
        return TASK_READY;
    case SP_THREAD_RUNNING:
        return TASK_RUNNING;
    case SP_THREAD_SLEEPING:
        return TASK_SLEEPING;
    case SP_THREAD_BLOCKED:
        return TASK_BLOCKED;
    case SP_THREAD_DYING:
    case SP_THREAD_DEAD:
    default:
        return TASK_ZOMBIE;
    }
}

/*
 * schedproc_sync_task_state
 *
 * Copy fresh scheduler facts back into the old `Task` box.
 *
 * Kid version: schedproc is the new engine, but lots of kernel code still
 * looks at the old dashboard. This function updates the dashboard so old code
 * reads the same story the new engine already knows.
 *
 * Important fields mirrored here:
 *   task->state: Human-readable state used throughout the kernel and shell.
 *   task->counter: Remaining timeslice budget exposed by legacy diagnostics.
 *   task->wakeup_tick: Wake deadline used by shell/task reporting.
 *   task->name: Process fallback name when the thread label is empty.
 */
static void schedproc_sync_task_state(Task* task) {
    KERNEL_SCHED_BINDING* binding;
    SP_THREADINFO info;

    if (!task) {
        return;
    }

    binding = schedproc_binding_for_task(task);
    if (!binding || !binding->threadHandle) {
        return;
    }
    if (sp_query_thread(binding->threadHandle, &info) != SP_OK) {
        return;
    }

    task->state = schedproc_map_thread_state(info.state);
    task->counter = (long)info.quantumLeft;
    task->wakeup_tick = (unsigned long)info.wakeTick;
    if (task->thread_name[0] == '\0' && task->process && task->process->name[0] != '\0') {
        task->name = (Buffer)task->process->name;
    }
}

/*
 * schedproc_sync_process_state
 *
 * Mirror the abstract schedproc process state back into the legacy `Process`
 * box so shell commands and loader diagnostics can keep reading the old fields.
 */
static void schedproc_sync_process_state(Process* process) {
    PSP_PROCESS processHandle;
    SP_PROCESSINFO info;

    if (!process) {
        return;
    }

    processHandle = schedproc_binding_for_process(process);
    if (!processHandle) {
        return;
    }
    if (sp_query_process(processHandle, &info) != SP_OK) {
        return;
    }

    process->state = (info.state == SP_PROCESS_DEAD || info.state == SP_PROCESS_EXITING) ? PROCESS_ZOMBIE : PROCESS_ACTIVE;
}

/*
 * schedproc_sync_all_visible_state
 *
 * Refresh every kernel-visible task/process descriptor from the schedproc
 * module before shell code or architecture code observes the tables.
 */
static void schedproc_sync_all_visible_state(void) {
    for (int index = 0; index < NR_TASKS; index++) {
        if (tasks[index]) {
            schedproc_sync_task_state(tasks[index]);
        }
    }

    for (int index = 0; index < NR_PROCESSES; index++) {
        if (processes[index]) {
            schedproc_sync_process_state(processes[index]);
        }
    }
}

/*
 * schedproc_resolve_next_task
 *
 * Ask the schedproc module which thread should run next, then map that thread
 * back into the legacy `Task *` consumed by the architecture switch path.
 */
static Task* schedproc_resolve_next_task(void) {
    PSP_THREAD threadHandle = 0;
    SP_RESULT  result;
    Task* task;

    if (!g_schedproc_context) {
        return current_task;
    }

    result = sp_dispatch(g_schedproc_context, &threadHandle);
    if (result == SP_E_EMPTY || !threadHandle) {
        return percpu_idle_task(task_cpu_index());
    }
    if (result != SP_OK) {
        log_warning("schedproc dispatch failed: result=%d", (int)result);
        return current_task ? current_task : percpu_idle_task(task_cpu_index());
    }

    task = schedproc_find_task_by_thread(threadHandle);
    if (!task) {
        log_warning("schedproc dispatch returned an unbound thread handle");
        return percpu_idle_task(task_cpu_index());
    }

    return task;
}

/*
 * schedproc_select_exit_successor
 *
 * When a user task exits immediately after spawning another runnable task, the
 * generic dispatch order tends to bounce through the bootstrap kernel task
 * first because that task has been sitting in the ready queue longer. In the
 * current compatibility bridge that extra round trip is fragile, so prefer a
 * non-bootstrap runnable task when one is already ready at exit time.
 *
 * This is intentionally conservative:
 * - start from the normal schedproc dispatch decision,
 * - only intervene when it picked task 0,
 * - and only rotate task 0 behind peers when another live task exists.
 */
static Task* schedproc_select_exit_successor(Task* exitingTask) {
    Task* nextTask = schedproc_resolve_next_task();
    Task* idleTask = percpu_idle_task(task_cpu_index());
    Task* housekeeper = g_schedproc_housekeeper_task;

    if (housekeeper == exitingTask || (housekeeper && housekeeper->state == TASK_ZOMBIE)) {
        housekeeper = 0;
    }

    /*
     * Leaving a user task through `sys_exit()` happens while the CPU is still
     * inside that task's EL0 SVC frame. Switching directly from that frame into
     * another already-running EL0 task is unsafe here because the return path
     * expects a matching exception frame on the destination stack.
     *
     * Kernel-thread successors are safe because they resume through the normal
     * `cpu_switch_to` / `start_thread_context` path. User-thread successors are
     * deferred through the bootstrap kernel task so the main scheduler loop can
     * later re-enter userspace from a clean kernel context.
     */
    if (nextTask && !(nextTask->flags & PF_KTHREAD)) {
        return housekeeper ? housekeeper : (idleTask ? idleTask : &init_task);
    }

    /*
     * The historical bootstrap `init_task` still resumes on the boot stack
     * saved from `kernel_main()`. That path is stable for the normal
     * kernel-main scheduling loop, but it remains fragile as an immediate
     * successor for `sys_exit()`.
     *
     * Route user-task exits through the per-CPU idle trampoline instead. That
     * thread always starts from a clean `start_thread_context` frame and can
     * safely re-enter the scheduler before any zombie reclamation runs.
     */
    if (nextTask == &init_task && idleTask) {
        return housekeeper ? housekeeper : idleTask;
    }

    if (nextTask == idleTask && housekeeper) {
        return housekeeper;
    }

    if (nextTask != &init_task || !g_schedproc_context) {
        return nextTask;
    }

    for (int index = 1; index < NR_TASKS; index++) {
        KERNEL_SCHED_BINDING* binding;
        Task* candidate = tasks[index];

        if (!candidate || candidate == exitingTask || candidate->state == TASK_ZOMBIE) {
            continue;
        }

        binding = schedproc_binding_for_task(candidate);
        if (!binding || !binding->threadHandle) {
            continue;
        }

        if (sp_yield_current(g_schedproc_context) == SP_OK) {
            Task* preferred = schedproc_resolve_next_task();

            if (preferred && preferred != &init_task && (preferred->flags & PF_KTHREAD)) {
                return preferred;
            }
        }
        break;
    }

    return nextTask;
}

static Task* schedproc_select_sleep_successor(Task* sleepingTask) {
    Task* nextTask = schedproc_resolve_next_task();
    Task* idleTask = percpu_idle_task(task_cpu_index());
    Task* housekeeper = g_schedproc_housekeeper_task;

    if (housekeeper == sleepingTask || (housekeeper && housekeeper->state == TASK_ZOMBIE)) {
        housekeeper = 0;
    }

    /*
     * A sleep syscall still returns through the caller's EL0 SVC frame. If we
     * switch directly into another user task here, the return path consumes the
     * wrong exception frame and userspace handoff stalls. Route the immediate
     * successor through a kernel-thread landing point first, then let that
     * kernel context re-enter the scheduler normally.
     */
    if (sleepingTask && !(sleepingTask->flags & PF_KTHREAD) && nextTask && !(nextTask->flags & PF_KTHREAD)) {
        return housekeeper ? housekeeper : (idleTask ? idleTask : &init_task);
    }

    if (sleepingTask && !(sleepingTask->flags & PF_KTHREAD) && nextTask == &init_task && idleTask) {
        return housekeeper ? housekeeper : idleTask;
    }

    if (sleepingTask && !(sleepingTask->flags & PF_KTHREAD) && nextTask == idleTask && housekeeper) {
        return housekeeper;
    }

    return nextTask;
}

/*
 * schedproc_bind_kernel_entities
 *
 * Create the schedproc-side process/thread objects that correspond to one new
 * kernel-visible process and its main thread.
 */
static int schedproc_bind_kernel_entities(Process* process, Task* task, const char* threadName) {
    SP_RESULT  result;
    PSP_PROCESS processHandle;
    PSP_THREAD  threadHandle;
    uint8_t     priority;
    uint32_t    quantum;
    Bool        enqueueAtTail = false;

    if (!g_schedproc_context || !process || !task) {
        return -1;
    }

    result = sp_create_process(g_schedproc_context, process->name, &processHandle);
    if (result != SP_OK) {
        return -1;
    }

    if (threadName && strcmp(threadName, "sched-reaper") == 0) {
        priority = schedproc_idle_priority();
        quantum = schedproc_idle_quantum();
        enqueueAtTail = true;
    }
    else {
        priority = schedproc_priority_from_task(task->priority);
        quantum = schedproc_quantum_from_task(task);
    }
    result = sp_create_thread(g_schedproc_context, processHandle, threadName ? threadName : "", priority, quantum, &threadHandle);
    if (result != SP_OK) {
        (void)sp_destroy_process(g_schedproc_context, processHandle);
        return -1;
    }

    if (sp_resume_thread(g_schedproc_context, threadHandle) != SP_OK ||
        /*
         * Place a freshly created thread at the head of its priority queue so
         * exec/bootstrap work can run immediately after the current task yields
         * or exits, instead of first bouncing through the already-runnable init
         * task. That keeps spawn->exit handoff simple and avoids an extra
         * kernel-task round trip on the current single-CPU scheduler bridge.
         */
        sp_make_thread_runnable(g_schedproc_context, threadHandle, enqueueAtTail) != SP_OK) {
        (void)sp_mark_thread_dead(g_schedproc_context, threadHandle);
        (void)sp_destroy_thread(g_schedproc_context, threadHandle);
        (void)sp_destroy_process(g_schedproc_context, processHandle);
        return -1;
    }

    g_schedproc_bindings[task->id].threadHandle = threadHandle;
    g_schedproc_bindings[process->id].processHandle = processHandle;
    return 0;
}

/*
 * schedproc_bind_kernel_thread
 *
 * Create the schedproc-side thread object for an additional thread that shares
 * an existing kernel-visible process box.
 */
static int schedproc_bind_kernel_thread(Process* process, Task* task, const char* threadName) {
    PSP_PROCESS processHandle;
    PSP_THREAD  threadHandle;
    SP_RESULT   result;

    if (!g_schedproc_context || !process || !task) {
        return -1;
    }

    processHandle = schedproc_binding_for_process(process);
    if (!processHandle) {
        return -1;
    }

    result = sp_create_thread(
        g_schedproc_context,
        processHandle,
        threadName ? threadName : "",
        schedproc_priority_from_task(task->priority),
        schedproc_quantum_from_task(task),
        &threadHandle);
    if (result != SP_OK) {
        return -1;
    }

    if (sp_resume_thread(g_schedproc_context, threadHandle) != SP_OK ||
        sp_make_thread_runnable(g_schedproc_context, threadHandle, false) != SP_OK) {
        (void)sp_mark_thread_dead(g_schedproc_context, threadHandle);
        (void)sp_destroy_thread(g_schedproc_context, threadHandle);
        return -1;
    }

    g_schedproc_bindings[task->id].threadHandle = threadHandle;
    return 0;
}

/*
 * process_track_kernel_page / page normalization helpers
 *
 * These helpers preserve the deleted process-manager behaviour that records
 * every page allocated on behalf of a task so later cleanup can reclaim:
 * - the task storage page,
 * - the kernel stack page,
 * - task-private page-table pages,
 * - and user-mapped pages.
 */
static int process_track_kernel_page(Task* task, Address page) {
    if (!task || task->mm.kernel_pages_count >= MAX_PROCESS_PAGES) {
        return -1;
    }

    for (int index = 0; index < task->mm.kernel_pages_count; index++) {
        if ((task->mm.kernel_pages[index] & MM_PAGE_MASK) == (page & MM_PAGE_MASK)) {
            return 0;
        }
    }

    task->mm.kernel_pages[task->mm.kernel_pages_count++] = page;
    return 0;
}

static Address process_normalize_page_address(Address page) {
    Address normalized = (Address)(page & MM_PAGE_MASK);

    if (normalized == 0) {
        return 0;
    }
    if (mem_is_kernel_virt_addr((VirtAddr)normalized)) {
        PhysAddr phys = mem_virt_to_phys((VirtAddr)normalized);

        if (mem_is_valid_phys_page(phys)) {
            return (Address)phys;
        }
    }
    if (!mem_is_valid_phys_page((PhysAddr)normalized)) {
        return 0;
    }

    return normalized;
}

static Address process_task_kernel_page(Task* task) {
    if (!task || task->mm.kernel_pages_count <= 0) {
        return 0;
    }

    return process_normalize_page_address(task->mm.kernel_pages[0]);
}

static Address process_task_stack_page(Task* task) {
    if (!task) {
        return 0;
    }

    return process_normalize_page_address(task->kernel_stack_page);
}

static int process_permanent_kernel_pages(Task* task) {
    int count = 0;

    if (process_task_kernel_page(task) != 0) {
        count = 1;
    }
    if (process_task_stack_page(task) != 0) {
        count = 2;
    }

    return count;
}

static int process_clamp_page_count(const char* label, long taskId, int count) {
    if (count < 0) {
        log_warning("Task %ld has negative %s count %d; clamping to 0", taskId, label, count);
        return 0;
    }
    if (count > MAX_PROCESS_PAGES) {
        log_warning("Task %ld has corrupt %s count %d; clamping to %d", taskId, label, count, MAX_PROCESS_PAGES);
        return MAX_PROCESS_PAGES;
    }

    return count;
}

/*
 * schedproc_mark_task_zombie
 *
 * Convert one kernel task into a dead schedproc thread and, when requested,
 * mark every sibling thread in the same process dead as well. This keeps the
 * new module and the legacy kernel tables aligned during process exit.
 */
static void schedproc_mark_task_zombie(Task* task, Bool includeSiblings) {
    Process* owner;

    if (!task) {
        return;
    }

    owner = task->process;
    task->state = TASK_ZOMBIE;

    if (schedproc_binding_for_task(task) && schedproc_binding_for_task(task)->threadHandle) {
        (void)sp_mark_thread_dead(g_schedproc_context, schedproc_binding_for_task(task)->threadHandle);
    }

    if (!owner) {
        return;
    }

    if (includeSiblings) {
        PSP_PROCESS processHandle = schedproc_binding_for_process(owner);

        owner->state = PROCESS_ZOMBIE;
        if (processHandle) {
            (void)sp_begin_process_exit(g_schedproc_context, processHandle);
        }

        for (int index = 0; index < NR_TASKS; index++) {
            if (!tasks[index] || tasks[index] == task || tasks[index]->process != owner) {
                continue;
            }
            tasks[index]->state = TASK_ZOMBIE;
            if (schedproc_binding_for_task(tasks[index]) && schedproc_binding_for_task(tasks[index])->threadHandle) {
                (void)sp_mark_thread_dead(g_schedproc_context, schedproc_binding_for_task(tasks[index])->threadHandle);
            }
        }
    }
    else if (owner->thread_count <= 1) {
        owner->state = PROCESS_ZOMBIE;
    }
}

/*
 * schedproc_reassign_main_thread
 *
 * Pick another live thread in the same process after the previous main thread
 * has been reaped, so shell/process inspection continues to point at a valid
 * thread when the process still has survivors.
 */
static void schedproc_reassign_main_thread(Process* process, Task* oldTask) {
    if (!process || process->main_thread != oldTask) {
        return;
    }

    process->main_thread = 0;
    process->main_thread_id = -1;
    for (int index = 0; index < NR_TASKS; index++) {
        if (tasks[index] && tasks[index] != oldTask && tasks[index]->process == process && tasks[index]->state != TASK_ZOMBIE) {
            process->main_thread = tasks[index];
            process->main_thread_id = tasks[index]->id;
            return;
        }
    }
}

/*
 * init_schedler
 *
 * One-time bridge bootstrap. This creates the schedproc context, wires kernel
 * callbacks into it, and registers the bootstrap kernel task/process as the
 * initial runnable thread.
 */
void init_schedler(void) {
    SP_KERNELAPI api;
    PSP_PROCESS   processHandle;
    PSP_THREAD    threadHandle;

    if (g_schedproc_initialized) {
        return;
    }

    memzero((Address)&api, sizeof(api));
    api.heapAlloc = sp_kernel_heap_alloc;
    api.heapFree = sp_kernel_heap_free;
    api.getTickCount = sp_kernel_get_tick_count;
    api.logLine = sp_kernel_log_line;

    if (sp_init(&api, &g_schedproc_context) != SP_OK) {
        log_error("Unable to initialize schedproc context");
        return;
    }

    if (sp_create_process(g_schedproc_context, init_process.name, &processHandle) != SP_OK ||
        sp_create_thread(
            g_schedproc_context,
            processHandle,
            "bootstrap",
            schedproc_idle_priority(),
            schedproc_idle_quantum(),
            &threadHandle) != SP_OK ||
        sp_resume_thread(g_schedproc_context, threadHandle) != SP_OK ||
        sp_make_thread_runnable(g_schedproc_context, threadHandle, true) != SP_OK ||
        sp_dispatch(g_schedproc_context, 0) != SP_OK) {
        log_error("Unable to bootstrap schedproc kernel task");
        return;
    }

    init_process.main_thread = &init_task;
    init_process.main_thread_id = 0;
    init_process.thread_count = 1;
    init_task.process = &init_process;
    init_task.name = (Buffer)"KERNEL IDLE";

    g_schedproc_bindings[0].threadHandle = threadHandle;
    g_schedproc_bindings[0].processHandle = processHandle;
    current_tasks[0] = &init_task;
    current_task_ids[0] = 0;
    g_schedproc_initialized = true;
}

/*
 * process_lookup / process_main_thread
 *
 * Keep the historical global table lookup ABI intact.
 */
Process* process_lookup(long pid) {
    if (pid < 0 || pid >= NR_PROCESSES) {
        return 0;
    }

    return processes[pid];
}

Task* process_main_thread(long pid) {
    Process* process = process_lookup(pid);

    return process ? process->main_thread : 0;
}

/*
 * preempt_disable / preempt_enable
 *
 * Preserve the old nesting-count behaviour used throughout the kernel. The
 * schedproc module currently runs on one CPU, so this counter still acts as
 * the kernel's local "do not schedule me yet" flag.
 */
void preempt_disable(void) {
    if (current_task) {
        current_task->preempt_count++;
    }
}

void preempt_enable(void) {
    if (current_task && current_task->preempt_count > 0) {
        current_task->preempt_count--;
    }
}

void scheduler_lock_acquire(void) {
    while (!scheduler_spin_try_acquire(&g_scheduler_spinlock)) {
        asm volatile("yield\n");
    }
}

void scheduler_lock_release(void) {
    scheduler_spin_release(&g_scheduler_spinlock);
}

int current_task_should_return_to_user(void) {
    init_schedler();
    return current_task && !(current_task->flags & PF_KTHREAD);
}

/*
 * schedler_switch_to
 *
 * Architecture-facing context-switch entry. The schedproc module decides which
 * thread should run, while this function still owns:
 * - updating the kernel's `current_task` slot,
 * - switching page tables,
 * - and invoking the assembly `cpu_switch_to` helper.
 */
void schedler_switch_to(struct task_struct* next) {
    unsigned int cpu = task_cpu_index();

    if (!next) {
        next = percpu_idle_task(cpu);
    }

    if (current_task == next) {
        current_task_ids[cpu] = current_task ? (int)current_task->id : -1;
        if (current_task && current_task->state == TASK_READY) {
            current_task->state = TASK_RUNNING;
        }
        return;
    }

    {
        struct task_struct* prev = current_task;
        struct pt_regs* nextRegs = 0;

        if (next && !(next->flags & PF_KTHREAD)) {
            nextRegs = task_pt_regs(next);
        }

        _trace(
            "schedler_switch_to: cpu=%u prev=%ld next=%ld prev_pgd=0x%lX next_pgd=0x%lX prev_flags=0x%lX next_flags=0x%lX prev_pc=0x%lX next_pc=0x%lX prev_sp=0x%lX next_sp=0x%lX",
            cpu,
            prev ? prev->id : -1,
            next ? next->id : -1,
            prev ? prev->mm.pgd : 0,
            next ? next->mm.pgd : 0,
            prev ? prev->flags : 0,
            next ? next->flags : 0,
            prev ? prev->cpu_context.pc : 0,
            next ? next->cpu_context.pc : 0,
            prev ? prev->cpu_context.sp : 0,
            next ? next->cpu_context.sp : 0);
        current_task = next;
        current_task_ids[cpu] = next ? (int)next->id : -1;
        set_pgd(next->mm.pgd);
        cpu_switch_to(prev, next);
        _trace(
            "schedler_switch_to: resumed cpu=%u current=%ld pgd=0x%lX flags=0x%lX pc=0x%lX sp=0x%lX",
            cpu,
            current_task ? current_task->id : -1,
            current_task ? current_task->mm.pgd : 0,
            current_task ? current_task->flags : 0,
            current_task ? current_task->cpu_context.pc : 0,
            current_task ? current_task->cpu_context.sp : 0);
    }
}

/*
 * schedler_schedule
 *
 * Main scheduler handoff for the compatibility layer.
 *
 * Kid version:
 * 1. let the current schedproc thread step out of the chair if needed,
 * 2. refresh the old kernel tables so everyone can see up-to-date state,
 * 3. ask schedproc who should sit in the chair next,
 * 4. switch the CPU to that task.
 */
void schedler_schedule(void) {
    Task* nextTask;
    KERNEL_SCHED_BINDING* binding;

    init_schedler();
    dbg_wait_if_paused();

    binding = schedproc_binding_for_task(current_task);
    if (binding && binding->threadHandle && current_task && current_task->state == TASK_RUNNING) {
        (void)sp_yield_current(g_schedproc_context);
    }

    /* First sync: make old kernel tables match the current schedproc picture. */
    schedproc_sync_all_visible_state();
    nextTask = schedproc_resolve_next_task();
    /* Second sync: publish any state changes caused by the dispatch decision itself. */
    schedproc_sync_all_visible_state();

    /*
     * The per-CPU idle trampoline is intentionally outside the schedproc task
     * tables. When schedproc reports only the bootstrap `init_task` as
     * runnable, there is no useful work to switch to here: jumping from the
     * idle trampoline back onto the fragile bootstrap stack recreates the same
     * EL1h return bug we are avoiding in the exit path.
     *
     * Keep executing the current idle loop instead. As soon as a real schedproc
     * thread becomes runnable, `nextTask` will stop pointing at `init_task` and
     * the normal switch path below will run.
     */
    if ((!binding || !binding->threadHandle) && current_task && nextTask == &init_task) {
        if (current_task->state == TASK_READY) {
            current_task->state = TASK_RUNNING;
        }
        return;
    }

    schedler_switch_to(nextTask);
}

unsigned long schedler_get_ticks(void) {
    return g_schedproc_ticks;
}

/*
 * schedler_sleep_ticks
 *
 * Put the current schedproc thread to sleep, mirror the new sleep state back
 * into the kernel-visible `Task`, and immediately dispatch a replacement.
 */
void schedler_sleep_ticks(unsigned long ticks) {
    Task* nextTask;

    init_schedler();

    if (ticks == 0) {
        return;
    }
    if (!current_task || !schedproc_binding_for_task(current_task) || !schedproc_binding_for_task(current_task)->threadHandle) {
        return;
    }

    if (sp_sleep_current(g_schedproc_context, ticks) != SP_OK) {
        return;
    }

    schedproc_sync_all_visible_state();
    nextTask = schedproc_select_sleep_successor(current_task);
    schedler_switch_to(nextTask);
}

/*
 * Block the current task until another scheduler event makes it runnable
 * again, then route the immediate successor through the same safe landing
 * logic used by the sleep path.
 */
void schedler_block_current(void) {
    Task* blockedTask;
    Task* nextTask;

    init_schedler();

    blockedTask = current_task;
    if (!blockedTask || !schedproc_binding_for_task(blockedTask) || !schedproc_binding_for_task(blockedTask)->threadHandle) {
        return;
    }

    if (sp_block_current(g_schedproc_context) != SP_OK) {
        return;
    }

    schedproc_sync_all_visible_state();
    nextTask = schedproc_select_sleep_successor(blockedTask);
    schedler_switch_to(nextTask);
}

int schedler_unblock_task(Task* task) {
    KERNEL_SCHED_BINDING* binding;

    init_schedler();

    if (!task || task->state != TASK_BLOCKED) {
        return -1;
    }

    binding = schedproc_binding_for_task(task);
    if (!binding || !binding->threadHandle) {
        return -1;
    }

    if (sp_unblock_thread(g_schedproc_context, binding->threadHandle, true) != SP_OK) {
        return -1;
    }

    schedproc_sync_task_state(task);
    percpu_kick_cpu(task->cpu_affinity);
    return 0;
}

/*
 * schedler_timer_tick
 *
 * Advance the visible kernel tick count and the schedproc scheduler state, then
 * preempt the current task only when the dispatch decision changed.
 */
void schedler_timer_tick(void) {
    Task* nextTask;
    unsigned long interruptedMode;

    init_schedler();
    dbg_wait_if_paused();

    if (task_cpu_index() != 0) {
        return;
    }

    /*
     * The legacy kernel uses `preempt_disable()` as a hard promise that timer
     * preemption will not context-switch in the middle of sensitive regions
     * such as:
     * - task/process creation,
     * - process exit teardown,
     * - zombie cleanup,
     * - user-image replacement during exec.
     *
     * The deleted scheduler honored that contract. The first schedproc-backed
     * timer path did not, which allowed IRQ-time task switches while a new task
     * descriptor was still being initialized. When the switch landed on a
     * partially constructed task, `cpu_switch_to` could restore an invalid
     * `pc` and branch into user-space addresses while still in EL1.
     *
     * For now, keep the compatibility contract simple and conservative:
     * while preemption is disabled, we do not advance scheduler state from the
     * timer interrupt. That avoids mid-critical-section switches and restores
     * the kernel behaviour existing code already depends on.
     */
    if (current_task && current_task->preempt_count > 0) {
        return;
    }

    /*
     * Do not context-switch out of an EL1 timer interrupt.
     *
     * When the timer fires while the CPU is already executing kernel code
     * (for example inside a syscall body or loader path), the exception entry
     * frame on the current stack describes how to resume that kernel code.
     * Replacing `current_task` and its kernel stack inside that IRQ would make
     * the vector return path consume another task's saved frame, which is how
     * we end up trying to execute user virtual addresses with
     * `SPSR_EL1 == EL1h`.
     *
     * We still advance time and wake sleeping tasks above. The actual switch is
     * deferred until the kernel reaches a safe scheduling point outside this
     * EL1 interrupt frame.
     */
    interruptedMode = schedproc_previous_exception_mode();
    g_schedproc_ticks++;
    if (interruptedMode != PSR_MODE_EL0t) {
        if (g_schedproc_context) {
            (void)sp_tick_deferred(g_schedproc_context, 1u);
        }
        return;
    }

    if (g_schedproc_context) {
        (void)sp_tick(g_schedproc_context, 1u);
    }

    schedproc_sync_all_visible_state();
    nextTask = schedproc_resolve_next_task();
    if (nextTask != current_task) {
        schedler_switch_to(nextTask);
    }
}

void schedule_tail(void) {
    preempt_enable();
}

/*
 * task_pt_regs
 *
 * The trap frame still lives at the top of the task's dedicated kernel stack
 * page. This helper computes that address for exception return and exec paths.
 */
struct pt_regs* task_pt_regs(struct task_struct* tsk) {
    Address stackPage = tsk ? process_task_stack_page(tsk) : 0;
    unsigned long frame = stackPage
        ? (unsigned long)(stackPage + VA_START + PAGE_SIZE - sizeof(struct pt_regs))
        : (unsigned long)tsk + THREAD_SIZE - sizeof(struct pt_regs);

    return (struct pt_regs*)frame;
}

/*
 * process_reset_user_space
 *
 * Drop every task-private user mapping, heap record, DLL-local record, and
 * page-table page so a later exec can rebuild the address space from scratch.
 */
void process_reset_user_space(Task* task) {
    Address taskPage = process_task_kernel_page(task);
    Address stackPage = process_task_stack_page(task);
    int userPageCount = process_clamp_page_count("user page", task ? task->id : -1, task ? task->mm.user_pages_count : 0);
    int kernelPageCount = process_clamp_page_count("kernel page", task ? task->id : -1, task ? task->mm.kernel_pages_count : 0);
    int permanentPages = process_permanent_kernel_pages(task);
    Address extraKernelPages[MAX_PROCESS_PAGES];
    int extraKernelPageCount = 0;

    if (!task) {
        return;
    }

    /*
     * Tear down user mappings through the normal MMU unmap path so shared-page
     * reference counts and `task->mm.user_pages[]` stay consistent. Direct page
     * frees here are unsafe now that DLLs, drivers, and helper EL0 threads can
     * share physical pages.
     */
    while (task->mm.user_pages_count > 0) {
        Address pageVa = (Address)(task->mm.user_pages[0].virt_addr & MM_PAGE_MASK);

        if (pageVa != 0 && process_unmap_page(task, pageVa) == 0) {
            continue;
        }

        /*
         * If bookkeeping is already stale, drop the broken slot so reset can
         * still make progress instead of looping forever on one bad entry.
         */
        for (int shift = 0; shift < task->mm.user_pages_count - 1; shift++) {
            task->mm.user_pages[shift] = task->mm.user_pages[shift + 1];
        }
        task->mm.user_pages_count--;
        if (task->mm.user_pages_count >= 0) {
            memzero((Address)&task->mm.user_pages[task->mm.user_pages_count], sizeof(task->mm.user_pages[0]));
        }
    }

    for (int index = permanentPages; index < kernelPageCount; index++) {
        Address page = process_normalize_page_address(task->mm.kernel_pages[index]);
        Bool duplicate = false;

        if (page == 0) {
            continue;
        }
        for (int prior = 0; prior < extraKernelPageCount; prior++) {
            if (extraKernelPages[prior] == page) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            extraKernelPages[extraKernelPageCount++] = page;
        }
    }

    for (int index = 0; index < extraKernelPageCount; index++) {
        if (extraKernelPages[index] != 0) {
            mem_free_page(extraKernelPages[index]);
        }
    }

    task->mm.user_pages_count = 0;
    task->mm.kernel_pages_count = 0;
    task->mm.pgd = 0;
    task->mm.heap_next = USER_HEAP_BASE;
    task->mm.dll_local_next = USER_SHARED_LIBRARY_LOCAL_BASE;
    memzero((Address)task->mm.user_pages, sizeof(task->mm.user_pages));
    memzero((Address)task->mm.kernel_pages, sizeof(task->mm.kernel_pages));
    if (taskPage != 0) {
        task->mm.kernel_pages[task->mm.kernel_pages_count++] = taskPage;
    }
    if (stackPage != 0) {
        task->mm.kernel_pages[task->mm.kernel_pages_count++] = stackPage;
    }
    task->kernel_stack_page = stackPage;
    memzero((Address)task->mm.heap_allocs, sizeof(task->mm.heap_allocs));
    memzero((Address)task->mm.dll_locals, sizeof(task->mm.dll_locals));
}

/*
 * process_unload / exit_current_process
 *
 * Mark the current task or process dead in both worlds:
 * - legacy `Task`/`Process` tables become zombies,
 * - schedproc thread/process state transitions to dead/exiting.
 *
 * After that, the scheduler immediately dispatches away from the exiting task.
 */
void process_unload(Task* task) {
    Bool includeSiblings = false;

    if (!task) {
        return;
    }

    preempt_disable();
    scheduler_lock_acquire();
    includeSiblings = (task->process && task->process->main_thread == task);
    _trace(
        "process_unload: task=%ld process=%ld includeSiblings=%d state=%d pgd=0x%lX",
        task->id,
        task->process ? task->process->id : -1,
        includeSiblings,
        task->state,
        task->mm.pgd);
    schedproc_mark_task_zombie(task, includeSiblings);
    scheduler_lock_release();
    preempt_enable();

    if (task == current_task) {
        Task* nextTask;

        schedproc_sync_all_visible_state();
        nextTask = schedproc_select_exit_successor(task);
        schedproc_sync_all_visible_state();
        schedler_switch_to(nextTask);
        return;
    }

    schedler_schedule();
}

void exit_current_process(long result) {
    if (current_task && current_task->process) {
        current_task->process->result = result;
        schedproc_publish_exit_status(current_task->process->id, current_task->process->parent_process_id, result);
    }

    _trace("Process %d exited with status 0x%lx", current_task ? (int)current_task->id : -1, result);
    process_unload(current_task);

    while (1) {
        asm volatile("wfe\n");
    }
}

static void schedproc_collect_process_tree(long pid, Bool* visited) {
    if (!visited || pid <= 0 || pid >= NR_PROCESSES || visited[pid]) {
        return;
    }

    visited[pid] = true;
    for (int index = 1; index < NR_PROCESSES; index++) {
        Process* child = processes[index];

        if (!child || child->state == PROCESS_ZOMBIE || child->parent_process_id != pid) {
            continue;
        }

        schedproc_collect_process_tree(child->id, visited);
    }
}

static void schedproc_kill_marked_processes(Bool* visited, long result, long rootPid) {
    if (!visited) {
        return;
    }

    for (int pid = 1; pid < NR_PROCESSES; pid++) {
        Task* task;
        Process* process;

        if (!visited[pid] || pid == rootPid) {
            continue;
        }

        process = process_lookup(pid);
        task = process_main_thread(pid);
        if (process) {
            process->result = result;
            schedproc_publish_exit_status(process->id, process->parent_process_id, result);
            log_info("process tree kill: root=%ld child=%ld name=%s", rootPid, process->id, process->name);
        }
        if (!task || task == current_task || task->state == TASK_ZOMBIE) {
            continue;
        }

        scheduler_lock_acquire();
        schedproc_mark_task_zombie(task, task->process && task->process->main_thread == task);
        scheduler_lock_release();
    }
}

void terminate_current_process_tree(long result) {
    Bool visited[NR_PROCESSES];
    long rootPid;

    if (!current_task || !current_task->process) {
        exit_current_process(result);
        return;
    }

    for (int index = 0; index < NR_PROCESSES; index++) {
        visited[index] = false;
    }

    rootPid = current_task->process->id;
    schedproc_collect_process_tree(rootPid, visited);
    schedproc_kill_marked_processes(visited, result, rootPid);
    exit_current_process(result);
}

int kill_task(long pid) {
    Task* task;
    Process* process;

    if (pid <= 0 || pid >= NR_TASKS) {
        return -1;
    }

    task = process_main_thread(pid);
    if (!task) {
        task = tasks[pid];
    }
    if (!task || task->state == TASK_ZOMBIE) {
        return -1;
    }

    process = task->process;
    if (process) {
        process->result = -1;
        schedproc_publish_exit_status(process->id, process->parent_process_id, -1);
    }

    if (task == current_task) {
        exit_current_process(-1);
        return 0;
    }

    scheduler_lock_acquire();
    schedproc_mark_task_zombie(task, task->process && task->process->main_thread == task);
    scheduler_lock_release();
    return 0;
}

/*
 * Wait for one direct child to publish an exit result.
 *
 * The caller blocks in the scheduler instead of sleeping on a timer. When the
 * child exits, `schedproc_publish_exit_status()` unblocks this parent and the
 * loop retries the consume step immediately.
 */
int process_wait(long pid, long* result) {
    long parent_pid;

    if (!result || pid <= 0 || pid >= NR_PROCESSES || !current_process) {
        return -1;
    }

    parent_pid = current_process->id;
    for (;;) {
        Process* process = process_lookup(pid);

        if (schedproc_try_consume_exit_status(pid, parent_pid, result)) {
            return 1;
        }
        if (!process) {
            return -1;
        }
        if (process->parent_process_id != parent_pid) {
            return -1;
        }

        schedler_block_current();
    }
}

/*
 * cleanup_zombie_processes
 *
 * Reclaim every resource that belonged to zombie tasks:
 * - detach loader-owned user modules,
 * - free tracked kernel/user pages,
 * - destroy schedproc thread/process objects,
 * - remove slots from the global kernel lookup tables.
 */
void cleanup_zombie_processes(void) {


    for (int index = 0; index < NR_TASKS; index++) {
        Task* task = tasks[index];
        Process* process;
        PSP_THREAD threadHandle;
        PSP_PROCESS processHandle;
        Address kernelPages[MAX_PROCESS_PAGES];
        Address userPageVas[MAX_PROCESS_PAGES];
        int kernelPageCount;
        int userPageCount;

        if (!task || task->state != TASK_ZOMBIE || task == &init_task) {
            continue;
        }

        process = task->process;
        threadHandle = g_schedproc_bindings[index].threadHandle;
        processHandle = process ? schedproc_binding_for_process(process) : 0;

        /*
         * Run full loader shutdown before we snapshot the task page tables so
         * every DLL/driver `Deinit()` callback executes and every loader-owned
         * mapping drops out of `task->mm.user_pages`.
         */
        (void)ldr_kernel_release_user_modules_for_task((void*)task);

        kernelPageCount = process_clamp_page_count("kernel page", task->id, task->mm.kernel_pages_count);
        userPageCount = process_clamp_page_count("user page", task->id, task->mm.user_pages_count);

        for (int pageIndex = 0; pageIndex < kernelPageCount; pageIndex++) {
            kernelPages[pageIndex] = process_normalize_page_address(task->mm.kernel_pages[pageIndex]);
        }
        for (int pageIndex = 0; pageIndex < userPageCount; pageIndex++) {
            userPageVas[pageIndex] = (Address)(task->mm.user_pages[pageIndex].virt_addr & MM_PAGE_MASK);
        }

        /*
         * Release any residual user mappings through the normal MMU unmap path
         * instead of freeing physical pages directly.
         *
         * This keeps task bookkeeping, page-table entries, and shared-page
         * reference counts in sync after loader-driven unloads and shared
         * user-thread clones.
         */
        for (int pageIndex = 0; pageIndex < userPageCount; pageIndex++) {
            if (userPageVas[pageIndex] != 0) {
                (void)process_unmap_page(task, userPageVas[pageIndex]);
            }
        }

        for (int pageIndex = kernelPageCount - 1; pageIndex >= 0; pageIndex--) {
            Bool duplicate = false;

            if (kernelPages[pageIndex] != 0) {
                for (int priorIndex = pageIndex - 1; priorIndex >= 0; priorIndex--) {
                    if (kernelPages[priorIndex] == kernelPages[pageIndex]) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    mem_free_page(kernelPages[pageIndex]);
                }
            }
        }

        if (threadHandle) {
            (void)sp_mark_thread_dead(g_schedproc_context, threadHandle);
            (void)sp_destroy_thread(g_schedproc_context, threadHandle);
        }

        scheduler_lock_acquire();
        g_schedproc_bindings[index].threadHandle = 0;
        tasks[index] = 0;
        nr_tasks--;

        if (process) {
            if (process->thread_count > 0) {
                process->thread_count--;
            }
            schedproc_reassign_main_thread(process, task);
            if (process->thread_count == 0) {
                if (process->id >= 0 && process->id < NR_PROCESSES && processes[process->id] == process) {
                    processes[process->id] = 0;
                    nr_processes--;
                }
                if (process->id >= 0 && process->id < NR_TASKS) {
                    g_schedproc_bindings[process->id].processHandle = 0;
                }
            }
        }
        scheduler_lock_release();

        if (process && process->thread_count == 0) {
            if (processHandle) {
                (void)sp_destroy_process(g_schedproc_context, processHandle);
            }
            kfree((Address)process);
        }
    }
}

/*
 * user_process_loader
 *
 * Legacy flat-image loader preserved for older call sites. Newer user-mode
 * launches go through `user-runtime.c`, but this function still keeps the raw
 * image path working for experiments and fallback flows.
 */
void user_process_loader(Address programAddr, ulong programSize) {
    Task* task = current_task;
    int programCodePages = (int)(programSize / PAGE_SIZE) + 1;
    int programStackPages = (int)(VA_USER_STACK / PAGE_SIZE);
    int pageIndex;
    int imagePageIndex = 0;

    if (!task) {
        return;
    }

    preempt_disable();
    task->flags = 0;
    task->cpu_context.x19 = 0;
    task->mm.pgd = 0;

    {
        Address stackPage = mem_alloc_page();
        struct pt_regs* regs;

        if (!stackPage) {
            preempt_enable();
            process_unload(task);
            return;
        }

        process_map_page(task, stackPage, (Address)(VA_USER_STACK - PAGE_SIZE), PE_USER_DATA);
        regs = task_pt_regs(task);
        regs->pstate = PSR_MODE_EL0t;
        regs->pc = VA_USER_START;
        regs->sp = VA_USER_STACK;
    }

    for (pageIndex = programStackPages; programCodePages > 0; programCodePages--, pageIndex++, imagePageIndex++) {
        Address page = mem_alloc_page();
        int codeOffset;
        int copyLength;

        if (!page) {
            preempt_enable();
            process_unload(task);
            return;
        }

        codeOffset = imagePageIndex * PAGE_SIZE;
        copyLength = (programSize - (imagePageIndex * PAGE_SIZE)) > PAGE_SIZE
            ? PAGE_SIZE
            : (int)(programSize % PAGE_SIZE);

        process_map_page(task, page, (Address)(pageIndex * PAGE_SIZE), PE_USER_CODE);
        memcpy((Address)(page + VA_START), (Address)(programAddr + codeOffset), copyLength);
    }

    set_pgd(task->mm.pgd);
    preempt_enable();
}

/*
 * create_user_process
 *
 * Preserve the historical helper that spawns a new task which begins by
 * running `user_process_loader` on a raw user image buffer.
 */
int create_user_process(Address programAddr, ULong programSize) {
    int resultPid = -1;

    preempt_disable();

    {
        Task* newTask;
        Process* newProcess = 0;
        int slot;
        Address taskPage = mem_alloc_page();
        Address stackPage;
        struct pt_regs* childregs;

        if (!taskPage) {
            preempt_enable();
            return -1;
        }

        stackPage = mem_alloc_page();
        if (!stackPage) {
            mem_free_page(taskPage);
            preempt_enable();
            return -1;
        }

        memzero(taskPage + VA_START, PAGE_SIZE);
        newTask = (struct task_struct*)(taskPage + VA_START);
        newTask->kernel_stack_page = stackPage;
        slot = process_allocate_thread_slot();
        if (slot < 0) {
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return -1;
        }

        newProcess = process_allocate_box(slot, current_process);
        if (!newProcess) {
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return -1;
        }

        childregs = task_pt_regs(newTask);
        newTask->mm.heap_next = USER_HEAP_BASE;
        newTask->mm.dll_local_next = USER_SHARED_LIBRARY_LOCAL_BASE;
        newTask->name = (Buffer)newTask->thread_name;
        newTask->thread_name[0] = '\0';
        memzero((Address)newTask->mm.heap_allocs, sizeof(newTask->mm.heap_allocs));
        memzero((Address)newTask->mm.dll_locals, sizeof(newTask->mm.dll_locals));
        if (process_track_kernel_page(newTask, taskPage) < 0 || process_track_kernel_page(newTask, stackPage) < 0) {
            kfree((Address)newProcess);
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return -1;
        }

        newTask->cpu_context.x19 = (ULong)&user_process_loader;
        newTask->cpu_context.x20 = (ULong)programAddr;
        newTask->cpu_context.x21 = programSize;
        newTask->mm.pgd = schedproc_kernel_pgd_template();
        newTask->flags = PF_KTHREAD;
        newTask->priority = current_task ? current_task->priority : PRIORITY_NORMAL;
        newTask->state = TASK_READY;
        newTask->counter = newTask->priority;
        newTask->cpu_affinity = schedproc_select_initial_cpu_affinity();
        newTask->preempt_count = 1;
        newTask->cpu_context.pc = (unsigned long)start_thread_context;
        newTask->cpu_context.sp = (unsigned long)childregs;
        newTask->id = slot;

        if (schedproc_bind_kernel_entities(newProcess, newTask, "user-loader") != 0) {
            kfree((Address)newProcess);
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return -1;
        }

        scheduler_lock_acquire();
        tasks[slot] = newTask;
        processes[newProcess->id] = newProcess;
        nr_tasks++;
        nr_processes++;
        process_attach_thread(newTask, newProcess);
        scheduler_lock_release();
        percpu_kick_cpu(newTask->cpu_affinity);
        resultPid = (int)newProcess->id;
    }

    preempt_enable();
    return resultPid;
}

/*
 * process_create_main_thread
 *
 * Main entry used by the current kernel to spawn:
 * - bootstrap kernel threads,
 * - user-loader helper threads,
 * - and future single-threaded processes.
 */
ulong process_create_main_thread(Flags flags, Address programAddr, Pointer arg) {
    ulong resultPid = (ulong)-1;
    const char* bindingName = (programAddr == (Address)&schedproc_housekeeper_main) ? "sched-reaper" : "kernel-thread";

    preempt_disable();

    {
        Task* newTask;
        Process* newProcess = 0;
        int slot;
        Address taskPage = mem_alloc_page();
        Address stackPage;
        struct pt_regs* childregs;

        if (!taskPage) {
            preempt_enable();
            return (ulong)-1;
        }

        stackPage = mem_alloc_page();
        if (!stackPage) {
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        memzero(taskPage + VA_START, PAGE_SIZE);
        newTask = (struct task_struct*)(taskPage + VA_START);
        newTask->kernel_stack_page = stackPage;
        slot = process_allocate_thread_slot();
        if (slot < 0) {
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        newProcess = process_allocate_box(slot, current_process);
        if (!newProcess) {
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        childregs = task_pt_regs(newTask);
        newTask->mm.heap_next = USER_HEAP_BASE;
        newTask->mm.dll_local_next = USER_SHARED_LIBRARY_LOCAL_BASE;
        newTask->name = (Buffer)newTask->thread_name;
        newTask->thread_name[0] = '\0';
        memzero((Address)newTask->mm.heap_allocs, sizeof(newTask->mm.heap_allocs));
        memzero((Address)newTask->mm.dll_locals, sizeof(newTask->mm.dll_locals));
        if (process_track_kernel_page(newTask, taskPage) < 0 || process_track_kernel_page(newTask, stackPage) < 0) {
            kfree((Address)newProcess);
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        if (flags & PF_KTHREAD) {
            newTask->cpu_context.x19 = (ULong)programAddr;
            newTask->cpu_context.x20 = (ULong)arg;
            newTask->mm.pgd = schedproc_kernel_pgd_template();
        }
        else {
            kerror("User thread cloning is not implemented");
            kfree((Address)newProcess);
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        newTask->flags = flags;
        newTask->priority = current_task ? current_task->priority : PRIORITY_NORMAL;
        newTask->state = TASK_READY;
        newTask->counter = newTask->priority;
        newTask->cpu_affinity = schedproc_select_initial_cpu_affinity();
        newTask->preempt_count = 1;
        newTask->cpu_context.pc = (ULong)start_thread_context;
        newTask->cpu_context.sp = (ULong)childregs;
        newTask->id = slot;

        if (schedproc_bind_kernel_entities(newProcess, newTask, bindingName) != 0) {
            kfree((Address)newProcess);
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        scheduler_lock_acquire();
        tasks[slot] = newTask;
        processes[newProcess->id] = newProcess;
        nr_tasks++;
        nr_processes++;
        process_attach_thread(newTask, newProcess);
        scheduler_lock_release();

        percpu_kick_cpu(newTask->cpu_affinity);
        resultPid = (ulong)newProcess->id;
    }

    preempt_enable();
    return resultPid;
}

/*
 * process_copy_thread
 *
 * Create another thread inside the current process. The kernel still uses the
 * legacy `Task` structure for CPU context, but the new schedproc module owns
 * the runnable/sleep/current bookkeeping for the spawned thread.
 */
ulong process_copy_thread(Flags flags, Address programAddr, Pointer arg) {
    ulong resultTid = (ulong)-1;
    Process* owner = current_process;

    if (!owner) {
        return (ulong)-1;
    }

    preempt_disable();

    {
        Task* newTask;
        int slot;
        Address taskPage = mem_alloc_page();
        Address stackPage;
        struct pt_regs* childregs;

        if (!taskPage) {
            preempt_enable();
            return (ulong)-1;
        }

        stackPage = mem_alloc_page();
        if (!stackPage) {
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        memzero(taskPage + VA_START, PAGE_SIZE);
        newTask = (struct task_struct*)(taskPage + VA_START);
        newTask->kernel_stack_page = stackPage;
        slot = process_allocate_thread_slot();
        if (slot < 0) {
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        childregs = task_pt_regs(newTask);
        newTask->mm.heap_next = USER_HEAP_BASE;
        newTask->mm.dll_local_next = USER_SHARED_LIBRARY_LOCAL_BASE;
        newTask->name = (Buffer)newTask->thread_name;
        newTask->thread_name[0] = '\0';
        memzero((Address)newTask->mm.heap_allocs, sizeof(newTask->mm.heap_allocs));
        memzero((Address)newTask->mm.dll_locals, sizeof(newTask->mm.dll_locals));
        if (process_track_kernel_page(newTask, taskPage) < 0 || process_track_kernel_page(newTask, stackPage) < 0) {
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        if (flags & PF_KTHREAD) {
            newTask->cpu_context.x19 = (ULong)programAddr;
            newTask->cpu_context.x20 = (ULong)arg;
            newTask->mm.pgd = schedproc_kernel_pgd_template();
        }
        else {
            kerror("User thread cloning is not implemented");
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        newTask->flags = flags;
        newTask->priority = current_task ? current_task->priority : PRIORITY_NORMAL;
        newTask->state = TASK_READY;
        newTask->counter = newTask->priority;
        newTask->cpu_affinity = schedproc_select_initial_cpu_affinity();
        newTask->preempt_count = 1;
        newTask->cpu_context.pc = (ULong)start_thread_context;
        newTask->cpu_context.sp = (ULong)childregs;
        newTask->id = slot;

        if (schedproc_bind_kernel_thread(owner, newTask, "thread") != 0) {
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        scheduler_lock_acquire();
        tasks[slot] = newTask;
        nr_tasks++;
        process_attach_thread(newTask, owner);
        scheduler_lock_release();

        percpu_kick_cpu(newTask->cpu_affinity);
        resultTid = (ulong)slot;
    }

    preempt_enable();
    return resultTid;
}

/*
 * process_create_user_thread
 *
 * Create one additional EL0 thread inside the current process by cloning the
 * caller's existing user mappings into a fresh page-table tree with shared
 * physical pages and giving the new thread its own private user stack page.
 *
 * Important ownership rules:
 * - code/data/rodata pages from the source task are mapped with
 *   `process_map_shared_page()`, so the new thread observes the same driver
 *   globals without taking sole ownership of those physical pages,
 * - the new thread receives a dedicated user stack page at `VA_USER_STACK`,
 * - cleanup of the new thread only releases its retained references and its
 *   private stack/table pages, leaving the original task's mappings intact.
 */
ulong process_create_user_thread(Address entryPc, Pointer arg) {
    ulong resultTid = (ulong)-1;
    Process* owner = current_process;
    Task* sourceTask = current_task;

    if (!owner || !sourceTask || (sourceTask->flags & PF_KTHREAD)) {
        return (ulong)-1;
    }

    preempt_disable();

    {
        Task* newTask;
        int slot;
        Address taskPage = mem_alloc_page();
        Address stackPage;
        Address userStackPage;
        struct pt_regs* childregs;

        if (!taskPage) {
            preempt_enable();
            return (ulong)-1;
        }

        stackPage = mem_alloc_page();
        if (!stackPage) {
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        /*
         * The background driver/user helper runs in EL0, so it needs its own
         * private user stack page in addition to the normal kernel stack page
         * used for exceptions and scheduler context.
         */
        userStackPage = mem_alloc_page();
        if (!userStackPage) {
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        memzero(taskPage + VA_START, PAGE_SIZE);
        newTask = (struct task_struct*)(taskPage + VA_START);
        newTask->kernel_stack_page = stackPage;
        slot = process_allocate_thread_slot();
        if (slot < 0) {
            mem_free_page(userStackPage);
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        childregs = task_pt_regs(newTask);
        newTask->mm.heap_next = sourceTask->mm.heap_next;
        newTask->mm.dll_local_next = sourceTask->mm.dll_local_next;
        newTask->name = (Buffer)newTask->thread_name;
        strncpy(newTask->thread_name, "user-thread", sizeof(newTask->thread_name) - 1);
        newTask->thread_name[sizeof(newTask->thread_name) - 1] = '\0';
        memzero((Address)newTask->mm.heap_allocs, sizeof(newTask->mm.heap_allocs));
        memzero((Address)newTask->mm.dll_locals, sizeof(newTask->mm.dll_locals));
        if (process_track_kernel_page(newTask, taskPage) < 0 || process_track_kernel_page(newTask, stackPage) < 0) {
            mem_free_page(userStackPage);
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        /*
         * Share every existing user mapping except the caller's live stack
         * page. That preserves module globals and imported code while giving
         * the cloned thread a private stack to run on.
         */
        for (int index = 0; index < sourceTask->mm.user_pages_count; index++) {
            MmuWalkResult walk;
            Address sourceVa = sourceTask->mm.user_pages[index].virt_addr;
            Address sourcePa = (Address)(sourceTask->mm.user_pages[index].phys_addr & MM_PAGE_MASK);

            if (sourceVa == (VA_USER_STACK - PAGE_SIZE) || sourcePa == 0) {
                continue;
            }
            if (process_walk_page(sourceTask, sourceVa, &walk) != 0) {
                mem_free_page(userStackPage);
                mem_free_page(stackPage);
                mem_free_page(taskPage);
                preempt_enable();
                return (ulong)-1;
            }
            if (process_map_shared_page(newTask, sourcePa, sourceVa, walk.flags) != 0) {
                mem_free_page(userStackPage);
                mem_free_page(stackPage);
                mem_free_page(taskPage);
                preempt_enable();
                return (ulong)-1;
            }
        }

        if (process_map_page(newTask, userStackPage, (Address)(VA_USER_STACK - PAGE_SIZE), PE_USER_DATA) != 0) {
            mem_free_page(userStackPage);
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        memzero((Address)childregs, sizeof(*childregs));
        childregs->regs[0] = (unsigned long)arg;
        childregs->pstate = PSR_MODE_EL0t;
        childregs->pc = entryPc;
        childregs->sp = VA_USER_STACK;

        newTask->flags = 0;
        newTask->priority = sourceTask->priority;
        newTask->state = TASK_READY;
        newTask->counter = newTask->priority;
        newTask->cpu_affinity = schedproc_select_initial_cpu_affinity();
        newTask->preempt_count = 1;
        newTask->cpu_context.x19 = 0;
        newTask->cpu_context.x20 = 0;
        newTask->cpu_context.pc = (ULong)start_thread_context;
        newTask->cpu_context.sp = (ULong)childregs;
        newTask->id = slot;

        if (schedproc_bind_kernel_thread(owner, newTask, "driver-loop") != 0) {
            process_reset_user_space(newTask);
            mem_free_page(stackPage);
            mem_free_page(taskPage);
            preempt_enable();
            return (ulong)-1;
        }

        scheduler_lock_acquire();
        tasks[slot] = newTask;
        nr_tasks++;
        process_attach_thread(newTask, owner);
        scheduler_lock_release();

        percpu_kick_cpu(newTask->cpu_affinity);
        resultTid = (ulong)slot;
    }

    preempt_enable();
    return resultTid;
}

int copy_process(Flags clone_flags, unsigned long fn, unsigned long arg, unsigned long stack) {
    (void)stack;
    return (int)process_copy_thread(clone_flags, (Address)fn, (Pointer)arg);
}

/*
 * Process/task dump helpers
 *
 * The shell already depends heavily on these formatted views during bring-up,
 * so they remain available as compatibility helpers.
 */
void process_dump_task_struct(Task* task) {
    Process* process = task ? task->process : 0;
    const char* stateName = "ZOMBIE";
    const char* stateColor = "\x1b[31m";

    if (!task) {
        return;
    }

    schedproc_sync_task_state(task);
    if (task->state == TASK_RUNNING) {
        stateName = "RUN";
        stateColor = "\x1b[32m";
    }
    else if (task->state == TASK_READY) {
        stateName = "READY";
        stateColor = "\x1b[36m";
    }
    else if (task->state == TASK_SLEEPING) {
        stateName = "SLEEP";
        stateColor = "\x1b[33m";
    }
    else if (task->state == TASK_BLOCKED) {
        stateName = "BLOCK";
        stateColor = "\x1b[35m";
    }

    kprint(TASK_DUMP_SEPARATOR);
    kprint("\x1b[1;36mThread Details\x1b[0m\n");
    kprint(TASK_DUMP_SEPARATOR);
    kprint("  \x1b[1;34mthread        \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%d\x1b[0m\n", task->id);
    kprint("  \x1b[1;34mprocess       \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%ld\x1b[0m\n", task_process_id(task));
    kprint("  \x1b[1;34mparent proc   \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%ld\x1b[0m\n", process ? process->parent_process_id : -1L);
    kprint("  \x1b[1;34mname          \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%s\x1b[0m\n", task->name ? (char*)task->name : "<none>");
    kprint("  \x1b[1;34mprogram path  \x1b[0m \x1b[2;37m|\x1b[0m \x1b[36m%s\x1b[0m\n", process && process->program_path[0] ? process->program_path : "<none>");
    kprint("  \x1b[1;34mlaunch args   \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%s\x1b[0m\n", process && process->launch_args[0] ? process->launch_args : "<none>");
    kprint("  \x1b[1;34mthreads       \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%ld\x1b[0m\n", process ? process->thread_count : 0L);
    kprint("  \x1b[1;34mstate         \x1b[0m \x1b[2;37m|\x1b[0m %s%s\x1b[0m\n", stateColor, stateName);
    kprint("  \x1b[1;34mcounter       \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%d\x1b[0m\n", task->counter);
    kprint("  \x1b[1;34mpriority      \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%d\x1b[0m\n", task->priority);
    kprint("  \x1b[1;34mcpu           \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%u\x1b[0m\n", task->cpu_affinity);
    kprint("  \x1b[1;34mpreempt       \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%d\x1b[0m\n", task->preempt_count);
    kprint("  \x1b[1;34mflags         \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m0x%X\x1b[0m\n", task->flags);
    kprint(TASK_DUMP_SEPARATOR);
    kprint("\x1b[1;36mMemory\x1b[0m\n");
    kprint(TASK_DUMP_SEPARATOR);
    kprint("  \x1b[1;34mpgd           \x1b[0m \x1b[2;37m|\x1b[0m \x1b[36m0x%lX\x1b[0m\n", task->mm.pgd);
    kprint("  \x1b[1;34mheap next     \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m0x%lX\x1b[0m\n", task->mm.heap_next);
    kprint("  \x1b[1;34mdll next      \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m0x%lX\x1b[0m\n", task->mm.dll_local_next);
    kprint("  \x1b[1;34muser pages    \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%d\x1b[0m\n", task->mm.user_pages_count);
    kprint("  \x1b[1;34mkernel pages  \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%d\x1b[0m\n", task->mm.kernel_pages_count);
    kprint("  \x1b[1;34mkstack page   \x1b[0m \x1b[2;37m|\x1b[0m \x1b[36m0x%lX\x1b[0m\n", task->kernel_stack_page);
    kprint(TASK_DUMP_SEPARATOR);
    kprint("\x1b[1;36mRegisters\x1b[0m\n");
    kprint(TASK_DUMP_SEPARATOR);
    kprint("  \x1b[1;34mx19   \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mx20   \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mx21   \x1b[0m \x1b[36m0x%016lX\x1b[0m\n", task->cpu_context.x19, task->cpu_context.x20, task->cpu_context.x21);
    kprint("  \x1b[1;34mx22   \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mx23   \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mx24   \x1b[0m \x1b[36m0x%016lX\x1b[0m\n", task->cpu_context.x22, task->cpu_context.x23, task->cpu_context.x24);
    kprint("  \x1b[1;34mx25   \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mx26   \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mx27   \x1b[0m \x1b[36m0x%016lX\x1b[0m\n", task->cpu_context.x25, task->cpu_context.x26, task->cpu_context.x27);
    kprint("  \x1b[1;34mx28   \x1b[0m \x1b[36m0x%016lX\x1b[0m\n", task->cpu_context.x28);
    kprint("  \x1b[1;34msp    \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mpc    \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mfp    \x1b[0m \x1b[36m0x%016lX\x1b[0m\n", task->cpu_context.sp, task->cpu_context.pc, task->cpu_context.fp);
    kprint(TASK_DUMP_SEPARATOR);
}

void process_dump_task(ulong pid) {
    if (pid < NR_TASKS && tasks[pid]) {
        process_dump_task_struct(tasks[pid]);
    }
}

void dump_current_task(void) {
    process_dump_task_struct(current_task);
}

void print_current_task_id(void) {
    printf("==> CURRENT TASK ID: %d\n\n", current_task ? (int)current_task->id : -1);
}
