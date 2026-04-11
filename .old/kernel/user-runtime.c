/**
 * User-space loader runtime support for mapping and managing user executables,
 * DLLs, and driver modules from kernel context. This module tracks per-task
 * loader state, temporary VM reservations, and background driver-loop threads
 * for user-space .sys modules.
 *
 * Each kernel task that may launch or manage user-space modules has an
 * associated UserLoaderRuntime record that tracks the current loader context,
 * pending handoff addresses for EL0 execution, and temporary VM reservations
 * used to safely map user executables, DLLs, and driver modules into the
 * process address space.
 *
 * The loader runtime tracks up to USER_LOADER_MAX_RESERVATIONS temporary VM
 * reservations per task while mapping or protecting pages for a user module.
 * Each reservation records which pages have been committed, which pages have
 * been protected, and the final flags that should be applied once the mapping
 * is complete. This ensures that the loader can safely stage user-space
 * modules into the process address space without leaving partially mapped or
 * unprotected pages visible to the running user task.
 *
 * In addition, each loaded user-space .sys module that requires a background
 * driver thread has an associated UserDriverLoopState record that tracks the
 * owning task, the loader runtime that loaded the module, the module handle,
 * and the kernel thread ID of the background loop so that it can be stopped
 * cleanly when the module is unloaded or the owning task exits.
 *
 */
#include "arch/cortex-a53/mmu.h"
#include "filesystem/vfs/vfs.h"
#include "include/memory.h"
#include "ldr_internal.h"
#include "log.h"
#include "my-loader/ldr_kernel.h"
#include "task.h"
#include "user-exe.h"
#include "utils.h"

#define USER_LOADER_MAX_RESERVATIONS 8
#define USER_LOADER_ACTION_BIT (1UL << 63)

 /*
  * Per-task VM reservation bookkeeping used while the loader is mapping or
  * protecting one user executable, DLL, or driver image.
  */
typedef struct UserLoaderReservation {
    Bool used;
    Address base;
    Size size;
    UInt page_count;
    UByte committed[USER_SHARED_LIBRARY_MAX_PAGES];
    UByte protected_mask[USER_SHARED_LIBRARY_MAX_PAGES];
    Flags final_flags[USER_SHARED_LIBRARY_MAX_PAGES];
} UserLoaderReservation;

/*
 * Loader state anchored to one kernel task. This record keeps the live loader
 * context, pending EL0 handoff addresses, and temporary reservation metadata
 * needed to map user modules safely.
 */
typedef struct UserLoaderRuntime {
    Bool used;
    Task* task;
    PLDR_CONTEXT context;
    Address next_module_base;
    Address pending_entry_pc;
    Address pending_stack_top;
    PLDR_MODULE pending_close_module;
    Bool loading_executable;
    UserLoaderReservation reservations[USER_LOADER_MAX_RESERVATIONS];
    struct UserDriverLoopStateStruct* active_driver_loop;
} UserLoaderRuntime;

/*
 * Background driver-thread bookkeeping for one loaded user-space .sys module.
 */
typedef struct UserDriverLoopStateStruct {
    Bool used;
    Task* owner_task;
    UserLoaderRuntime* runtime;
    PLDR_MODULE module;
    long loop_tid;
    char path[USER_SHARED_LIBRARY_PATH_MAX];
} UserDriverLoopState;

/*
 * Heap-backed spawn request copied into a bootstrap kernel thread before it
 * replaces itself with the real user executable.
 */
typedef struct UserSpawnRequest {
    char path[USER_EXEC_PATH_MAX];
    char name[TASK_USER_NAME_MAX];
    char args[TASK_USER_LAUNCH_ARGS_MAX];
} UserSpawnRequest;

/* One runtime slot per task so each process owns an independent loader view. */
static UserLoaderRuntime g_user_loader_runtimes[NR_TASKS];
/* One optional driver-loop slot per task for user-space driver workers. */
static UserDriverLoopState g_user_driver_loops[NR_TASKS];
/* Active runtime temporarily borrowed while loader callbacks execute. */
static UserLoaderRuntime* g_user_loader_active_runtime;
static volatile unsigned int g_user_loader_operation_lock;

static inline unsigned int user_loader_spin_try_acquire(volatile unsigned int* lock) {
    unsigned int acquired = 1;
    unsigned int status;
    unsigned int previous;

    asm volatile(
        "ldaxr %w0, [%2]\n"
        "cbnz %w0, 1f\n"
        "stxr %w1, %w3, [%2]\n"
        "1:\n"
        : "=&r"(previous), "=&r"(status)
        : "r"(lock), "r"(acquired)
        : "memory");

    return previous == 0 && status == 0;
}

static inline void user_loader_spin_release(volatile unsigned int* lock) {
    asm volatile("stlr wzr, [%0]" : : "r"(lock) : "memory");
}

static void user_loader_operation_begin(UserLoaderRuntime* runtime) {
    while (!user_loader_spin_try_acquire(&g_user_loader_operation_lock)) {
        asm volatile("yield\n" ::: "memory");
    }

    g_user_loader_active_runtime = runtime;
}

static void user_loader_operation_end(void) {
    g_user_loader_active_runtime = 0;
    user_loader_spin_release(&g_user_loader_operation_lock);
}

static UserLoaderRuntime* user_loader_runtime_from_task(Task* task);
static UserDriverLoopState* user_loader_find_driver_loop_for_module(PLDR_MODULE module);
static void user_loader_stop_driver_loop(UserDriverLoopState* loop_state);
static void user_loader_stop_all_driver_loops(UserLoaderRuntime* runtime);

/* Return the filename portion of a path for suffix-based module lookup. */
static const char* user_loader_path_basename(const char* path) {
    const char* cursor;
    const char* basename;

    if (!path || path[0] == '\0') {
        return "";
    }

    basename = path;
    for (cursor = path; *cursor != '\0'; cursor++) {
        if (*cursor == '/') {
            basename = cursor + 1;
        }
    }

    return basename;
}

/* Check whether a path ends with a known image suffix such as .exe or .dll. */
static Bool user_loader_has_suffix(const char* path, const char* suffix) {
    UInt path_length;
    UInt suffix_length;

    if (!path || !suffix) {
        return false;
    }

    path_length = (UInt)strlen(path);
    suffix_length = (UInt)strlen(suffix);
    if (path_length < suffix_length) {
        return false;
    }

    return strcmp(path + (path_length - suffix_length), suffix) == 0;
}

/* Resolve the runtime currently serving loader callbacks on this CPU. */
static UserLoaderRuntime* user_loader_runtime_current(void) {
    if (g_user_loader_active_runtime) {
        return g_user_loader_active_runtime;
    }

    return user_loader_runtime_from_task(current_task);
}

/* Translate loader VM flags into the kernel's page-table permission flags. */
static Flags user_loader_arch_flags(Flags vm_flags) {
    if (vm_flags & LDR_VM_EXEC) {
        return PE_USER_CODE;
    }
    if (vm_flags & LDR_VM_WRITE) {
        return PE_USER_DATA;
    }
    if (vm_flags & LDR_VM_READ) {
        return PE_USER_RO;
    }

    return PE_USER_DATA;
}

/* Compare two module paths using either full strings or basename fallback. */
static Bool user_loader_path_equals(const char* lhs, const char* rhs) {
    if (!lhs || !rhs) {
        return false;
    }

    if (strcmp(lhs, rhs) == 0) {
        return true;
    }

    return strcmp(user_loader_path_basename(lhs), user_loader_path_basename(rhs)) == 0;
}

/* Find the driver-loop record currently associated with one loaded module. */
static UserDriverLoopState* user_loader_find_driver_loop_for_module(PLDR_MODULE module) {
    if (!module) {
        return 0;
    }

    for (int index = 0; index < NR_TASKS; index++) {
        if (g_user_driver_loops[index].used && g_user_driver_loops[index].module == module) {
            return &g_user_driver_loops[index];
        }
    }

    return 0;
}

/*
 * Start one extra EL0 thread for a user-space driver's DriverLoop export.
 */
static int user_loader_start_driver_loop(UserLoaderRuntime* runtime, PLDR_MODULE module, const char* path) {
    UserDriverLoopState* loop_state = 0;
    Address loop_entry = 0;
    long tid;

    if (!runtime || !module || module->kind != LDR_IMAGE_SYS) {
        return 0;
    }
    if (ldr_find_export(runtime->context, module, "DriverLoop", &loop_entry) != LDR_OK) {
        return 0;
    }

    loop_state = user_loader_find_driver_loop_for_module(module);
    if (loop_state) {
        return 0;
    }

    for (int index = 0; index < NR_TASKS; index++) {
        if (!g_user_driver_loops[index].used) {
            loop_state = &g_user_driver_loops[index];
            break;
        }
    }
    if (!loop_state) {
        return -1;
    }

    /**
     * Initialize the driver-loop state for this module so that it can be
     * tracked and managed independently of the main loader runtime. This
     * includes marking the slot as used, associating it with the owning
     * task and runtime, and setting the module and loop thread ID to
     * default values before spawning the actual user-space thread.
     */
    memzero((Address)loop_state, sizeof(*loop_state));
    loop_state->used = true;
    loop_state->owner_task = runtime->task;
    loop_state->runtime = runtime;
    loop_state->module = module;
    loop_state->loop_tid = -1;
    strncpy(loop_state->path, path ? path : "", sizeof(loop_state->path) - 1);

    /*
     * `DriverLoop` must execute from EL0 userspace, not EL1 kernel context.
     * Spawn it as a true additional user thread that shares the owner's mapped
     * module pages but owns a separate user stack.
     */
    tid = (long)process_create_user_thread(loop_entry, (Pointer)module->baseAddress);
    if (tid < 0) {
        memzero((Address)loop_state, sizeof(*loop_state));
        return -1;
    }

    loop_state->loop_tid = tid;
    runtime->active_driver_loop = loop_state;
    return 0;
}

static void user_loader_stop_driver_loop(UserDriverLoopState* loop_state) {
    if (!loop_state || !loop_state->used) {
        return;
    }

    if (loop_state->loop_tid > 0) {
        (void)kill_task(loop_state->loop_tid);
    }

    if (loop_state->runtime && loop_state->runtime->active_driver_loop == loop_state) {
        loop_state->runtime->active_driver_loop = 0;
    }
    memzero((Address)loop_state, sizeof(*loop_state));
}

static void user_loader_stop_all_driver_loops(UserLoaderRuntime* runtime) {
    if (!runtime) {
        return;
    }

    for (int index = 0; index < NR_TASKS; index++) {
        if (g_user_driver_loops[index].used && g_user_driver_loops[index].runtime == runtime) {
            user_loader_stop_driver_loop(&g_user_driver_loops[index]);
        }
    }
}

static UserLoaderReservation* user_loader_find_reservation(UserLoaderRuntime* runtime, Address base) {
    if (!runtime) {
        return 0;
    }

    for (int index = 0; index < USER_LOADER_MAX_RESERVATIONS; index++) {
        if (runtime->reservations[index].used && runtime->reservations[index].base == base) {
            return &runtime->reservations[index];
        }
    }

    return 0;
}

static UserLoaderReservation* user_loader_find_reservation_for_range(UserLoaderRuntime* runtime, Address base, Size size) {
    Address end = base + size;

    if (!runtime || size == 0) {
        return 0;
    }

    for (int index = 0; index < USER_LOADER_MAX_RESERVATIONS; index++) {
        Address reservation_end;

        if (!runtime->reservations[index].used) {
            continue;
        }

        reservation_end = runtime->reservations[index].base + runtime->reservations[index].size;
        if (base >= runtime->reservations[index].base && end <= reservation_end) {
            return &runtime->reservations[index];
        }
    }

    return 0;
}

static Bool user_loader_region_is_free(UserLoaderRuntime* runtime, Address base, Size size) {
    Address end = base + size;

    if (!runtime || size == 0) {
        return false;
    }

    for (int index = 0; index < USER_LOADER_MAX_RESERVATIONS; index++) {
        Address reservation_end;

        if (!runtime->reservations[index].used) {
            continue;
        }

        reservation_end = runtime->reservations[index].base + runtime->reservations[index].size;
        if (!(end <= runtime->reservations[index].base || base >= reservation_end)) {
            return false;
        }
    }

    return true;
}

static Address user_loader_choose_base(UserLoaderRuntime* runtime, Address preferred_base, Size size) {
    Address base = mem_align_down(preferred_base);
    Address limit = runtime && runtime->loading_executable ? USER_SHARED_LIBRARY_BASE : USER_SHARED_LIBRARY_LIMIT;
    Address cursor;

    if (!runtime || size == 0) {
        return 0;
    }

    if (base != 0) {
        if (runtime->loading_executable) {
            if (base >= VA_USER_START && base + size <= USER_SHARED_LIBRARY_BASE && user_loader_region_is_free(runtime, base, size)) {
                return base;
            }
        }
        else if (base >= USER_SHARED_LIBRARY_BASE && base + size <= USER_SHARED_LIBRARY_LIMIT && user_loader_region_is_free(runtime, base, size)) {
            return base;
        }
    }

    cursor = runtime->loading_executable ? VA_USER_START : runtime->next_module_base;
    cursor = mem_align_up(cursor);
    while (cursor + size <= limit) {
        if (user_loader_region_is_free(runtime, cursor, size)) {
            return cursor;
        }
        cursor += PAGE_SIZE;
    }

    return 0;
}

static int user_loader_release_reservation(UserLoaderRuntime* runtime, UserLoaderReservation* reservation) {
    Task* task = runtime ? runtime->task : 0;

    if (!runtime || !task || !reservation || !reservation->used) {
        return -1;
    }

    for (UInt page_index = 0; page_index < reservation->page_count; page_index++) {
        if (reservation->committed[page_index]) {
            (void)process_unmap_page(task, reservation->base + (page_index * PAGE_SIZE));
        }
    }

    memzero((Address)reservation, sizeof(*reservation));
    return 0;
}

static void user_loader_reset_runtime(UserLoaderRuntime* runtime) {
    if (!runtime) {
        return;
    }

    if (runtime->context) {
        user_loader_stop_all_driver_loops(runtime);
        user_loader_operation_begin(runtime);
        (void)ldr_shutdown(runtime->context);
        user_loader_operation_end();
    }

    memzero((Address)runtime, sizeof(*runtime));
}

static int user_loader_lock_create(PLDR_LOCKHANDLE out_lock) {
    if (!out_lock) {
        return -1;
    }

    out_lock->opaque = 1;
    return 0;
}

static int user_loader_lock_acquire(LDR_LOCKHANDLE lock_handle) {
    (void)lock_handle;
    return 0;
}

static int user_loader_lock_release(LDR_LOCKHANDLE lock_handle) {
    (void)lock_handle;
    return 0;
}

static void user_loader_log_putc(void* context, char ch) {
    char** cursor = (char**)context;

    if (!cursor || !*cursor) {
        return;
    }

    **cursor = ch;
    (*cursor)++;
}

static void user_loader_log_printf(Int level, CONST char* format, ...) {
    char message[256];
    char* cursor = message;
    va_list args;

    (void)level;
    memzero((Address)message, sizeof(message));
    va_start(args, format);
    tfp_format(&cursor, user_loader_log_putc, (char*)format, args);
    va_end(args);
    *cursor = '\0';
    log_warning("%s", message);
}

static int user_loader_vfs_open(CONST char* path, Flags flags, PLDR_FILEHANDLE out_file) {
    FileDesc* fd;

    (void)flags;
    if (!path || !out_file) {
        return -1;
    }

    fd = (FileDesc*)kmalloc(sizeof(FileDesc));
    if (!fd) {
        return -1;
    }

    if (vfs_fd_open(fd, path, O_READ) != SUCCESS) {
        kfree((Address)fd);
        return -1;
    }

    out_file->opaque = (Address)fd;
    return 0;
}

static int user_loader_vfs_read_at(LDR_FILEHANDLE file_handle, ULong offset, Pointer buffer, Size size, Size* out_read) {
    FileDesc* fd = (FileDesc*)file_handle.opaque;
    int read;

    if (!fd || !buffer || !out_read) {
        return -1;
    }

    if (vfs_fd_seek(fd, (unsigned int)offset, SEEK_SET) < 0) {
        return -1;
    }

    read = vfs_fd_read(fd, buffer, (unsigned int)size);
    if (read < 0) {
        return -1;
    }

    *out_read = (Size)read;
    return 0;
}

static int user_loader_vfs_size(LDR_FILEHANDLE file_handle, ULong* out_size) {
    FileDesc* fd = (FileDesc*)file_handle.opaque;

    if (!fd || !out_size) {
        return -1;
    }

    *out_size = fd->size;
    return 0;
}

static int user_loader_vfs_close(LDR_FILEHANDLE file_handle) {
    FileDesc* fd = (FileDesc*)file_handle.opaque;

    if (!fd) {
        return -1;
    }

    (void)vfs_fd_close(fd);
    kfree((Address)fd);
    return 0;
}

static int user_loader_vm_reserve(Address preferred_base, Size size, Flags flags, Address* out_base) {
    UserLoaderRuntime* runtime = user_loader_runtime_current();
    Address base;
    Size aligned_size;
    UserLoaderReservation* reservation = 0;

    if (!runtime || !out_base || size == 0 || (flags & LDR_VM_USER) == 0) {
        return -1;
    }

    aligned_size = mem_align_up(size);
    base = user_loader_choose_base(runtime, preferred_base, aligned_size);
    if (base == 0) {
        return -1;
    }

    for (int index = 0; index < USER_LOADER_MAX_RESERVATIONS; index++) {
        if (!runtime->reservations[index].used) {
            reservation = &runtime->reservations[index];
            break;
        }
    }

    if (!reservation) {
        return -1;
    }

    if ((aligned_size / PAGE_SIZE) > USER_SHARED_LIBRARY_MAX_PAGES) {
        return -1;
    }

    memzero((Address)reservation, sizeof(*reservation));
    reservation->used = true;
    reservation->base = base;
    reservation->size = aligned_size;
    reservation->page_count = (UInt)(aligned_size / PAGE_SIZE);
    *out_base = base;

    if (!runtime->loading_executable) {
        runtime->next_module_base = base + aligned_size;
    }

    return 0;
}

static int user_loader_vm_commit(Address base_address, Size size, Flags flags) {
    UserLoaderRuntime* runtime = user_loader_runtime_current();
    UserLoaderReservation* reservation = user_loader_find_reservation_for_range(runtime, base_address, size);
    Task* task = runtime ? runtime->task : 0;
    UInt first_page;
    UInt page_count;

    if (!runtime || !reservation || !task || size == 0) {
        return -1;
    }

    first_page = (UInt)((mem_align_down(base_address) - reservation->base) / PAGE_SIZE);
    page_count = (UInt)(mem_align_up((base_address - mem_align_down(base_address)) + size) / PAGE_SIZE);
    for (UInt page_index = 0; page_index < page_count; page_index++) {
        UInt slot = first_page + page_index;
        Address va = reservation->base + (slot * PAGE_SIZE);
        Address page;

        if (slot >= reservation->page_count) {
            return -1;
        }
        if (reservation->committed[slot]) {
            continue;
        }

        page = mem_alloc_page();
        if (!page) {
            for (UInt rollback = first_page; rollback < slot; rollback++) {
                if (reservation->committed[rollback]) {
                    (void)process_unmap_page(task, reservation->base + (rollback * PAGE_SIZE));
                    reservation->committed[rollback] = 0;
                }
            }
            return -1;
        }

        if (process_map_page(task, page, va, user_loader_arch_flags(flags)) != 0) {
            mem_free_page(page);
            for (UInt rollback = first_page; rollback < slot; rollback++) {
                if (reservation->committed[rollback]) {
                    (void)process_unmap_page(task, reservation->base + (rollback * PAGE_SIZE));
                    reservation->committed[rollback] = 0;
                }
            }
            return -1;
        }

        reservation->committed[slot] = 1;
    }

    set_pgd(task->mm.pgd);
    return 0;
}

static int user_loader_vm_protect(Address base_address, Size size, Flags flags) {
    UserLoaderRuntime* runtime = user_loader_runtime_current();
    UserLoaderReservation* reservation = user_loader_find_reservation_for_range(runtime, base_address, size);
    UInt first_page;
    UInt page_count;

    if (!runtime || !reservation || size == 0) {
        return -1;
    }

    first_page = (UInt)((mem_align_down(base_address) - reservation->base) / PAGE_SIZE);
    page_count = (UInt)(mem_align_up((base_address - mem_align_down(base_address)) + size) / PAGE_SIZE);
    for (UInt page_index = 0; page_index < page_count; page_index++) {
        UInt slot = first_page + page_index;

        if (slot >= reservation->page_count) {
            return -1;
        }

        reservation->protected_mask[slot] = 1;
        reservation->final_flags[slot] |= flags;
        if (reservation->committed[slot] && process_update_page_flags(runtime->task, reservation->base + (slot * PAGE_SIZE), user_loader_arch_flags(reservation->final_flags[slot])) != 0) {
            return -1;
        }
    }

    set_pgd(runtime->task->mm.pgd);
    return 0;
}

static int user_loader_vm_release(Address base_address, Size size) {
    UserLoaderRuntime* runtime = user_loader_runtime_current();
    UserLoaderReservation* reservation = user_loader_find_reservation(runtime, base_address);

    (void)size;
    if (!runtime || !reservation) {
        return -1;
    }

    return user_loader_release_reservation(runtime, reservation);
}

static int user_loader_proc_create(CONST char* name, PLDR_PROCESSHANDLE out_process) {
    (void)name;
    if (!out_process || !current_task) {
        return -1;
    }

    out_process->opaque = (Address)current_task;
    return 0;
}

static int user_loader_proc_set_entry(LDR_PROCESSHANDLE process_handle, Address entry_pc, Address stack_top) {
    UserLoaderRuntime* runtime = user_loader_runtime_from_task((Task*)process_handle.opaque);

    if (!runtime) {
        return -1;
    }

    runtime->pending_entry_pc = entry_pc;
    runtime->pending_stack_top = stack_top;
    return 0;
}

static int user_loader_proc_add_module(LDR_PROCESSHANDLE process_handle, CONST Pointer module_pointer) {
    (void)process_handle;
    (void)module_pointer;
    return 0;
}

static int user_loader_proc_start(LDR_PROCESSHANDLE process_handle) {
    (void)process_handle;
    return 0;
}

static void* user_loader_heap_alloc(Size size) {
    return (void*)kmalloc((int)(size ? size : 1));
}

static void user_loader_heap_free(Pointer memory) {
    if (memory) {
        kfree((Address)memory);
    }
}

static const LDR_KERNELAPI g_user_loader_api = {
    .vmReserve = user_loader_vm_reserve,
    .vmCommit = user_loader_vm_commit,
    .vmProtect = user_loader_vm_protect,
    .vmRelease = user_loader_vm_release,
    .heapAlloc = user_loader_heap_alloc,
    .heapFree = user_loader_heap_free,
    .vfsOpen = user_loader_vfs_open,
    .vfsReadAt = user_loader_vfs_read_at,
    .vfsSize = user_loader_vfs_size,
    .vfsClose = user_loader_vfs_close,
    .procCreate = user_loader_proc_create,
    .procSetEntry = user_loader_proc_set_entry,
    .procAddModule = user_loader_proc_add_module,
    .procStart = user_loader_proc_start,
    .lockCreate = user_loader_lock_create,
    .lockAcquire = user_loader_lock_acquire,
    .lockRelease = user_loader_lock_release,
    .logPrintf = user_loader_log_printf,
};

static UserLoaderRuntime* user_loader_runtime_from_task(Task* task) {
    if (!task || task->id < 0 || task->id >= NR_TASKS) {
        return 0;
    }

    if (!g_user_loader_runtimes[task->id].used || g_user_loader_runtimes[task->id].task != task) {
        return 0;
    }

    return &g_user_loader_runtimes[task->id];
}

static UserLoaderRuntime* user_loader_runtime_open(Task* task, Bool reset_existing) {
    UserLoaderRuntime* runtime;

    if (!task || task->id < 0 || task->id >= NR_TASKS) {
        return 0;
    }

    runtime = &g_user_loader_runtimes[task->id];
    if (runtime->used && runtime->task != task) {
        user_loader_reset_runtime(runtime);
    }
    if (runtime->used && reset_existing) {
        user_loader_reset_runtime(runtime);
    }
    if (!runtime->used) {
        PLDR_CONTEXT context = 0;

        if (ldr_init(&g_user_loader_api, &context) != LDR_OK) {
            return 0;
        }

        memzero((Address)runtime, sizeof(*runtime));
        runtime->used = true;
        runtime->task = task;
        runtime->context = context;
        runtime->next_module_base = USER_SHARED_LIBRARY_BASE;
        runtime->pending_stack_top = VA_USER_STACK;
    }

    return runtime;
}

static PLDR_MODULE user_loader_find_module(UserLoaderRuntime* runtime, const char* path) {
    if (!runtime || !runtime->context || !path) {
        return 0;
    }

    {
        PLDR_MODULE module = 0;

        if (ldr_find_module_by_path(runtime->context, path, &module) == LDR_OK) {
            return module;
        }
    }

    return 0;
}

static int user_loader_release_module_handle(UserLoaderRuntime* runtime, PLDR_MODULE module, Bool force_unload) {
    UserDriverLoopState* driver_loop;
    LDR_RESULT result;

    if (!runtime || !runtime->context || !module) {
        return -1;
    }

    driver_loop = user_loader_find_driver_loop_for_module(module);
    if (driver_loop && (force_unload || ldr_module_ref_count(module) <= 1)) {
        user_loader_stop_driver_loop(driver_loop);
    }

    if (force_unload) {
        result = ldr_unload_module_force(runtime->context, module);
    }
    else {
        result = ldr_release_module(runtime->context, module);
    }

    return result == LDR_OK ? 0 : -1;
}

static int user_loader_release_module_by_path(UserLoaderRuntime* runtime, const char* path, Bool require_driver) {
    PLDR_MODULE module;

    if (!runtime || !path) {
        return -1;
    }

    module = user_loader_find_module(runtime, path);
    if (!module || module->kind == LDR_IMAGE_EXE) {
        return -1;
    }
    if (require_driver && module->kind != LDR_IMAGE_SYS) {
        return -1;
    }

    return user_loader_release_module_handle(runtime, module, false);
}

static int user_loader_map_stack(Task* task) {
    Address stack_page;

    if (!task) {
        return -1;
    }

    for (int index = 0; index < task->mm.user_pages_count; index++) {
        if (task->mm.user_pages[index].virt_addr == (VA_USER_STACK - PAGE_SIZE)) {
            return 0;
        }
    }

    stack_page = mem_alloc_page();
    if (!stack_page) {
        return -1;
    }

    if (process_map_page(task, stack_page, VA_USER_STACK - PAGE_SIZE, PE_USER_DATA) != 0) {
        mem_free_page(stack_page);
        return -1;
    }

    return 0;
}

static LDR_IMAGEKIND user_loader_kind_from_path(const char* path) {
    if (!path) {
        return LDR_IMAGE_DLL;
    }
    if (user_loader_has_suffix(path, ".sys")) {
        return LDR_IMAGE_SYS;
    }
    if (user_loader_has_suffix(path, ".exe")) {
        return LDR_IMAGE_EXE;
    }

    return LDR_IMAGE_DLL;
}

static void user_loader_fill_process_metadata(Task* task, const char* path, const char* name, const char* args) {
    const char* fallback_name;

    if (!task || !task->process) {
        return;
    }

    fallback_name = path ? user_loader_path_basename(path) : "user";
    strncpy(task->process->program_path, path ? path : "", sizeof(task->process->program_path) - 1);
    task->process->program_path[sizeof(task->process->program_path) - 1] = '\0';
    strncpy(task->process->launch_args, args ? args : "", sizeof(task->process->launch_args) - 1);
    task->process->launch_args[sizeof(task->process->launch_args) - 1] = '\0';
    strncpy(task->process->name, name && name[0] ? name : fallback_name, sizeof(task->process->name) - 1);
    task->process->name[sizeof(task->process->name) - 1] = '\0';
    strncpy(task->thread_name, task->process->name, sizeof(task->thread_name) - 1);
    task->thread_name[sizeof(task->thread_name) - 1] = '\0';
    task->name = (Buffer)task->process->name;
}

static int user_loader_load_module(UserLoaderRuntime* runtime, const char* path, LDR_IMAGEKIND kind, PLDR_MODULE* out_module) {
    LDR_LOADREQUEST request;
    LDR_RESULT result;

    if (!runtime || !runtime->context || !path || !out_module) {
        return -1;
    }

    request.path = path;
    request.targetProcess.opaque = (Address)runtime->task;
    request.flags = LDR_VM_USER;

    user_loader_operation_begin(runtime);
    if (kind == LDR_IMAGE_EXE) {
        result = ldr_load_exe(runtime->context, &request, out_module);
    }
    else if (kind == LDR_IMAGE_SYS) {
        result = ldr_load_driver(runtime->context, &request, out_module);
    }
    else {
        result = ldr_load_dll(runtime->context, &request, out_module);
    }
    user_loader_operation_end();

    if (result != LDR_OK) {
        log_error("user loader failed for %s (kind=%d, result=%d)", path, kind, result);
    }

    return result == LDR_OK ? 0 : -1;
}

static void user_loader_spawn_bootstrap(Pointer arg) {
    UserSpawnRequest* request = (UserSpawnRequest*)arg;

    if (!request) {
        exit_current_process(-1);
        return;
    }

    user_loader_fill_process_metadata(current_task, request->path, request->name, request->args);
    if (exec_user_program(request->path) != 0) {
        log_error("spawn bootstrap failed for %s", request->path);
        kfree((Address)request);
        exit_current_process(-1);
        return;
    }
    kfree((Address)request);
}

int ldr_kernel_release_user_modules_for_task(void* task_ptr) {
    UserLoaderRuntime* runtime = user_loader_runtime_from_task((Task*)task_ptr);

    if (!runtime) {
        return 0;
    }

    user_loader_reset_runtime(runtime);
    return 0;
}

int ldr_kernel_forget_user_modules_for_task(void* task_ptr) {
    UserLoaderRuntime* runtime = user_loader_runtime_from_task((Task*)task_ptr);

    if (!runtime) {
        return 0;
    }

    memzero((Address)runtime, sizeof(*runtime));
    return 0;
}

int ldr_kernel_unload_user_shared_library(void* task_ptr, const char* path) {
    UserLoaderRuntime* runtime = user_loader_runtime_from_task((Task*)task_ptr);

    if (!runtime || !path || path[0] == '\0') {
        return -1;
    }

    return user_loader_release_module_by_path(runtime, path, false);
}

int ldr_kernel_unload_user_driver(void* task_ptr, const char* path) {
    UserLoaderRuntime* runtime = user_loader_runtime_from_task((Task*)task_ptr);

    if (!runtime || !path || path[0] == '\0') {
        return -1;
    }

    return user_loader_release_module_by_path(runtime, path, true);
}

int exec_user_program(const char* path) {
    UserLoaderRuntime* runtime;
    PLDR_MODULE module = 0;
    struct pt_regs* regs;

    if (!current_task || !current_process || !path || path[0] == '\0') {
        return -1;
    }

    (void)ldr_kernel_release_user_modules_for_task(current_task);
    process_reset_user_space(current_task);

    runtime = user_loader_runtime_open(current_task, true);
    if (!runtime) {
        log_error("exec_user_program: unable to open runtime for %s", path);
        return -1;
    }

    runtime->loading_executable = true;
    if (user_loader_load_module(runtime, path, LDR_IMAGE_EXE, &module) != 0) {
        runtime->loading_executable = false;
        log_error("exec_user_program: loader rejected %s", path);
        user_loader_reset_runtime(runtime);
        return -1;
    }
    runtime->loading_executable = false;

    if (user_loader_map_stack(current_task) != 0) {
        log_error("exec_user_program: unable to map user stack for %s", path);
        user_loader_reset_runtime(runtime);
        process_reset_user_space(current_task);
        return -1;
    }

    regs = task_pt_regs(current_task);
    memzero((Address)regs, sizeof(*regs));
    regs->pstate = PSR_MODE_EL0t;
    regs->pc = runtime->pending_entry_pc ? runtime->pending_entry_pc : (module->baseAddress + module->entryRva);
    regs->sp = VA_USER_STACK;

    current_task->flags = 0;
    current_task->cpu_context.x19 = 0;
    current_task->cpu_context.x20 = 0;
    user_loader_fill_process_metadata(current_task, path, current_process->name, current_process->launch_args);
    set_pgd(current_task->mm.pgd);
    log_info("exec_user_program: committed %s at pc=0x%lX sp=0x%lX", path, regs->pc, regs->sp);
    return 0;
}

int spawn_user_program(const char* path, const char* name, const char* args) {
    UserSpawnRequest* request;
    long pid;

    if (!path || path[0] == '\0') {
        return -1;
    }

    _trace("spawn_user_program: spawning %s with name='%s' args='%s'", path, name ? name : "", args ? args : "");

    request = (UserSpawnRequest*)kmalloc(sizeof(UserSpawnRequest));
    if (!request) {
        return -1;
    }

    _trace("spawn_user_program: allocated request structure at 0x%lX", (Address)request);

    memzero((Address)request, sizeof(*request));
    strncpy(request->path, path, sizeof(request->path) - 1);
    if (name && name[0] != '\0') {
        strncpy(request->name, name, sizeof(request->name) - 1);
    }
    if (args && args[0] != '\0') {
        strncpy(request->args, args, sizeof(request->args) - 1);
    }

    pid = (long)process_create_main_thread(PF_KTHREAD, (Address)&user_loader_spawn_bootstrap, request);
    _trace("spawn_user_program: process_create_main_thread returned pid=%ld", pid);
    if (pid < 0) {
        kfree((Address)request);
        return -1;
    }

    {
        Task* task = process_main_thread(pid);

        if (task) {
            user_loader_fill_process_metadata(task, request->path, request->name, request->args);
        }
    }

    return (int)pid;
}

Address load_user_shared_library(const char* path) {
    UserLoaderRuntime* runtime;
    PLDR_MODULE module;
    LDR_IMAGEKIND kind;
    Bool needs_user_init = false;

    if (!current_task || !path || path[0] == '\0') {
        return 0;
    }

    runtime = user_loader_runtime_open(current_task, false);
    if (!runtime) {
        return 0;
    }

    module = user_loader_find_module(runtime, path);
    if (module) {
        if (ldr_retain_module(runtime->context, module) != LDR_OK) {
            return 0;
        }
    }
    else {
        kind = user_loader_kind_from_path(path);
        if (kind == LDR_IMAGE_EXE || user_loader_load_module(runtime, path, kind, &module) != 0) {
            return 0;
        }
        needs_user_init = true;
        if (kind == LDR_IMAGE_SYS && user_loader_start_driver_loop(runtime, module, path) != 0) {
            (void)user_loader_release_module_handle(runtime, module, true);
            return 0;
        }
    }

    return needs_user_init ? (module->baseAddress | USER_LOADER_ACTION_BIT) : module->baseAddress;
}

long unload_user_shared_library(const char* path) {
    UserLoaderRuntime* runtime;
    PLDR_MODULE module;

    if (!current_task || !path || path[0] == '\0') {
        return -1;
    }

    runtime = user_loader_runtime_from_task(current_task);
    if (!runtime) {
        return -1;
    }

    if (runtime->pending_close_module) {
        if (!runtime->pending_close_module->path || !user_loader_path_equals(runtime->pending_close_module->path, path)) {
            return -1;
        }

        module = runtime->pending_close_module;
        runtime->pending_close_module = 0;
        return user_loader_release_module_handle(runtime, module, false);
    }

    module = user_loader_find_module(runtime, path);
    if (!module || module->kind == LDR_IMAGE_EXE) {
        return -1;
    }
    if (ldr_module_ref_count(module) <= 1) {
        runtime->pending_close_module = module;
        return (long)(module->baseAddress | USER_LOADER_ACTION_BIT);
    }

    return user_loader_release_module_handle(runtime, module, false);
}

long unload_user_driver(const char* path) {
    UserLoaderRuntime* runtime;
    PLDR_MODULE module;
    long result;

    if (!current_task || !path || path[0] == '\0') {
        return -1;
    }

    runtime = user_loader_runtime_from_task(current_task);
    if (!runtime) {
        return -1;
    }

    module = user_loader_find_module(runtime, path);
    if (!module || module->kind != LDR_IMAGE_SYS) {
        return -1;
    }

    result = unload_user_shared_library(path);
    if (result > 0 && (result & USER_LOADER_ACTION_BIT) == 0) {
        return -1;
    }

    return result;
}

Address load_user_shared_library_export(const char* path, const char* export_name) {
    UserLoaderRuntime* runtime;
    PLDR_MODULE module;
    Address address = 0;

    if (!current_task || !path || !export_name || path[0] == '\0' || export_name[0] == '\0') {
        return 0;
    }

    runtime = user_loader_runtime_open(current_task, false);
    if (!runtime) {
        return 0;
    }

    module = user_loader_find_module(runtime, path);
    if (!module) {
        return 0;
    }

    if (ldr_find_export(runtime->context, module, export_name, &address) != LDR_OK) {
        return 0;
    }

    return address;
}

Address load_user_shared_library_local(const char* path, ULong size) {
    Task* task = current_task;
    Address base;
    ULong page_count;

    if (!task || !path || path[0] == '\0' || size == 0) {
        return 0;
    }

    for (int index = 0; index < USER_SHARED_LIBRARY_MAX_TASK_LOCALS; index++) {
        if (task->mm.dll_locals[index].virt_addr != 0 && strcmp(task->mm.dll_locals[index].path, path) == 0) {
            return task->mm.dll_locals[index].virt_addr;
        }
    }

    page_count = mem_align_up(size) / PAGE_SIZE;
    base = task->mm.dll_local_next ? task->mm.dll_local_next : USER_SHARED_LIBRARY_LOCAL_BASE;
    if (base + (page_count * PAGE_SIZE) > USER_SHARED_LIBRARY_LOCAL_LIMIT) {
        return 0;
    }

    for (int slot = 0; slot < USER_SHARED_LIBRARY_MAX_TASK_LOCALS; slot++) {
        if (task->mm.dll_locals[slot].virt_addr != 0) {
            continue;
        }

        for (ULong page_index = 0; page_index < page_count; page_index++) {
            Address page = mem_alloc_page();
            if (!page || process_map_page(task, page, base + (page_index * PAGE_SIZE), PE_USER_DATA) != 0) {
                if (page) {
                    mem_free_page(page);
                }
                for (ULong rollback = 0; rollback < page_index; rollback++) {
                    (void)process_unmap_page(task, base + (rollback * PAGE_SIZE));
                }
                return 0;
            }
        }

        strncpy(task->mm.dll_locals[slot].path, path, sizeof(task->mm.dll_locals[slot].path) - 1);
        task->mm.dll_locals[slot].virt_addr = base;
        task->mm.dll_locals[slot].size = size;
        task->mm.dll_locals[slot].page_count = page_count;
        task->mm.dll_local_next = base + (page_count * PAGE_SIZE);
        set_pgd(task->mm.pgd);
        return base;
    }

    return 0;
}

UInt user_shared_library_snapshot(UserSharedLibraryInfo* infos, UInt max_infos) {
    UInt count = 0;

    if (infos && max_infos > 0) {
        memzero((Address)infos, sizeof(UserSharedLibraryInfo) * max_infos);
    }

    for (int runtime_index = 0; runtime_index < NR_TASKS; runtime_index++) {
        UserLoaderRuntime* runtime = &g_user_loader_runtimes[runtime_index];
        PLDR_MODULE module;

        if (!runtime->used || !runtime->context || !runtime->task) {
            continue;
        }

        for (module = runtime->context->moduleHead; module; module = module->nextModule) {
            UserDriverLoopState* driver_loop;
            UInt ref_count;
            UInt existing_index = max_infos;

            if (module->kind == LDR_IMAGE_EXE || !module->path || module->path[0] == '\0') {
                continue;
            }

            driver_loop = user_loader_find_driver_loop_for_module(module);
            ref_count = (UInt)(ldr_module_ref_count(module) > 0 ? ldr_module_ref_count(module) : 0);

            if (infos && max_infos > 0) {
                for (UInt info_index = 0; info_index < count && info_index < max_infos; info_index++) {
                    if (infos[info_index].used && user_loader_path_equals(infos[info_index].path, module->path)) {
                        existing_index = info_index;
                        break;
                    }
                }
            }

            if (existing_index < max_infos) {
                infos[existing_index].ref_count += ref_count;
                infos[existing_index].driver_loop_active = infos[existing_index].driver_loop_active || (driver_loop ? true : false);
                continue;
            }

            if (infos && count < max_infos) {
                infos[count].used = true;
                strncpy(infos[count].path, module->path, sizeof(infos[count].path) - 1);
                infos[count].base_va = module->baseAddress;
                infos[count].entry_point = module->baseAddress + module->entryRva;
                infos[count].image_size = module->imageSize;
                infos[count].page_count = (UInt)(mem_align_up(module->imageSize) / PAGE_SIZE);
                infos[count].ref_count = ref_count;
                infos[count].kind = (UInt)module->kind;
                infos[count].driver_loop_active = driver_loop ? true : false;
            }
            count++;
        }
    }

    return count;
}

UInt user_shared_library_export_snapshot(const char* path, UserSharedLibraryExportInfo* exports, UInt max_exports) {
    UInt count = 0;

    if (exports && max_exports > 0) {
        memzero((Address)exports, sizeof(UserSharedLibraryExportInfo) * max_exports);
    }
    if (!path || path[0] == '\0') {
        return 0;
    }

    for (int runtime_index = 0; runtime_index < NR_TASKS; runtime_index++) {
        UserLoaderRuntime* runtime = &g_user_loader_runtimes[runtime_index];
        PLDR_MODULE module;

        if (!runtime->used || !runtime->context) {
            continue;
        }

        module = user_loader_find_module(runtime, path);
        if (!module) {
            continue;
        }

        for (UInt index = 0; index < module->exportCount; index++) {
            if (exports && count < max_exports) {
                strncpy(exports[count].name, module->exports[index].symbolName ? module->exports[index].symbolName : "", sizeof(exports[count].name) - 1);
                exports[count].address = module->baseAddress + module->exports[index].symbolRva;
            }
            count++;
        }

        return count;
    }

    return 0;
}

UInt user_shared_library_import_snapshot(long task_id, const char* path, UserSharedLibraryImportInfo* imports, UInt max_imports) {
    UserLoaderRuntime* runtime;
    UInt count = 0;

    if (imports && max_imports > 0) {
        memzero((Address)imports, sizeof(UserSharedLibraryImportInfo) * max_imports);
    }
    if (task_id < 0 || task_id >= NR_TASKS || !path || path[0] == '\0') {
        return 0;
    }

    runtime = &g_user_loader_runtimes[task_id];
    if (!runtime->used || !runtime->context || !runtime->task) {
        return 0;
    }

    for (PLDR_MODULE module = runtime->context->moduleHead; module; module = module->nextModule) {
        if (!module->path || module->path[0] == '\0') {
            continue;
        }

        for (UInt import_index = 0; import_index < module->importCount; import_index++) {
            PCLDR_IMPORT import_item = &module->imports[import_index];
            Bool duplicate = false;

            if (!import_item->moduleName || !import_item->symbolName || !user_loader_path_equals(import_item->moduleName, path)) {
                continue;
            }

            if (imports && max_imports > 0) {
                for (UInt info_index = 0; info_index < count && info_index < max_imports; info_index++) {
                    if (user_loader_path_equals(imports[info_index].importer_path, module->path) &&
                        strcmp(imports[info_index].symbol_name, import_item->symbolName) == 0) {
                        duplicate = true;
                        break;
                    }
                }
            }

            if (duplicate) {
                continue;
            }

            if (imports && count < max_imports) {
                strncpy(imports[count].importer_path, module->path, sizeof(imports[count].importer_path) - 1);
                strncpy(imports[count].symbol_name, import_item->symbolName, sizeof(imports[count].symbol_name) - 1);
                imports[count].iat_address = module->baseAddress + import_item->iatRva;
            }
            count++;
        }
    }

    return count;
}
