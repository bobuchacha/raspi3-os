#define LOG_ENABLE_TRACE 0

#include "device.h"
#include "log.h"
#include "memory.h"
#include "printf.h"
#include "ros.h"
#include "percpu.h"
#include "task.h"
#include "utils.h"

// entry.S
extern unsigned long start_thread_context();
extern unsigned long user_process_pre_loader();

void process_dump_task_struct(Task* task);
void copy_virtual_memory(struct task_struct* p);

// mmu.c
int process_map_page(Task* task, Address pa, Address va, Flags flags);

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

static int process_allocate_thread_slot(void) {
    for (int index = 1; index < NR_TASKS; index++) {
        if (!tasks[index]) {
            return index;
        }
    }

    return -1;
}

static Process* process_allocate_box(long pid, Process* parent) {
    Process* process = (Process*)kmalloc(sizeof(Process));

    if (!process) {
        return 0;
    }

    memzero((Address)process, sizeof(*process));
    process->id = pid;
    process->parent_process_id = parent ? parent->id : 0;
    process->state = PROCESS_ACTIVE;
    process->main_thread_id = pid;
    return process;
}

static void process_attach_thread(Task* task, Process* process) {
    task->process = process;
    if (!task->thread_name[0]) {
        task->name = (Buffer)(process && process->name[0] ? process->name : "thread");
    }

    if (process) {
        process->thread_count++;
        if (!process->main_thread) {
            process->main_thread = task;
            process->main_thread_id = task->id;
        }
    }
}

/**
 * Record one kernel-owned page in the task bookkeeping array.
 *
 * Args:
 *   task: Task that owns the page.
 *   page: Physical address of the page to track.
 *
 * Returns:
 *   `0` on success, or `-1` when the bookkeeping array is full.
 */
static int process_track_kernel_page(Task* task, Address page) {
    if (task->mm.kernel_pages_count >= MAX_PROCESS_PAGES) {
        return -1;
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

static int process_clamp_page_count(const char* label, long task_id, int count) {
    if (count < 0) {
        log_warning("Task %ld has negative %s count %d; clamping to 0", task_id, label, count);
        return 0;
    }
    if (count > MAX_PROCESS_PAGES) {
        log_warning("Task %ld has corrupt %s count %d; clamping to %d", task_id, label, count, MAX_PROCESS_PAGES);
        return MAX_PROCESS_PAGES;
    }

    return count;
}

/**
 * Drop all current user mappings and heap records from a task.
 *
 * Args:
 *   task: Task whose user address space should be cleared.
 *
 * Returns:
 *   Nothing. User pages and task-specific page tables are released.
 */
void process_reset_user_space(Task* task) {
    Address task_page = process_task_kernel_page(task);
    Address stack_page = process_task_stack_page(task);
    int user_page_count = process_clamp_page_count("user page", task ? task->id : -1, task ? task->mm.user_pages_count : 0);
    int kernel_page_count = process_clamp_page_count("kernel page", task ? task->id : -1, task ? task->mm.kernel_pages_count : 0);
    int permanent_pages = process_permanent_kernel_pages(task);

    // Free user-owned code, data, stack, and heap pages tracked in the task metadata.
    for (int index = 0; index < user_page_count; index++) {
        Address page = (Address)(task->mm.user_pages[index].phys_addr & MM_PAGE_MASK);

        if (page != 0) {
            mem_free_page(page);
        }
    }

    // Free every page-table page except the task-struct page that also carries the kernel stack.
    for (int index = permanent_pages; index < kernel_page_count; index++) {
        Address page = (Address)(task->mm.kernel_pages[index] & MM_PAGE_MASK);

        if (page != 0) {
            mem_free_page(page);
        }
    }

    // Reset bookkeeping so the next exec starts from a clean user address space.
    task->mm.user_pages_count = 0;
    task->mm.kernel_pages_count = 0;
    task->mm.pgd = 0;
    task->mm.heap_next = USER_HEAP_BASE;
    task->mm.dll_local_next = USER_SHARED_LIBRARY_LOCAL_BASE;
    memzero((Address)task->mm.user_pages, sizeof(task->mm.user_pages));
    memzero((Address)task->mm.kernel_pages, sizeof(task->mm.kernel_pages));
    if (task_page != 0) {
        task->mm.kernel_pages[task->mm.kernel_pages_count++] = task_page;
    }
    if (stack_page != 0) {
        task->mm.kernel_pages[task->mm.kernel_pages_count++] = stack_page;
    }
    task->kernel_stack_page = stack_page;
    memzero((Address)task->mm.heap_allocs, sizeof(task->mm.heap_allocs));
    memzero((Address)task->mm.dll_locals, sizeof(task->mm.dll_locals));
}

/**
 * Mark a task as a zombie and yield so another task can run.
 *
 * Args:
 *   task: Task being terminated.
 *
 * Returns:
 *   Never returns in the current execution context because scheduling occurs immediately.
 */
void process_unload(Task* task) {
    // Move the task into zombie state so the cleanup pass can reclaim its resources later.
    preempt_disable();
    scheduler_lock_acquire();
    task->state = TASK_ZOMBIE;
    if (task->process && task->process->main_thread == task) {
        task->process->state = PROCESS_ZOMBIE;
    }
    scheduler_lock_release();

    preempt_enable();

    // Yield immediately so this dead task stops executing.
    schedler_schedule();
}

/**
 * Terminate the current task with a status value.
 *
 * Args:
 *   result: Exit status used only for tracing and diagnostics.
 *
 * Returns:
 *   Never returns when the unload path succeeds.
 */
void exit_current_process(long result) {
    _trace("Process %d exitted with status 0x%lx", current_task->id, result);
    process_unload(current_task);
}

int kill_task(long pid) {
    Task* task;

    if (pid <= 0 || pid >= NR_TASKS) {
        return -1;
    }

    preempt_disable();
    task = process_main_thread(pid);
    if (!task) {
        task = tasks[pid];
    }
    if (!task || task->state == TASK_ZOMBIE) {
        preempt_enable();
        return -1;
    }

    if (task == current_task) {
        preempt_enable();
        exit_current_process(-1);
        return 0;
    }

    scheduler_lock_acquire();
    task->state = TASK_ZOMBIE;
    if (task->process && task->process->main_thread == task) {
        task->process->state = PROCESS_ZOMBIE;
    }
    scheduler_lock_release();
    preempt_enable();
    return 0;
}

/**
 * Reclaim pages and task slots that belong to zombie tasks.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Nothing. Zombie resources are released back to the global allocators.
 */
void cleanup_zombie_processes() {
    // _trace("Cleaning up...");
    preempt_disable();
    Task* t;

    // Walk the global task table and reclaim every task that has already exited.
    for (int i = 0; i < NR_TASKS; i++) {
        t = tasks[i];
        if (!t)
            continue;
        if (t->state == TASK_ZOMBIE) {
            Address kernel_pages[MAX_PROCESS_PAGES];
            Address user_pages[MAX_PROCESS_PAGES];
            int kernel_page_count = process_clamp_page_count("kernel page", t->id, t->mm.kernel_pages_count);
            int user_page_count = process_clamp_page_count("user page", t->id, t->mm.user_pages_count);

            _trace("Preparing to clean up process %d", i);

            for (int j = 0; j < kernel_page_count; j++) {
                kernel_pages[j] = process_normalize_page_address(t->mm.kernel_pages[j]);
            }
            for (int j = 0; j < user_page_count; j++) {
                user_pages[j] = process_normalize_page_address(t->mm.user_pages[j].phys_addr);
            }

            // remove kernel pages
            for (int j = kernel_page_count - 1; j >= 0; j--) {
                Address page = kernel_pages[j];

                if (page == 0) {
                    continue;
                }

                _trace("Cleaning up process %d: Freeing page 0x%lX", t->id, page);
                mem_free_page(page);
            }

            // free user pages
            for (int j = 0; j < user_page_count; j++) {
                Address page = user_pages[j];

                if (page == 0) {
                    continue;
                }

                _trace("Cleaning up process %d: Freeing user page 0x%lX", t->id, page);
                mem_free_page(page);
            }

            // unset task in the array
            scheduler_lock_acquire();
            tasks[i] = null;
            if (t->process) {
                if (t->process->thread_count > 0) {
                    t->process->thread_count--;
                }
                if (t->process->main_thread == t && t->process->id > 0 && processes[t->process->id] == t->process) {
                    processes[t->process->id] = 0;
                    nr_processes--;
                    kfree((Address)t->process);
                }
            }
            nr_tasks--;
            scheduler_lock_release();
        }
    }
    preempt_enable();
}

/**
 * Load a flat raw user image into the current task and prepare EL0 entry registers.
 *
 * Args:
 *   program_addr: Kernel virtual address of the source image.
 *   program_size: Size of the image in bytes.
 *
 * Returns:
 *   Nothing. On allocation failure the current task is unloaded.
 */
void user_process_loader(Address program_addr, ulong program_size) {
    _trace("Loading user process at 0x%lX, size: %d bytes...", program_addr, program_size);
    preempt_disable();

    // prepare our thread to switch to user
    Task* task = current_task;
    int program_code_pages = (program_size / PAGE_SIZE) + 1,
        program_stack_pages = VA_USER_STACK / PAGE_SIZE;
    int i, code_offset, copy_length, j = 0;

    task->flags = 0;
    task->cpu_context.x19 = 0;
    task->mm.pgd = 0; // get away with kernel PGD

    // reserve memory for stack
    // only need to map 1 page. As the Data Abort handler will map more page as stack grows downward
    Address s = mem_alloc_page();
    if (!s)
        return process_unload(task);

    process_map_page(task, s, (Address)(VA_USER_STACK - PAGE_SIZE), PE_USER_DATA);

    // prepare our task registers that ret_to_user will use. These info will be loaded by kernel_exit 0
    struct pt_regs* regs = task_pt_regs(task);
    regs->pstate = PSR_MODE_EL0t; // change to EL0
    regs->pc = VA_USER_START;     // our code start at this address. Period!
    regs->sp = VA_USER_STACK;     // our stack start from here, grow down ward

    // reserve memory for code. And copy user code into its memory
    for (i = program_stack_pages; program_code_pages > 0; program_code_pages--, i++, j++) {
        Address _segment = mem_alloc_page();
        if (!_segment)
            return process_unload(task);

        code_offset = j * PAGE_SIZE;
        copy_length = (program_size - (j * PAGE_SIZE)) > PAGE_SIZE ? PAGE_SIZE : (program_size % PAGE_SIZE);

        _trace("Allocating 1 page for the new process code...");

        // Map each code page into the user virtual address range reserved for the raw image.
        process_map_page(task, _segment, i * PAGE_SIZE, PE_USER_CODE);

        _trace("Copying %d bytes from 0x%lX to  0x%lX for user code...", copy_length, program_addr + code_offset, _segment + VA_START);

        // Copy the corresponding chunk from the source image into the freshly mapped code page.
        memcpy(_segment + VA_START, program_addr + code_offset, copy_length);
    }

    // set pgd

    // Activate the new page tables before returning toward the EL0 entry path.
    set_pgd(task->mm.pgd);
    preempt_enable();
    _trace("User process loaded. process id: %d. Switching to user mode...", current_task->id);
    //     while(1);
}

/**
 * Allocate and enqueue a new task that boots through the raw user-image loader.
 *
 * Args:
 *   program_addr: Kernel virtual address of the raw program image.
 *   program_size: Image size in bytes.
 *
 * Returns:
 *   New PID on success, or `-1` when task creation fails.
 */
int create_user_process(Address program_addr, ULong program_size) {
    preempt_disable();
    Task* new_task;
    Process* new_process = 0;
    int slot;

    Address task_page = mem_alloc_page();
    Address stack_page;

    if (!task_page) {
        preempt_enable();
        return -1;
    }

    stack_page = mem_alloc_page();
    if (!stack_page) {
        mem_free_page(task_page);
        preempt_enable();
        return -1;
    }

    memzero(task_page + VA_START, PAGE_SIZE);
    new_task = (struct task_struct*)(task_page + VA_START);
    new_task->kernel_stack_page = stack_page;
    slot = process_allocate_thread_slot();
    if (slot < 0) {
        mem_free_page(stack_page);
        mem_free_page(task_page);
        preempt_enable();
        return -1;
    }
    new_process = process_allocate_box(slot, current_process);
    if (!new_process) {
        mem_free_page(stack_page);
        mem_free_page(task_page);
        preempt_enable();
        return -1;
    }
    struct pt_regs* childregs = task_pt_regs(new_task);
    new_task->mm.kernel_pages_count = 0;
    new_task->mm.user_pages_count = 0;
    new_task->mm.heap_next = USER_HEAP_BASE;
    new_task->mm.dll_local_next = USER_SHARED_LIBRARY_LOCAL_BASE;
    new_task->name = (Buffer)new_task->thread_name;
    new_task->thread_name[0] = '\0';

    // Clear heap bookkeeping because this task starts with no dynamic allocations.
    memzero((Address)new_task->mm.heap_allocs, sizeof(new_task->mm.heap_allocs));
    memzero((Address)new_task->mm.dll_locals, sizeof(new_task->mm.dll_locals));
    if (process_track_kernel_page(new_task, task_page) < 0 || process_track_kernel_page(new_task, stack_page) < 0) {
        kfree((Address)new_process);
        mem_free_page(stack_page);
        mem_free_page(task_page);
        preempt_enable();
        return -1;
    }

    _trace("Forking new thread. Address: 0x%lx. SP: 0x%lX\n", (ulong)new_task, childregs);

    new_task->cpu_context.x19 = (ULong)&user_process_loader;
    new_task->cpu_context.x20 = (ULong)program_addr;
    new_task->cpu_context.x21 = program_size;
    new_task->mm.pgd = get_pgd(); // user kenel pgd for now

    // Initialize scheduler-visible task state before placing the task in the global run queue.
    new_task->flags = PF_KTHREAD;
    new_task->process = new_process;
    new_task->priority = current_task->priority;
    new_task->state = TASK_READY;
    new_task->counter = new_task->priority;
    // Keep task execution on the caller CPU while secondary context handling is stabilized.
    new_task->cpu_affinity = task_cpu_index();
    new_task->preempt_count = 1; // disable preemtion until schedule_tail

    // new_task->cpu_context.pc = (unsigned long)user_process_pre_loader;
    new_task->cpu_context.pc = (unsigned long)start_thread_context;
    new_task->cpu_context.sp = (unsigned long)childregs; // new thread's SP is right below the reg struct. This is kernel space address

    new_task->id = slot;
    scheduler_lock_acquire();
    tasks[slot] = new_task;
    processes[new_process->id] = new_process;
    nr_tasks++;
    nr_processes++;
    process_attach_thread(new_task, new_process);
    scheduler_lock_release();

    // Wake a sleeping secondary so it can notice the newly queued task promptly.
    percpu_kick_cpu(new_task->cpu_affinity);

    preempt_enable();
    return new_process->id;
}

/**
 * Allocate and enqueue a new task that starts from a supplied kernel or user entry.
 *
 * Args:
 *   flags: Thread type flags such as `PF_KTHREAD`.
 *   program_addr: Entry function for kernel threads or loader hook for user threads.
 *   arg: First argument supplied to the new thread.
 *
 * Returns:
 *   New PID on success, or `-1` when task creation fails.
 */
ulong process_create_main_thread(Flags flags, Address program_addr, Pointer arg) {
    preempt_disable();
    Task* new_task;
    Process* new_process = 0;
    int slot;

    Address task_page = mem_alloc_page();
    Address stack_page;

    if (!task_page) {
        preempt_enable();
        return -1;
    }

    stack_page = mem_alloc_page();
    if (!stack_page) {
        mem_free_page(task_page);
        preempt_enable();
        return -1;
    }

    memzero(task_page + VA_START, PAGE_SIZE);
    new_task = (struct task_struct*)(task_page + VA_START);
    new_task->kernel_stack_page = stack_page;
    slot = process_allocate_thread_slot();
    if (slot < 0) {
        mem_free_page(stack_page);
        mem_free_page(task_page);
        preempt_enable();
        return -1;
    }
    new_process = process_allocate_box(slot, current_process);
    if (!new_process) {
        mem_free_page(stack_page);
        mem_free_page(task_page);
        preempt_enable();
        return -1;
    }
    struct pt_regs* childregs = task_pt_regs(new_task);
    new_task->mm.kernel_pages_count = 0;
    new_task->mm.user_pages_count = 0;
    new_task->mm.heap_next = USER_HEAP_BASE;
    new_task->mm.dll_local_next = USER_SHARED_LIBRARY_LOCAL_BASE;
    new_task->name = (Buffer)new_task->thread_name;
    new_task->thread_name[0] = '\0';

    // Start with an empty heap allocation table for the new task.
    memzero((Address)new_task->mm.heap_allocs, sizeof(new_task->mm.heap_allocs));
    memzero((Address)new_task->mm.dll_locals, sizeof(new_task->mm.dll_locals));
    if (process_track_kernel_page(new_task, task_page) < 0 || process_track_kernel_page(new_task, stack_page) < 0) {
        kfree((Address)new_process);
        mem_free_page(stack_page);
        mem_free_page(task_page);
        preempt_enable();
        return -1;
    }

    _trace("Forking new thread. Address: 0x%lx. SP: 0x%lX\n", (ulong)new_task, childregs);

    // memzero((unsigned long)childregs, sizeof(struct pt_regs));
    // memzero((unsigned long)&p->cpu_context, sizeof(struct cpu_context));

    if (flags & PF_KTHREAD) {
        // Stash the thread entry and first argument in the callee-saved context consumed by `ret_from_fork`.
        new_task->cpu_context.x19 = (ULong)program_addr;
        new_task->cpu_context.x20 = (ULong)arg;
        new_task->mm.pgd = get_pgd();
    }
    else {
        _trace("Copying new user thread");
        kerror("Implement this!");
        // struct pt_regs * cur_regs = task_pt_regs(current_task);
        // *childregs = *cur_regs;
        // childregs->regs[0] = 0;
        // // childregs->sp = stack + PAGE_SIZE;
        // // p->stack = stack;
        // copy_virtual_memory(new_task);		// copy virtual memory from current task to the new process
    }

    new_task->flags = flags;
    new_task->process = new_process;
    new_task->priority = current_task->priority;
    new_task->state = TASK_READY;
    new_task->counter = new_task->priority;
    // Keep task execution on the caller CPU while secondary context handling is stabilized.
    new_task->cpu_affinity = task_cpu_index();
    new_task->preempt_count = 1; // disable preemtion until schedule_tail

    new_task->cpu_context.pc = (ULong)start_thread_context;
    new_task->cpu_context.sp = (ULong)childregs; // new thread's SP is right below the reg struct

    new_task->id = slot;
    scheduler_lock_acquire();
    tasks[slot] = new_task;
    processes[new_process->id] = new_process;
    nr_tasks++;
    nr_processes++;
    process_attach_thread(new_task, new_process);
    scheduler_lock_release();

    // If the task landed on a sleeping secondary, send an event so WFE exits quickly.
    percpu_kick_cpu(new_task->cpu_affinity);

    preempt_enable();
    return new_process->id;
}

ulong process_copy_thread(Flags flags, Address program_addr, Pointer arg) {
    preempt_disable();
    Task* new_task;
    Process* owner = current_process;
    int slot;

    Address task_page = mem_alloc_page();
    Address stack_page;

    if (!task_page) {
        preempt_enable();
        return -1;
    }

    stack_page = mem_alloc_page();
    if (!stack_page) {
        mem_free_page(task_page);
        preempt_enable();
        return -1;
    }

    memzero(task_page + VA_START, PAGE_SIZE);
    new_task = (struct task_struct*)(task_page + VA_START);
    new_task->kernel_stack_page = stack_page;
    slot = process_allocate_thread_slot();
    if (slot < 0) {
        mem_free_page(stack_page);
        mem_free_page(task_page);
        preempt_enable();
        return -1;
    }
    struct pt_regs* childregs = task_pt_regs(new_task);
    new_task->mm.kernel_pages_count = 0;
    new_task->mm.user_pages_count = 0;
    new_task->mm.heap_next = USER_HEAP_BASE;
    new_task->mm.dll_local_next = USER_SHARED_LIBRARY_LOCAL_BASE;
    new_task->name = (Buffer)new_task->thread_name;
    new_task->thread_name[0] = '\0';

    memzero((Address)new_task->mm.heap_allocs, sizeof(new_task->mm.heap_allocs));
    memzero((Address)new_task->mm.dll_locals, sizeof(new_task->mm.dll_locals));
    if (process_track_kernel_page(new_task, task_page) < 0 || process_track_kernel_page(new_task, stack_page) < 0) {
        mem_free_page(stack_page);
        mem_free_page(task_page);
        preempt_enable();
        return -1;
    }

    _trace("Forking new thread. Address: 0x%lx. SP: 0x%lX\n", (ulong)new_task, childregs);

    if (flags & PF_KTHREAD) {
        new_task->cpu_context.x19 = (ULong)program_addr;
        new_task->cpu_context.x20 = (ULong)arg;
        new_task->mm.pgd = get_pgd();
    }
    else {
        _trace("Copying new user thread");
        kerror("Implement this!");
    }

    new_task->flags = flags;
    new_task->process = owner;
    new_task->priority = current_task->priority;
    new_task->state = TASK_READY;
    new_task->counter = new_task->priority;
    // Keep task execution on the caller CPU while secondary context handling is stabilized.
    new_task->cpu_affinity = task_cpu_index();
    new_task->preempt_count = 1;

    new_task->cpu_context.pc = (ULong)start_thread_context;
    new_task->cpu_context.sp = (ULong)childregs;
    new_task->id = slot;

    scheduler_lock_acquire();
    tasks[slot] = new_task;
    nr_tasks++;
    process_attach_thread(new_task, owner);
    scheduler_lock_release();

    percpu_kick_cpu(new_task->cpu_affinity);
    preempt_enable();
    return slot;
}

/**
 * Print a formatted dump of one task descriptor.
 *
 * Args:
 *   task: Task to describe.
 *
 * Returns:
 *   Nothing.
 */
void process_dump_task_struct(Task* task) {
    Process* process = task ? task->process : 0;
    const char* state_name = "ZOMBIE";
    const char* state_color = "\x1b[31m";

    if (task->state == TASK_RUNNING) {
        state_name = "RUN";
        state_color = "\x1b[32m";
    }
    else if (task->state == TASK_READY) {
        state_name = "READY";
        state_color = "\x1b[36m";
    }
    else if (task->state == TASK_SLEEPING) {
        state_name = "SLEEP";
        state_color = "\x1b[33m";
    }
    else if (task->state == TASK_BLOCKED) {
        state_name = "BLOCK";
        state_color = "\x1b[35m";
    }

    kprint("\x1b[2;37m-------------------------------------------------------------------------------\x1b[0m\n");
    kprint("\x1b[1;36mThread Details\x1b[0m\n");
    kprint("\x1b[2;37m-------------------------------------------------------------------------------\x1b[0m\n");
    kprint("  \x1b[1;34mthread        \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%d\x1b[0m\n", task->id);
    kprint("  \x1b[1;34mprocess       \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%ld\x1b[0m\n", task_process_id(task));
    kprint("  \x1b[1;34mparent proc   \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%ld\x1b[0m\n", process ? process->parent_process_id : -1L);
    kprint("  \x1b[1;34mname          \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%s\x1b[0m\n", task->name ? (char*)task->name : "<none>");
    kprint("  \x1b[1;34mprogram path  \x1b[0m \x1b[2;37m|\x1b[0m \x1b[36m%s\x1b[0m\n", process && process->program_path[0] ? process->program_path : "<none>");
    kprint("  \x1b[1;34mlaunch args   \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%s\x1b[0m\n", process && process->launch_args[0] ? process->launch_args : "<none>");
    kprint("  \x1b[1;34mthreads       \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%ld\x1b[0m\n", process ? process->thread_count : 0L);
    kprint("  \x1b[1;34mstate         \x1b[0m \x1b[2;37m|\x1b[0m %s%s\x1b[0m\n", state_color, state_name);
    kprint("  \x1b[1;34mcounter       \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%d\x1b[0m\n", task->counter);
    kprint("  \x1b[1;34mpriority      \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%d\x1b[0m\n", task->priority);
    kprint("  \x1b[1;34mcpu           \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%u\x1b[0m\n", task->cpu_affinity);
    kprint("  \x1b[1;34mpreempt       \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%d\x1b[0m\n", task->preempt_count);
    kprint("  \x1b[1;34mflags         \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m0x%X\x1b[0m\n", task->flags);
    kprint("\x1b[2;37m-------------------------------------------------------------------------------\x1b[0m\n");
    kprint("\x1b[1;36mMemory\x1b[0m\n");
    kprint("\x1b[2;37m-------------------------------------------------------------------------------\x1b[0m\n");
    kprint("  \x1b[1;34mpgd           \x1b[0m \x1b[2;37m|\x1b[0m \x1b[36m0x%lX\x1b[0m\n", task->mm.pgd);
    kprint("  \x1b[1;34mheap next     \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m0x%lX\x1b[0m\n", task->mm.heap_next);
    kprint("  \x1b[1;34mdll next      \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m0x%lX\x1b[0m\n", task->mm.dll_local_next);
    kprint("  \x1b[1;34muser pages    \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%d\x1b[0m\n", task->mm.user_pages_count);
    kprint("  \x1b[1;34mkernel pages  \x1b[0m \x1b[2;37m|\x1b[0m \x1b[97m%d\x1b[0m\n", task->mm.kernel_pages_count);
    kprint("  \x1b[1;34mkstack page   \x1b[0m \x1b[2;37m|\x1b[0m \x1b[36m0x%lX\x1b[0m\n", task->kernel_stack_page);
    kprint("\x1b[2;37m-------------------------------------------------------------------------------\x1b[0m\n");
    kprint("\x1b[1;36mRegisters\x1b[0m\n");
    kprint("\x1b[2;37m-------------------------------------------------------------------------------\x1b[0m\n");
    kprint("  \x1b[1;34mx19   \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mx20   \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mx21   \x1b[0m \x1b[36m0x%016lX\x1b[0m\n", task->cpu_context.x19, task->cpu_context.x20, task->cpu_context.x21);
    kprint("  \x1b[1;34mx22   \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mx23   \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mx24   \x1b[0m \x1b[36m0x%016lX\x1b[0m\n", task->cpu_context.x22, task->cpu_context.x23, task->cpu_context.x24);
    kprint("  \x1b[1;34mx25   \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mx26   \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mx27   \x1b[0m \x1b[36m0x%016lX\x1b[0m\n", task->cpu_context.x25, task->cpu_context.x26, task->cpu_context.x27);
    kprint("  \x1b[1;34mx28   \x1b[0m \x1b[36m0x%016lX\x1b[0m\n", task->cpu_context.x28);
    kprint("  \x1b[1;34msp    \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mpc    \x1b[0m \x1b[36m0x%016lX\x1b[0m    \x1b[1;34mfp    \x1b[0m \x1b[36m0x%016lX\x1b[0m\n", task->cpu_context.sp, task->cpu_context.pc, task->cpu_context.fp);
    kprint("\x1b[2;37m-------------------------------------------------------------------------------\x1b[0m\n");
}

/**
 * Dump one task by PID.
 *
 * Args:
 *   pid: Task id to inspect.
 *
 * Returns:
 *   Nothing.
 */
void process_dump_task(ulong pid) {
    process_dump_task_struct(tasks[pid]);
}

/**
 * Dump the currently running task.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Nothing.
 */
void dump_current_task() {
    process_dump_task_struct(current_task);
}

/**
 * Print the current task identifier.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Nothing.
 */
void print_current_task_id() {
    printf("==> CURRENT TASK ID: %d\n\n", current_task->id);
}

/**
 * Compute the `pt_regs` frame address stored at the end of a task page.
 *
 * Args:
 *   tsk: Task whose saved register frame is requested.
 *
 * Returns:
 *   Pointer to that task's trap frame.
 */
struct pt_regs* task_pt_regs(struct task_struct* tsk) {
    Address stack_page = tsk ? process_task_stack_page(tsk) : 0;
    unsigned long p = stack_page ? (unsigned long)(stack_page + VA_START + PAGE_SIZE - sizeof(struct pt_regs))
        : (unsigned long)tsk + THREAD_SIZE - sizeof(struct pt_regs);
    return (struct pt_regs*)p;
}
