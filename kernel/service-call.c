#include "arch/cortex-a53/mmu.h"
#include "device.h"
#include "filesystem/vfs/vfs.h"
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

    // Map enough user pages to cover the request and roll back partially completed work on failure.
    for (ULong index = 0; index < page_count; index++) {
        Address page = mem_alloc_page();
        if (!page) {
            for (ULong rollback = 0; rollback < index; rollback++) {
                process_unmap_page(task, base + (rollback * PAGE_SIZE));
            }
            return 0;
        }

        process_map_page(task, page, base + (index * PAGE_SIZE), PE_USER_DATA);
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

    if (!load_user_shared_library(path_copy)) {
        return 0;
    }

    address = load_user_shared_library_export(path_copy, export_copy);
    return address;
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
    return (unsigned long)(unsigned char)uart0_getc();
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
    info->counter = task->counter;
    info->priority = task->priority;
    info->flags = task->flags;

    name = process->name[0] != '\0' ? process->name : (task->name ? (char*)task->name : "");
    strncpy(info->name, name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    return 1;
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
void* const sys_call_table[] = { sys_write, sys_malloc, sys_free, sys_clone, sys_exit, sys_get_param, sys_sleep, sys_exec, sys_shlib_open, sys_task_name, sys_spawn, sys_shlib_local, sys_console_read, sys_read_file, sys_dir_entry, sys_mkdir, sys_task_info, sys_mem_info, sys_kill, sys_reboot, sys_debug_shell, sys_ext_invoke, sys_task_args, sys_module_invoke, sys_shlib_export };
