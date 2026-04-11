#ifndef TASK_H
#define TASK_H

#define THREAD_CPU_CONTEXT 0 // offset of cpu_context in task_struct

#ifndef __ASSEMBLER__

#include "ros.h"
#include "user-exe.h"

typedef enum TASK_STATES {
    TASK_ZOMBIE = 0,
    TASK_READY = 1,
    TASK_RUNNING = 2,
    TASK_SLEEPING = 3,
    TASK_BLOCKED = 4
} TaskState;

typedef enum PROCESS_STATES {
    PROCESS_ZOMBIE = 0,
    PROCESS_ACTIVE = 1
} ProcessState;

enum TASK_PRIORITY {
    PRIORITY_NORMAL = 1,
    PRIORITY_MEDIUM = 2,
    PRIORITY_HIGH = 3,
    PRIORITY_REAL_TIME = 4
};

typedef struct cpu_context {
    unsigned long x19;
    unsigned long x20;
    unsigned long x21;
    unsigned long x22;
    unsigned long x23;
    unsigned long x24;
    unsigned long x25;
    unsigned long x26;
    unsigned long x27;
    unsigned long x28;
    unsigned long fp; // x29
    unsigned long sp;
    unsigned long pc; // x30

} CPUContext;

#define MAX_PROCESS_PAGES 96
#define MAX_USER_HEAP_ALLOCS 16
#define USER_HEAP_BASE 0x100000
#define USER_HEAP_LIMIT 0x800000
#define TASK_USER_NAME_MAX 32
#define TASK_USER_PROGRAM_PATH_MAX 128
#define TASK_USER_LAUNCH_ARGS_MAX 128

#define USER_PAGE_OWNED 0x1U
#define USER_PAGE_SHARED 0x2U

/**
 * Track one user mapping so the kernel can reclaim task-owned pages later.
 *
 * Fields:
 *   phys_addr: Backing physical page address.
 *   virt_addr: User virtual address where that page is mapped.
 *   flags: Ownership bits that decide whether cleanup should free the backing page.
 */
typedef struct user_page {
    Address phys_addr;
    Address virt_addr;
    UInt flags;
} UserPage;

/**
 * Track one heap allocation returned to a user process.
 *
 * Fields:
 *   virt_addr: Base virtual address handed back to userspace.
 *   page_count: Number of contiguous pages owned by that allocation.
 */
typedef struct user_heap_alloc {
    Address virt_addr;
    ULong page_count;
} UserHeapAlloc;

/**
 * Track one per-task DLL-local allocation block keyed by library path.
 *
 * Fields:
 *   path: Shared-library path that owns this task-local storage block.
 *   virt_addr: Base user virtual address of the block in this task.
 *   size: Requested byte size preserved for compatibility checks.
 *   page_count: Number of mapped pages backing the block.
 */
typedef struct user_shared_library_local {
    char path[USER_SHARED_LIBRARY_PATH_MAX];
    Address virt_addr;
    ULong size;
    ULong page_count;
} UserSharedLibraryLocal;

/**
 * Per-task memory bookkeeping used for cleanup and dynamic user mappings.
 *
 * Fields:
 *   pgd: Root page-table physical address for this task.
 *   user_pages: User code, stack, and heap pages currently mapped.
 *   kernel_pages: Page-table pages that belong to this task.
 *   heap_next: Next free user heap virtual address.
 *   heap_allocs: Outstanding heap allocations that can be released with `free`.
 *   dll_local_next: Next free base in the per-task DLL-local virtual range.
 *   dll_locals: Task-private DLL storage blocks keyed by shared-library path.
 */
typedef struct mm_struct {
    Address pgd;
    int user_pages_count;
    UserPage user_pages[MAX_PROCESS_PAGES];
    int kernel_pages_count;
    Address kernel_pages[MAX_PROCESS_PAGES];
    Address heap_next;
    UserHeapAlloc heap_allocs[MAX_USER_HEAP_ALLOCS];
    Address dll_local_next;
    UserSharedLibraryLocal dll_locals[USER_SHARED_LIBRARY_MAX_TASK_LOCALS];
} TaskMemory;

/**
 * Saved register frame consumed by `exit_to_user_mode` when returning to EL0.
 *
 * Fields:
 *   regs: General-purpose register payload.
 *   sp: User stack pointer to restore.
 *   pc: User program counter to restore.
 *   pstate: Processor state that selects EL0 execution mode.
 */
typedef struct pt_regs {
    unsigned long regs[31];
    unsigned long sp;
    unsigned long pc;
    unsigned long pstate;
} TaskRegisters;

struct task_struct;

typedef struct process_struct {
    long id;
    long parent_process_id;
    ProcessState state;
    long result;
    long main_thread_id;
    long thread_count;
    struct task_struct* main_thread;
    char name[TASK_USER_NAME_MAX];
    char program_path[TASK_USER_PROGRAM_PATH_MAX];
    char launch_args[TASK_USER_LAUNCH_ARGS_MAX];
} Process;

/**
 * Full task descriptor used by the scheduler and process subsystem.
 *
 * Fields:
 *   cpu_context: Callee-saved kernel context used during task switches.
 *   id: Scheduler-visible thread identifier.
 *   state: Ready, running, sleeping, blocked, or zombie state.
 *   counter: Remaining timeslice budget.
 *   priority: Base scheduling weight.
 *   cpu_affinity: Logical CPU that owns and schedules this task.
 *   preempt_count: Nesting level of preemption disable sections.
 *   flags: Kernel-thread vs user-thread flags.
 *   process: Owning process box that groups this thread with siblings.
 *   mm: Per-process memory ownership currently carried by the main thread during phase 1.
 *   name: Human-readable thread label.
 *   thread_name: Small per-thread visible name.
 *   wakeup_tick: Scheduler tick when a sleeping task becomes runnable again.
 */
typedef struct task_struct {
    CPUContext cpu_context;
    long id;
    TaskState state;
    long counter;
    long priority;
    unsigned int cpu_affinity;
    long preempt_count;
    Flags flags;
    Process* process;
    TaskMemory mm;
    Address kernel_stack_page;
    Buffer name;
    char thread_name[TASK_USER_NAME_MAX];
    unsigned long wakeup_tick;
} Task;

/*
 * THEAD CONFIG
 */
#define THREAD_SIZE 4096
#define NR_CPUS 4
#define NR_TASKS 64
#define NR_PROCESSES NR_TASKS
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(Task) <= THREAD_SIZE, "Task struct must fit in one thread page");
#else
typedef char task_struct_must_fit_thread_page[(sizeof(Task) <= THREAD_SIZE) ? 1 : -1];
#endif
#define FIRST_TASK tasks[0]
#define LAST_TASK tasks[nr_tasks - 1]
#define PF_KTHREAD 0x00000002

/*
 * PSR bits
 */
#define PSR_MODE_EL0t 0x00000000
#define PSR_MODE_EL1t 0x00000004
#define PSR_MODE_EL1h 0x00000005
#define PSR_MODE_EL2t 0x00000008
#define PSR_MODE_EL2h 0x00000009
#define PSR_MODE_EL3t 0x0000000c
#define PSR_MODE_EL3h 0x0000000d

 /*
  * Let these variable expose everywhere through out our kernel code
  */
extern struct task_struct* current_tasks[NR_CPUS];
extern int current_task_ids[NR_CPUS];
extern struct task_struct* tasks[NR_TASKS]; // max support running task
extern int nr_tasks;                        // number of running tasks
extern Process* processes[NR_PROCESSES];
extern int nr_processes;

/**
 * Return the logical CPU index derived from MPIDR_EL1.
 *
 * Returns:
 *   Low-byte CPU index for the running core.
 */
static inline unsigned int task_cpu_index(void) {
    unsigned long mpidr;
    unsigned int cpu;

    // Read MPIDR_EL1 so task state can be addressed by CPU index.
    asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    cpu = (unsigned int)(mpidr & 0xFFUL);
    if (cpu >= NR_CPUS) {
        cpu = 0;
    }
    return cpu;
}

/**
 * Return the storage slot that holds the current task pointer for this CPU.
 *
 * Returns:
 *   Pointer to the per-CPU `current_task` slot.
 */
static inline struct task_struct** current_task_slot(void) {
    return &current_tasks[task_cpu_index()];
}

// Expose `current_task` as a per-CPU lvalue so existing C code can keep
// using the old identifier while reading and writing CPU-local state.
#define current_task (*current_task_slot())
#define current_process ((current_task) ? (current_task)->process : 0)

static inline long task_process_id(const struct task_struct* task) {
    return (task && task->process) ? task->process->id : -1;
}

extern void init_schedler(void);
extern void schedproc_start_housekeeper(void);

extern void schedler_schedule(void);

extern void schedler_timer_tick(void);
extern unsigned long schedler_get_ticks(void);
extern void schedler_sleep_ticks(unsigned long ticks);
extern void schedler_block_current(void);
extern int schedler_unblock_task(struct task_struct* task);

extern void schedler_switch_to(struct task_struct* next);

extern void preempt_disable(void);

extern void preempt_enable(void);

extern void cpu_switch_to(struct task_struct* prev, struct task_struct* next);
extern void scheduler_lock_acquire(void);
extern void scheduler_lock_release(void);

int copy_process(Flags clone_flags, unsigned long fn, unsigned long arg, unsigned long stack);

#define INIT_PROCESS                                                                                         \
    /* id */ 0,                                                                                             \
    /* parent_process_id */ 0,                                                                              \
    /* state */ PROCESS_ACTIVE,                                                                             \
    /* result */ 0,                                                                                         \
    /* main_thread_id */ 0,                                                                                 \
    /* thread_count */ 1,                                                                                   \
    /* main_thread */ 0,                                                                                    \
    /* name */ "kernel",                                                                                    \
    /* program_path */ {0},                                                                                 \
    /* launch_args */ {0}

#define INIT_TASK(PROCESS_PTR)                                                                               \
    /*cpu_context*/ {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},                                                \
                     /* id */ 0,                                                                             \
                     /* state */ TASK_RUNNING,                                                               \
                     0,                                                                                      \
                     1,                                                                                      \
                     0,                                                                                      \
                     0,                                                                                      \
                     PF_KTHREAD,                                                                             \
                     /* process */ PROCESS_PTR,                                                              \
                     /* mm */ {0, 0, {0}, 0, {0}, USER_HEAP_BASE, {0}, USER_SHARED_LIBRARY_LOCAL_BASE, {0}}, \
                     /* kernel_stack_page */ 0,                                                              \
                     /* name */ "KERNEL IDLE",                                                               \
                     /* thread_name */ {0},                                                                  \
                     /* wakeup_tick */ 0} // this is our kernel task

/**
 * Locate the saved register frame stored at the end of a task's kernel stack page.
 *
 * Args:
 *   tsk: Task whose trap frame is being queried.
 *
 * Returns:
 *   Pointer to the task's `pt_regs` frame.
 */
struct pt_regs* task_pt_regs(struct task_struct* tsk);

/**
 * Mark the current process as finished and schedule away from it.
 *
 * Args:
 *   result: Process exit status recorded for debugging purposes.
 *
 * Returns:
 *   Never returns in the normal case.
 */
void exit_current_process(long result);

/**
 * Reclaim memory and task slots for zombie processes.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Nothing. All reclaimable zombie resources are released.
 */
void cleanup_zombie_processes();

/**
 * Populate the current task with a flat user image copied from a raw buffer.
 *
 * Args:
 *   program_addr: Kernel virtual address of the source image.
 *   program_size: Image size in bytes.
 *
 * Returns:
 *   Nothing. On failure the current task is unloaded.
 */
void user_process_loader(Address program_addr, ulong program_size);

/**
 * Create a new task that will load and run a raw user image.
 *
 * Args:
 *   program_addr: Kernel virtual address of the source image.
 *   program_size: Image size in bytes.
 *
 * Returns:
 *   New PID on success, or `-1` when task creation fails.
 */
int create_user_process(Address program_addr, ULong program_size);

/**
 * Create a new kernel or user thread entry in the scheduler.
 *
 * Args:
 *   flags: Thread type flags such as `PF_KTHREAD`.
 *   program_addr: Entry routine or loader function.
 *   arg: Argument passed into the new thread.
 *
 * Returns:
 *   New PID on success, or `-1` when task creation fails.
 */
ulong process_copy_thread(Flags flags, Address program_addr, Pointer arg);
ulong process_create_main_thread(Flags flags, Address program_addr, Pointer arg);
ulong process_create_user_thread(Address entry_pc, Pointer arg);
Process* process_lookup(long pid);
Task* process_main_thread(long pid);

/**
 * Drop all user mappings and per-task heap metadata before loading a new image.
 *
 * Args:
 *   task: Task whose user address space should be reset.
 *
 * Returns:
 *   Nothing. Existing user pages and page tables are released.
 */
void process_reset_user_space(Task* task);
void process_dump_task_struct(Task* task);
void process_dump_task(ulong pid);
void dump_current_task();
void print_current_task_id();
void process_unload(Task* task);
int kill_task(long pid);
int process_wait(long pid, long* result);
void terminate_current_process_tree(long result);

#endif // __ASSEMBLER__
#endif // _TASK_H
