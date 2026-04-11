/*
 * service-call.c
 *
 * EL0 syscall implementations for memory management, process lifecycle,
 * filesystem access, shell/debug helpers, and module loading operations.
 */
#include "arch/cortex-a53/mmu.h"
#include "device.h"
#include "filesystem/vfs/vfs.h"
#include "gui.h"
#include "input.h"
#include "irq.h" // some code in device irq or arch irq
#include "log.h"
#include "memory.h"
#include "platform/cpu/debug_uart.h"
#include "printf.h"
#include "module.h"
#include "syscall.h"
#include "task.h"
#include "timer.h"
#include "user-exe.h"
#include "utils.h"

int kernel_shell_execute_command(const char* line);

#define USER_SLEEP_TICK_MSEC 500
#define USER_LOG_LINE_MAX 512U
#define USER_LOG_QUEUE_DEPTH 64U

typedef struct UserLogSlot {
    Bool in_use;
    char text[USER_LOG_LINE_MAX];
} UserLogSlot;

static UserLogSlot g_user_log_queue[USER_LOG_QUEUE_DEPTH];
static unsigned int g_user_log_head = 0U;
static unsigned int g_user_log_tail = 0U;
static unsigned int g_user_log_count = 0U;
static long g_user_log_receiver_pid = -1;
static long g_gui_input_owner_pid = -1;
static unsigned int g_sys_gui_scanline[4096];

static int sys_text_equals(const char* lhs, const char* rhs) {
    if (!lhs || !rhs) {
        return 0;
    }

    while (*lhs != '\0' && *rhs != '\0' && *lhs == *rhs) {
        lhs++;
        rhs++;
    }

    return *lhs == *rhs;
}

static unsigned long sys_text_length(const char* text) {
    unsigned long length = 0;

    if (!text) {
        return 0;
    }

    while (text[length] != '\0') {
        length++;
    }

    return length;
}

static int sys_process_is_active(long pid) {
    Process* process;

    if (pid < 0 || pid >= NR_PROCESSES) {
        return 0;
    }

    process = processes[pid];
    return process && process->state == PROCESS_ACTIVE;
}

static int sys_current_process_is_core(void) {
    return current_process &&
        (sys_text_equals(current_process->program_path, "/bin/core.exe") ||
            sys_text_equals(current_process->name, "core"));
}

static void sys_user_log_queue_reset(void) {
    g_user_log_head = 0U;
    g_user_log_tail = 0U;
    g_user_log_count = 0U;
    for (unsigned int index = 0U; index < USER_LOG_QUEUE_DEPTH; index++) {
        g_user_log_queue[index].in_use = false;
        g_user_log_queue[index].text[0] = '\0';
    }
}

static void sys_user_log_queue_reset_if_stale(void) {
    if (g_user_log_receiver_pid >= 0 && !sys_process_is_active(g_user_log_receiver_pid)) {
        g_user_log_receiver_pid = -1;
        sys_user_log_queue_reset();
    }
}

static void sys_gui_input_owner_reset_if_stale(void) {
    if (g_gui_input_owner_pid >= 0 && !sys_process_is_active(g_gui_input_owner_pid)) {
        g_gui_input_owner_pid = -1;
    }
}

static void sys_gui_shared_input_cleanup_stale_consumers(void) {
    GuiSharedInputRegion* region = gui_shared_input_region();

    if (!region) {
        return;
    }

    for (unsigned int index = 0U; index < GUI_SHARED_INPUT_MAX_CONSUMERS; index++) {
        GuiSharedInputConsumer* consumer = &region->consumers[index];

        if (consumer->pid != 0U && !sys_process_is_active((long)consumer->pid)) {
            (void)gui_shared_input_release_consumer((unsigned long)consumer->pid);
        }
    }
}

static int sys_gui_shared_input_is_mapped(Task* task) {
    MmuWalkResult walk;

    if (!task || !task->mm.pgd) {
        return 0;
    }

    return process_walk_page(task, USER_SHARED_INPUT_VIEW_BASE, &walk) == 0;
}

static long sys_gui_shared_input_map_current_task(void) {
    unsigned int page_count;
    unsigned int mapped_pages[4] = { 0U, 0U, 0U, 0U };

    if (!current_task) {
        return -1;
    }

    page_count = gui_shared_input_page_count();
    if (page_count > 4U) {
        return -1;
    }
    for (unsigned int index = 0U; index < page_count; index++) {
        Address page_va = USER_SHARED_INPUT_VIEW_BASE + ((Address)index * PAGE_SIZE);
        Address page_pa;
        MmuWalkResult walk;

        if (process_walk_page(current_task, page_va, &walk) == 0) {
            continue;
        }

        page_pa = gui_shared_input_page_phys(index);
        if (!page_pa || process_map_shared_page(current_task, page_pa, page_va, PE_USER_DATA) != 0) {
            for (unsigned int rollback = 0U; rollback < page_count; rollback++) {
                if (mapped_pages[rollback] != 0U) {
                    (void)process_unmap_page(current_task, USER_SHARED_INPUT_VIEW_BASE + ((Address)rollback * PAGE_SIZE));
                }
            }
            if (current_task->mm.pgd) {
                set_pgd(current_task->mm.pgd);
            }
            return -1;
        }

        mapped_pages[index] = 1U;
    }

    if (current_task->mm.pgd) {
        set_pgd(current_task->mm.pgd);
    }
    return 0;
}

static long sys_gui_shared_input_unmap_current_task(void) {
    unsigned int page_count;
    int unmapped_any = 0;

    if (!current_task || !current_task->mm.pgd) {
        return -1;
    }

    page_count = gui_shared_input_page_count();
    for (unsigned int index = 0U; index < page_count; index++) {
        Address page_va = USER_SHARED_INPUT_VIEW_BASE + ((Address)index * PAGE_SIZE);
        MmuWalkResult walk;

        if (process_walk_page(current_task, page_va, &walk) == 0) {
            if (process_unmap_page(current_task, page_va) == 0) {
                unmapped_any = 1;
            }
        }
    }

    if (unmapped_any) {
        set_pgd(current_task->mm.pgd);
    }
    return 0;
}

static int sys_gui_shared_input_fill_view_for_pid(unsigned long pid, GuiSharedInputView* view) {
    GuiSharedInputRegion* region = gui_shared_input_region();

    if (!region || !view || pid == 0UL) {
        return -1;
    }

    for (unsigned int index = 0U; index < GUI_SHARED_INPUT_MAX_CONSUMERS; index++) {
        GuiSharedInputConsumer* consumer = &region->consumers[index];

        if (consumer->pid == (unsigned int)pid) {
            view->version = GUI_SHARED_INPUT_VERSION;
            view->flags = consumer->flags;
            view->view_address = USER_SHARED_INPUT_VIEW_BASE;
            view->view_size = sizeof(GuiSharedInputRegion);
            view->consumer_index = index;
            view->initial_head_sequence = consumer->head_sequence;
            return 0;
        }
    }

    return -1;
}

static long sys_gui_shared_input_acquire_current_process(unsigned long user_view_address) {
    GuiSharedInputView view;

    if (!user_view_address
        || !current_process
        || !sys_process_is_active(current_process->id)) {
        log_error("[sys_gui_shared_input_acquire_current_process] invalid arguments: user_view_address=0x%lx current_process=%p active=%d\n",
            user_view_address, current_process, sys_process_is_active(current_process ? current_process->id : 0UL));

        return -1;
    }

    _trace("[sys_gui_shared_input_acquire_current_process] acquiring shared input for pid=%lu\n", current_process->id);
    sys_gui_shared_input_cleanup_stale_consumers();
    if (gui_shared_input_attach_consumer((unsigned long)current_process->id, &view) != 0) {
        return -1;
    }

    _trace("[sys_gui_shared_input_acquire_current_process] attached consumer for pid=%lu\n", current_process->id);
    if (!sys_gui_shared_input_is_mapped(current_task) && sys_gui_shared_input_map_current_task() != 0) {
        log_error("[sys_gui_shared_input_acquire_current_process] failed to map shared input for pid=%lu\n", current_process->id);
        (void)gui_shared_input_release_consumer((unsigned long)current_process->id);
        return -1;
    }

    _trace("[sys_gui_shared_input_acquire_current_process] copying shared input view to user space for pid=%lu at user address=0x%lx\n", current_process->id, user_view_address);
    if (process_copy_to_user(current_task, (Address)user_view_address, &view, sizeof(view)) != 0) {
        log_error("[sys_gui_shared_input_acquire_current_process] failed to copy shared input view to user space for pid=%lu at user address=0x%lx\n", current_process->id, user_view_address);
        (void)sys_gui_shared_input_unmap_current_task();
        (void)gui_shared_input_release_consumer((unsigned long)current_process->id);
        return -1;
    }

    return 0;
}

static long sys_gui_shared_input_release_current_process(void) {
    if (!current_process || !sys_process_is_active(current_process->id)) {
        return -1;
    }

    (void)sys_gui_shared_input_unmap_current_task();
    return gui_shared_input_release_consumer((unsigned long)current_process->id);
}

static long sys_gui_shared_input_query_current_process(unsigned long user_view_address) {
    GuiSharedInputView view;

    if (!user_view_address || !current_process || !sys_process_is_active(current_process->id)) {
        return -1;
    }

    sys_gui_shared_input_cleanup_stale_consumers();
    if (sys_gui_shared_input_fill_view_for_pid((unsigned long)current_process->id, &view) != 0) {
        return -1;
    }
    if (process_copy_to_user(current_task, (Address)user_view_address, &view, sizeof(view)) != 0) {
        return -1;
    }

    return 0;
}

static long sys_gui_input_acquire_current_process(void) {
    sys_gui_input_owner_reset_if_stale();
    if (!current_process || !sys_process_is_active(current_process->id)) {
        return -1;
    }

    g_gui_input_owner_pid = current_process->id;
    return 0;
}

static int sys_gui_input_is_owned_by_current_process(void) {
    sys_gui_input_owner_reset_if_stale();
    if (!current_process || !sys_process_is_active(current_process->id)) {
        return 0;
    }
    if (g_gui_input_owner_pid < 0) {
        g_gui_input_owner_pid = current_process->id;
    }

    return g_gui_input_owner_pid == current_process->id;
}

static void sys_gui_input_event_clear(GuiInputEvent* event) {
    if (!event) {
        return;
    }

    event->version = GUI_INPUT_EVENT_VERSION;
    event->type = GUI_INPUT_EVENT_NONE;
    event->x = 0U;
    event->y = 0U;
    event->key = 0U;
    event->buttons = 0U;
    event->reserved = 0U;
}

/**
 * Find the bookkeeping slot that owns a given user heap allocation.
 *
 * Args:
 *   task: Task whose heap table should be searched.
 *   addr: Allocation base address to look up.
 *
 * Returns:
 *   Slot index on success, or `-1` when the address is unknown.
 */
static int sys_find_heap_slot(Task* task, Address addr) {
    // Search the per-task heap table for an allocation whose base matches the requested address.
    for (int index = 0; index < MAX_USER_HEAP_ALLOCS; index++) {
        if (task->mm.heap_allocs[index].virt_addr == addr) {
            return index;
        }
    }

    return -1;
}

/**
 * Find an unused heap bookkeeping slot for a new allocation.
 *
 * Args:
 *   task: Task whose heap table should be searched.
 *
 * Returns:
 *   Free slot index on success, or `-1` when the heap table is full.
 */
static int sys_find_free_heap_slot(Task* task) {
    // Scan for the first empty allocation record so a new heap range can be tracked.
    for (int index = 0; index < MAX_USER_HEAP_ALLOCS; index++) {
        if (task->mm.heap_allocs[index].virt_addr == 0) {
            return index;
        }
    }

    return -1;
}

/*
 * Safely copy one user-provided C string into kernel memory with an explicit
 * size cap and per-byte address validation.
 */
static int sys_copy_user_cstring(const char* user_text, char* kernel_text, unsigned long kernel_size) {
    if (!user_text || !kernel_text || kernel_size == 0) {
        return -1;
    }

    for (unsigned long index = 0; index < kernel_size; index++) {
        char ch;

        if (process_copy_from_user(0, (Address)user_text + index, &ch, sizeof(ch)) != 0) {
            kernel_text[0] = '\0';
            return -1;
        }

        kernel_text[index] = ch;
        if (ch == '\0') {
            return 0;
        }
    }

    kernel_text[kernel_size - 1] = '\0';
    return -1;
}

/* Copy a fixed-size user buffer into kernel storage after validating access. */
static int sys_copy_user_buffer(const void* user_buffer, void* kernel_buffer, unsigned long size) {
    if (!user_buffer || !kernel_buffer || size == 0UL) {
        return -1;
    }

    return process_copy_from_user(0, (Address)user_buffer, kernel_buffer, size);
}

/**
 * Kernel implementation of the write syscall.
 *
 * Args:
 *   buf: User-space string buffer to print.
 *
 * Returns:
 *   Nothing. The string is emitted to the kernel console.
 */
void sys_write(char* buf) {
    char text[512];

    if (!buf) {
        return;
    }
    if (sys_copy_user_cstring(buf, text, sizeof(text)) != 0) {
        return;
    }

    console_lock();
    printf("%s", text);
    console_unlock();
}

long sys_log_send(const char* text) {
    char message[USER_LOG_LINE_MAX];

    if (!text) {
        return -1;
    }
    if (sys_copy_user_cstring(text, message, sizeof(message)) != 0 || message[0] == '\0') {
        return -1;
    }

    console_lock();
    sys_user_log_queue_reset_if_stale();
    if (g_user_log_receiver_pid < 0 || g_user_log_count >= USER_LOG_QUEUE_DEPTH) {
        console_unlock();
        return -1;
    }

    strncpy(g_user_log_queue[g_user_log_tail].text, message, sizeof(g_user_log_queue[g_user_log_tail].text) - 1U);
    g_user_log_queue[g_user_log_tail].text[sizeof(g_user_log_queue[g_user_log_tail].text) - 1U] = '\0';
    g_user_log_queue[g_user_log_tail].in_use = true;
    g_user_log_tail = (g_user_log_tail + 1U) % USER_LOG_QUEUE_DEPTH;
    g_user_log_count++;
    console_unlock();
    return 0;
}

long sys_log_recv(char* buffer, unsigned long size) {
    UserLogSlot slot;
    unsigned long length;

    if (!buffer || size == 0UL || !sys_current_process_is_core()) {
        return -1;
    }

    console_lock();
    g_user_log_receiver_pid = current_process->id;
    sys_user_log_queue_reset_if_stale();
    if (g_user_log_count == 0U) {
        console_unlock();
        return 0;
    }

    slot = g_user_log_queue[g_user_log_head];
    g_user_log_queue[g_user_log_head].in_use = false;
    g_user_log_queue[g_user_log_head].text[0] = '\0';
    g_user_log_head = (g_user_log_head + 1U) % USER_LOG_QUEUE_DEPTH;
    g_user_log_count--;
    console_unlock();

    if (!slot.in_use) {
        return 0;
    }

    length = sys_text_length(slot.text);
    if (length + 1UL > size) {
        return -1;
    }
    if (process_copy_to_user(current_task, (Address)buffer, slot.text, length + 1UL) != 0) {
        return -1;
    }

    return (long)length;
}

long sys_log_write(const char* text) {
    char message[USER_LOG_LINE_MAX];

    if (!text) {
        return -1;
    }
    if (sys_copy_user_cstring(text, message, sizeof(message)) != 0 || message[0] == '\0') {
        return -1;
    }

    console_lock();
    printf("%s", message);
    console_unlock();
    return 0;
}

/**
 * Placeholder implementation for clone/fork support.
 *
 * Args:
 *   stack: Reserved stack argument for future fork/clone semantics.
 *
 * Returns:
 *   Currently always `-1` because clone is not implemented yet.
 */
int sys_clone(unsigned long stack) {
    (void)stack;
    return -1;
}

/**
 * Allocate page-backed heap memory in the current user task.
 *
 * Args:
 *   x: Requested allocation size in bytes.
 *
 * Returns:
 *   Base user virtual address on success, or `0` when allocation fails.
 */
unsigned long sys_malloc(ulong x) {
    Task* task = current_task;
    ULong size = x ? x : 1;
    ULong page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    Address base = task->mm.heap_next ? task->mm.heap_next : USER_HEAP_BASE;
    Address end = base + (page_count * PAGE_SIZE);
    int slot = sys_find_free_heap_slot(task);

    if (slot < 0 || end > USER_HEAP_LIMIT) {
        return 0;
    }
    if (page_count > (ULong)(MAX_PROCESS_PAGES - task->mm.user_pages_count)) {
        return 0;
    }

    // Map enough user pages to cover the request and roll back partially completed work on failure.
    for (ULong index = 0; index < page_count; index++) {
        Address page = mem_alloc_page();
        if (!page) {
            for (ULong rollback = 0; rollback < index; rollback++) {
                process_unmap_page(task, base + (rollback * PAGE_SIZE));
            }
            if (task->mm.pgd) {
                set_pgd(task->mm.pgd);
            }
            return 0;
        }

        if (process_map_page(task, page, base + (index * PAGE_SIZE), PE_USER_DATA) != 0) {
            mem_free_page(page);
            for (ULong rollback = 0; rollback < index; rollback++) {
                process_unmap_page(task, base + (rollback * PAGE_SIZE));
            }
            if (task->mm.pgd) {
                set_pgd(task->mm.pgd);
            }
            return 0;
        }
    }

    // Record the allocation so later `free` calls can unmap the same range safely.
    task->mm.heap_allocs[slot].virt_addr = base;
    task->mm.heap_allocs[slot].page_count = page_count;
    task->mm.heap_next = end;

    // Re-activate the task's page tables so the new mapping is visible immediately.
    set_pgd(task->mm.pgd);
    return base;
}

/**
 * Release a user heap allocation owned by the current task.
 *
 * Args:
 *   ptr: Base pointer previously returned by `sys_malloc`.
 *
 * Returns:
 *   Nothing. Unknown pointers are ignored.
 */
void sys_free(void* ptr) {
    Address addr = (Address)ptr;
    Task* task = current_task;
    int slot = sys_find_heap_slot(task, addr);

    if (slot < 0) {
        return;
    }

    // Unmap each page in the allocation so both the address-space entry and the backing page are released.
    for (ULong index = 0; index < task->mm.heap_allocs[slot].page_count; index++) {
        process_unmap_page(task, addr + (index * PAGE_SIZE));
    }

    // Clear the bookkeeping entry now that the allocation no longer exists.
    task->mm.heap_allocs[slot].virt_addr = 0;
    task->mm.heap_allocs[slot].page_count = 0;

    // Re-install the task's page tables so the CPU sees the updated mappings.
    set_pgd(task->mm.pgd);
}

/**
 * Terminate the current process.
 *
 * Args:
 *   result: Exit status supplied by the user program.
 *
 * Returns:
 *   Never returns when the process exits successfully.
 */
void sys_exit(ULong result) {
    // Forward process teardown to the shared process subsystem.
    exit_current_process(result);
}

/**
 * Return a small diagnostic parameter value from the kernel.
 *
 * Args:
 *   param: Selector chosen by the caller.
 *
 * Returns:
 *   Currently always `0xDEAD`.
 */
int sys_get_param(int param) {
    (void)param;
    // if (param == 0) return 0xDEAD;
    return 0xDEAD;
}

/**
 * Put the current task to sleep for approximately the requested wall-clock interval.
 *
 * Args:
 *   msec: Sleep duration in milliseconds.
 *
 * Returns:
 *   `0` after wakeup.
 */
long sys_sleep(unsigned long msec) {
    // Convert milliseconds into scheduler ticks using the current timer granularity.
    unsigned long ticks = (msec + USER_SLEEP_TICK_MSEC - 1) / USER_SLEEP_TICK_MSEC;
    if (ticks == 0) {
        ticks = 1;
    }

    // Yield through the scheduler so another runnable task can execute while we sleep.
    schedler_sleep_ticks(ticks);
    return 0;
}

/**
 * Copy the current task's user-visible name into a caller-supplied user buffer.
 *
 * Args:
 *   buf: Destination user buffer.
 *   size: Buffer size in bytes.
 *
 * Returns:
 *   Number of bytes copied excluding the trailing NUL, or `-1` on invalid input.
 */
long sys_task_name(char* buf, unsigned long size) {
    const char* name;
    unsigned long count = 0;

    _trace("Getting task %d name", current_task->id);

    if (!buf || size == 0) {
        return -1;
    }

    name = (current_process && current_process->name[0] != '\0') ? current_process->name
        : (current_task->thread_name[0] != '\0' ? current_task->thread_name : "user");
    _trace("Task %d name is '%s'", current_task->id, name);
    while (count + 1 < size && name[count] != '\0') {
        buf[count] = name[count];
        count++;
    }
    buf[count] = '\0';
    return (long)count;
}

long sys_task_args(char* buf, unsigned long size) {
    const char* args;
    unsigned long count = 0;

    if (!buf || size == 0) {
        return -1;
    }

    args = (current_process && current_process->launch_args[0] != '\0') ? current_process->launch_args : "";
    while (count + 1 < size && args[count] != '\0') {
        buf[count] = args[count];
        count++;
    }
    buf[count] = '\0';
    return (long)count;
}

/**
 * Start a new user task from a program path and attach a small user-visible name to it.
 *
 * Args:
 *   path: Absolute VFS path to the executable.
 *   name: Small name string visible to the child task.
 *
 * Returns:
 *   Child PID on success, or `-1` when the spawn fails.
 */
long sys_spawn(const char* path, const char* name, const char* args) {
    char path_copy[USER_EXEC_PATH_MAX];
    char name_copy[TASK_USER_NAME_MAX];
    char args_copy[TASK_USER_LAUNCH_ARGS_MAX];
    const char* resolved_name = null;
    const char* resolved_args = null;

    if (!path || path[0] == '\0') {
        return -1;
    }

    if (sys_copy_user_cstring(path, path_copy, sizeof(path_copy)) != 0 || path_copy[0] == '\0') {
        return -1;
    }
    if (name && name[0] != '\0') {
        if (sys_copy_user_cstring(name, name_copy, sizeof(name_copy)) != 0) {
            return -1;
        }
        resolved_name = name_copy;
    }
    if (args && args[0] != '\0') {
        if (sys_copy_user_cstring(args, args_copy, sizeof(args_copy)) != 0) {
            return -1;
        }
        resolved_args = args_copy;
    }

    _trace("sys_spawn: Spawning program '%s' with name '%s' and args '%s'", path_copy, resolved_name ? resolved_name : "<null>", resolved_args ? resolved_args : "<null>");
    return spawn_user_program(path_copy, resolved_name, resolved_args);
}

/**
 * Replace the current process image with another executable file from the VFS.
 *
 * Args:
 *   path: Absolute path to the executable file to execute.
 *
 * Returns:
 *   `0` on success, or `-1` when the path is invalid or the executable cannot be loaded.
 */
long sys_exec(const char* path) {
    char path_copy[USER_EXEC_PATH_MAX];

    if (!path || path[0] == '\0') {
        return -1;
    }
    if (sys_copy_user_cstring(path, path_copy, sizeof(path_copy)) != 0 || path_copy[0] == '\0') {
        return -1;
    }

    // Hand control to the executable loader, which replaces the current task image in place.
    if (exec_user_program(path_copy) != 0) {
        log_error("Unable to exec %s", path_copy);

        // Kill the task when exec fails so the system does not continue in a half-reset state.
        exit_current_process(-1);
        return -1;
    }

    return 0;
}

/**
 * Map a fixed-base shared library into the current process and return its entry symbol.
 *
 * Args:
 *   path: Absolute path to the packed shared library file.
 *
 * Returns:
 *   Entry-point virtual address on success, or `0` when the path is invalid or the library cannot be loaded.
 */
unsigned long sys_shlib_open(const char* path) {
    char path_copy[USER_SHARED_LIBRARY_PATH_MAX];

    if (!path || path[0] == '\0') {
        return 0;
    }
    if (sys_copy_user_cstring(path, path_copy, sizeof(path_copy)) != 0 || path_copy[0] == '\0') {
        return 0;
    }

    // Load the shared image into the global cache and map its pages into the current task.
    _trace("sys_shlib_open: Loading shared library '%s'", path_copy);
    Address entry = load_user_shared_library(path_copy);
    if (!entry) {
        return 0;
    }

    return (unsigned long)entry;
}

unsigned long sys_shlib_export(const char* path, const char* export_name) {
    Address address;
    char path_copy[USER_SHARED_LIBRARY_PATH_MAX];
    char export_copy[USER_SHARED_LIBRARY_EXPORT_NAME_MAX];

    if (!path || path[0] == '\0' || !export_name || export_name[0] == '\0') {
        return 0;
    }
    if (sys_copy_user_cstring(path, path_copy, sizeof(path_copy)) != 0 || path_copy[0] == '\0') {
        return 0;
    }
    if (sys_copy_user_cstring(export_name, export_copy, sizeof(export_copy)) != 0 || export_copy[0] == '\0') {
        return 0;
    }

    address = load_user_shared_library_export(path_copy, export_copy);
    return address;
}

/**
 * Drop one explicit shared-library reference owned by the current process.
 *
 * Args:
 *   path: Absolute path to the packed shared library or driver image.
 *
 * Returns:
 *   `0` when the reference was released, or `-1` on invalid input or when the
 *   module was not currently loaded by the caller.
 */
long sys_shlib_close(const char* path) {
    char path_copy[USER_SHARED_LIBRARY_PATH_MAX];

    if (!path || path[0] == '\0') {
        return -1;
    }
    if (sys_copy_user_cstring(path, path_copy, sizeof(path_copy)) != 0 || path_copy[0] == '\0') {
        return -1;
    }

    return unload_user_shared_library(path_copy);
}

/**
 * Explicitly unload one driver image owned by the current process.
 *
 * Args:
 *   path: Absolute path to the `.sys` image.
 *
 * Returns:
 *   `0` when the driver reference was released, or `-1` on invalid input.
 */
long sys_driver_unload(const char* path) {
    char path_copy[USER_SHARED_LIBRARY_PATH_MAX];

    if (!path || path[0] == '\0') {
        return -1;
    }
    if (sys_copy_user_cstring(path, path_copy, sizeof(path_copy)) != 0 || path_copy[0] == '\0') {
        return -1;
    }

    return unload_user_driver(path_copy);
}

/**
 * Allocate or return a per-task DLL-local storage block associated with one shared library.
 *
 * Args:
 *   path: Absolute path to the packed shared library file.
 *   size: Minimum required byte size for the task-local block.
 *
 * Returns:
 *   User virtual address of the task-local block on success, or `0` on invalid input or allocation failure.
 */
unsigned long sys_shlib_local(const char* path, unsigned long size) {
    char path_copy[USER_SHARED_LIBRARY_PATH_MAX];

    if (!path || path[0] == '\0' || size == 0) {
        return 0;
    }
    if (sys_copy_user_cstring(path, path_copy, sizeof(path_copy)) != 0 || path_copy[0] == '\0') {
        return 0;
    }

    return load_user_shared_library_local(path_copy, size);
}

long sys_console_read(void) {
    return input_read_key();
}

long sys_read_file(const char* path, unsigned long offset, char* buf, unsigned long size) {
    struct FileDesc fd;
    long read;

    if (!path || !buf || size == 0) {
        return -1;
    }
    if (vfs_fd_open(&fd, path, O_READ) != SUCCESS) {
        return ERROR_NOT_EXIST;
    }
    if (offset >= fd.size) {
        vfs_fd_close(&fd);
        return 0;
    }
    if (offset > 0 && vfs_fd_seek(&fd, offset, SEEK_SET) < 0) {
        vfs_fd_close(&fd);
        return ERROR_INVAILD;
    }

    read = vfs_fd_read(&fd, buf, size);
    vfs_fd_close(&fd);
    return read;
}

long sys_dir_entry(const char* path, unsigned long entry_index, UserDirectoryEntry* entry) {
    struct FileDesc dir;
    struct DirectoryEntry dir_entry;

    if (!path || !entry) {
        return -1;
    }
    if (vfs_dir_open(&dir, path) != SUCCESS) {
        return ERROR_NOT_EXIST;
    }

    memzero((Address)entry, sizeof(*entry));
    for (unsigned long index = 0;; index++) {
        UByte* name_utf8 = null;
        int status = vfs_dir_read_ex(&dir, &dir_entry);

        if (status <= 0) {
            vfs_dir_close(&dir);
            return status < 0 ? status : 0;
        }
        if (index != entry_index) {
            continue;
        }
        if (str_from_utf16(dir_entry.long_name, 255, &name_utf8) != 0) {
            vfs_dir_close(&dir);
            return ERROR_READ_FAIL;
        }

        strncpy(entry->name, (char*)name_utf8, sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';
        entry->size = dir_entry.size;
        entry->attr = dir_entry.attr;
        kfree((Address)name_utf8);
        vfs_dir_close(&dir);
        return 1;
    }
}

long sys_mkdir(const char* path) {
    if (!path || path[0] == '\0') {
        return -1;
    }

    return vfs_mkdir(path);
}

long sys_task_info(long pid, UserTaskInfo* info) {
    Process* process;
    Task* task;
    const char* name;

    if (!info || pid < 0 || pid >= NR_TASKS) {
        return -1;
    }

    process = process_lookup(pid);
    if (!process) {
        return 0;
    }
    task = process->main_thread;
    if (!task) {
        return 0;
    }

    memzero((Address)info, sizeof(*info));
    info->id = process->id;
    info->thread_id = task->id;
    info->parent_process_id = process->parent_process_id;
    info->state = task->state;
    info->exit_code = process->result;
    info->counter = task->counter;
    info->priority = task->priority;
    info->flags = task->flags;

    name = process->name[0] != '\0' ? process->name : (task->name ? (char*)task->name : "");
    strncpy(info->name, name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    return 1;
}

long sys_wait_pid(long pid, long* result) {
    return process_wait(pid, result);
}

long sys_mem_info(UserMemInfo* info) {
    if (!info) {
        return -1;
    }

    info->total_bytes = mem_get_size();
    info->page_size = PAGE_SIZE;
    info->free_pages = mem_get_free_pages();
    info->free_bytes = info->free_pages * PAGE_SIZE;
    return 0;
}

long sys_kill(long pid) {
    return kill_task(pid);
}

void sys_reboot(void) {
    device_reboot();
}

long sys_debug_shell(const char* line) {
    if (!line || line[0] == '\0') {
        return -1;
    }

    return kernel_shell_execute_command(line);
}

long sys_gui_control(unsigned long command, unsigned long value) {
    if (command == GUI_CONTROL_DISPLAY_INFO) {
        GuiDisplayInfo info;

        if (!value || gui_get_display_info(&info) != 0) {
            return -1;
        }
        return process_copy_to_user(current_task, (Address)value, &info, sizeof(info)) == 0 ? 0 : -1;
    }

    if (command == GUI_CONTROL_POINTER_QUERY) {
        GuiPointerState state;

        if (!value || gui_get_pointer_state(&state) != 0) {
            return -1;
        }
        return process_copy_to_user(current_task, (Address)value, &state, sizeof(state)) == 0 ? 0 : -1;
    }

    if (command == GUI_CONTROL_DISPLAY_PRESENT) {
        GuiPresentBuffer present;

        if (!value || sys_copy_user_buffer((const void*)value, &present, sizeof(present)) != 0) {
            return -1;
        }
        if (present.version != GUI_PRESENT_BUFFER_VERSION || present.width == 0U || present.height == 0U ||
            present.pitch < (present.width * sizeof(unsigned int)) || present.width >(sizeof(g_sys_gui_scanline) / sizeof(g_sys_gui_scanline[0])) ||
            present.pixels == 0UL) {
            return -1;
        }

        for (unsigned int row = 0U; row < present.height; row++) {
            Address user_row = (Address)present.pixels + ((Address)row * (Address)present.pitch);

            if (sys_copy_user_buffer((const void*)user_row, g_sys_gui_scanline, (unsigned long)present.width * sizeof(unsigned int)) != 0) {
                return -1;
            }
            if (gui_present_scanline(present.x, present.y + row, present.width, g_sys_gui_scanline) != 0) {
                return -1;
            }
        }
        return 0;
    }

    if (command == GUI_CONTROL_INPUT_ACQUIRE) {
        return sys_gui_input_acquire_current_process();
    }

    if (command == GUI_CONTROL_SHARED_INPUT_ACQUIRE) {
        return sys_gui_shared_input_acquire_current_process(value);
    }

    if (command == GUI_CONTROL_SHARED_INPUT_RELEASE) {
        return sys_gui_shared_input_release_current_process();
    }

    if (command == GUI_CONTROL_SHARED_INPUT_QUERY) {
        return sys_gui_shared_input_query_current_process(value);
    }

    if (command == GUI_CONTROL_INPUT_EVENT_POLL) {
        GuiInputEvent event;

        if (!value) {
            return -1;
        }
        if (!sys_gui_input_is_owned_by_current_process()) {
            sys_gui_input_event_clear(&event);
        }
        else if (gui_get_input_event(&event) != 0) {
            return -1;
        }
        return process_copy_to_user(current_task, (Address)value, &event, sizeof(event)) == 0 ? 0 : -1;
    }

    return gui_control(command, value);
}

long sys_gui_set_taskbar_text(const char* clock_text, const char* date_text) {
    (void)clock_text;
    (void)date_text;
    return -1;
}

unsigned long sys_uptime_msec(void) {
    return get_system_timer() / 1000UL;
}

long sys_ext_invoke(const char* ext, const char* func, unsigned long a, unsigned long b) {
    long result;

    if (!ext || ext[0] == '\0' || !func || func[0] == '\0') {
        return -1;
    }

    if (module_invoke(ext, func, a, b, &result) != 0) {
        log_warning("Kernel module invoke target not found: %s.%s", ext, func);
        return -1;
    }
    return result;
}

long sys_module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result) {
    if (!result) {
        return -1;
    }

    if (module_invoke(module_name, export_name, a, b, result) != 0) {
        return -1;
    }
    return 0;
}

/* Syscall dispatch table indexed directly by the EL0 exception handler using the numbers in `syscall.h`. */
void* const sys_call_table[] = { sys_write, sys_malloc, sys_free, sys_clone, sys_exit, sys_get_param, sys_sleep, sys_exec, sys_shlib_open, sys_task_name, sys_spawn, sys_shlib_local, sys_console_read, sys_read_file, sys_dir_entry, sys_mkdir, sys_task_info, sys_mem_info, sys_kill, sys_reboot, sys_debug_shell, sys_ext_invoke, sys_task_args, sys_module_invoke, sys_shlib_export, sys_shlib_close, sys_driver_unload, sys_wait_pid, sys_gui_control, sys_gui_set_taskbar_text, sys_uptime_msec, sys_log_send, sys_log_recv, sys_log_write };
