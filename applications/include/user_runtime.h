#ifndef APPLICATIONS_USER_RUNTIME_H
#define APPLICATIONS_USER_RUNTIME_H

#include "dll_image.h"
#include "user_ipc.h"
#include <stddef.h>
#include <stdint.h>

/* Loader metadata constants consumed by the user-image packer. */
#define USER_LDR_META_VERSION 2U
#define USER_LDR_META_KIND_IMPORT 1U
#define USER_LDR_META_KIND_EXPORT 2U
#define USER_LDR_META_NAME_MAX 64U

/* Keep the executable image header at the aligned EL0 image base used by the kernel loader. */
#define USER_EXE_IMAGE_BASE 0x00200000UL

/* Standard userspace heap reservation mirrored from the kernel EL0 layout. */
#define USER_HEAP_BASE 0x20000000UL
#define USER_HEAP_LIMIT 0x40000000UL
#define USER_HEAP_BYTES (USER_HEAP_LIMIT - USER_HEAP_BASE)

/* Userspace syscall numbers mirrored from the kernel ABI. */
#define USER_SYS_WRITE 0UL
#define USER_SYS_EXIT 4UL
#define USER_SYS_SLEEP 6UL
#define USER_SYS_SHLIB_OPEN 8UL
#define USER_SYS_TASK_NAME 9UL
#define USER_SYS_SPAWN 10UL
#define USER_SYS_SHLIB_LOCAL 11UL
#define USER_SYS_CONSOLE_READ 12UL
#define USER_SYS_READ_FILE 13UL
#define USER_SYS_DIR_ENTRY 14UL
#define USER_SYS_MKDIR 15UL
#define USER_SYS_TASK_INFO 16UL
#define USER_SYS_MEM_INFO 17UL
#define USER_SYS_KILL 18UL
#define USER_SYS_REBOOT 19UL
#define USER_SYS_DEBUG_SHELL 20UL
#define USER_SYS_TASK_RESOURCE_INFO 21UL
#define USER_SYS_TASK_ARGS 22UL
#define USER_SYS_SHLIB_EXPORT 24UL
#define USER_SYS_SHLIB_CLOSE 25UL
#define USER_SYS_DRIVER_UNLOAD 26UL
#define USER_SYS_WAIT_PID 27UL
#define USER_SYS_GUI_CONTROL 28UL
#define USER_SYS_GUI_SET_TASKBAR_TEXT 29UL
#define USER_SYS_UPTIME_MSEC 30UL
#define USER_SYS_LOG_SEND 31UL
#define USER_SYS_LOG_RECV 32UL
#define USER_SYS_LOG_WRITE 33UL
#define USER_SYS_EVENT_SUBSCRIBE 34UL
#define USER_SYS_EVENT_READ 35UL
#define USER_SYS_EVENT_QUERY 36UL
#define USER_SYS_EVENT_UNSUBSCRIBE 37UL
#define USER_SYS_EVENT_WAIT 38UL
#define USER_SYS_REMOVE 39UL
#define USER_SYS_IPC_SEND 40UL
#define USER_SYS_IPC_RECV 41UL
#define USER_SYS_CREATE_THREAD 42UL
#define USER_SYS_SET_THREAD_PRIORITY 43UL
#define USER_SYS_SPAWN_ASYNC 44UL
#define USER_SYS_SPAWN_ASYNC_RESULT 45UL
#define USER_SYS_FILE_MAPPING_CREATE 46UL
#define USER_SYS_FILE_MAPPING_OPEN 47UL
#define USER_SYS_FILE_MAPPING_CLOSE 48UL
#define USER_SYS_FILE_MAPPING_MAP 49UL
#define USER_SYS_FILE_MAPPING_UNMAP 50UL
#define USER_SYS_FILE_MAPPING_RESIZE 51UL
#define USER_SYS_TASK_MODULES 52UL
#define USER_SYS_PATH_INFO 53UL
#define USER_SYS_EXIT_THREAD 54UL
#define USER_SYS_SET_THREAD_PRIORITY_BY_ID 55UL

/* Thread priority codes mirrored from the kernel scheduler ABI. Lower values run first. */
#define USER_THREAD_PRIORITY_HIGHEST 0UL
#define USER_THREAD_PRIORITY_ABOVE_NORMAL 8UL
#define USER_THREAD_PRIORITY_NORMAL 16UL
#define USER_THREAD_PRIORITY_BELOW_NORMAL 24UL
#define USER_THREAD_PRIORITY_IDLE 31UL

/* Task state codes mirrored from the kernel scheduler ABI. */
#ifndef APPLICATIONS_USER_TASK_STATE_CODES_DEFINED
#define APPLICATIONS_USER_TASK_STATE_CODES_DEFINED
#define USER_TASK_STATE_INITIALIZED 0UL
#define USER_TASK_STATE_READY 1UL
#define USER_TASK_STATE_RUNNING 2UL
#define USER_TASK_STATE_WAITING 3UL
#define USER_TASK_STATE_SUSPENDED 4UL
#define USER_TASK_STATE_TERMINATED 5UL
#endif

/* Task wait reasons mirrored from the kernel scheduler ABI. */
#ifndef APPLICATIONS_USER_TASK_WAIT_REASON_CODES_DEFINED
#define APPLICATIONS_USER_TASK_WAIT_REASON_CODES_DEFINED
#define USER_TASK_WAIT_NONE 0UL
#define USER_TASK_WAIT_DELAY 1UL
#define USER_TASK_WAIT_EVENT 2UL
#define USER_TASK_WAIT_MUTEX 3UL
#define USER_TASK_WAIT_SEMAPHORE 4UL
#define USER_TASK_WAIT_MESSAGE 5UL
#define USER_TASK_WAIT_IO 6UL
#endif

/* Loader action bit returned by open/close syscalls when Init or Deinit is required. */
#define USER_LOADER_ACTION_BIT (1UL << 63)

/* One filesystem directory entry returned by the kernel VFS syscalls. */
#ifndef APPLICATIONS_USER_DIRECTORY_ENTRY_DEFINED
#define APPLICATIONS_USER_DIRECTORY_ENTRY_DEFINED
typedef struct UserDirectoryEntry {
    char name[128];
    unsigned long size;
    unsigned long attr;
} UserDirectoryEntry;
#endif

/* One resolved VFS path snapshot returned by the path-info syscall. */
#ifndef APPLICATIONS_USER_PATH_INFO_DEFINED
#define APPLICATIONS_USER_PATH_INFO_DEFINED
typedef struct UserPathInfo {
    unsigned long size;
    unsigned long type;
    unsigned long backend_kind;
    unsigned long flags;
    unsigned long volume_letter;
    char device_name[16];
} UserPathInfo;
#endif

/* One userspace task snapshot returned by the task-info syscall. */
#ifndef APPLICATIONS_USER_TASK_INFO_DEFINED
#define APPLICATIONS_USER_TASK_INFO_DEFINED
typedef struct UserTaskInfo {
    long id;
    long thread_id;
    long parent_process_id;
    long main_thread_state;
    long wait_reason;
    long exit_code;
    long scheduler_ticks;
    long current_priority;
    unsigned long flags;
    char name[32];
} UserTaskInfo;
#endif

/* One task resource snapshot returned by the task-resource syscall. */
#ifndef APPLICATIONS_USER_TASK_RESOURCE_INFO_DEFINED
#define APPLICATIONS_USER_TASK_RESOURCE_INFO_DEFINED
#define USER_TASK_RESOURCE_FLAG_IMAGE_SECTION_TRUNCATED 1UL
#define USER_TASK_SECTION_FLAG_READ 1UL
#define USER_TASK_SECTION_FLAG_WRITE 2UL
#define USER_TASK_SECTION_FLAG_EXEC 4UL
#define USER_TASK_SECTION_FLAG_BSS 8UL
#define USER_TASK_LAYOUT_MAX_SECTIONS 16UL
typedef struct UserTaskSectionInfo {
    unsigned long start_address;
    unsigned long end_address;
    unsigned long flags;
    char name[12];
} UserTaskSectionInfo;
typedef struct UserTaskResourceInfo {
    unsigned long image_bytes;
    unsigned long stack_bytes;
    unsigned long heap_bytes;
    unsigned long total_bytes;
    unsigned long image_base;
    unsigned long flags;
    unsigned long image_section_count;
    char image_path[260];
    UserTaskSectionInfo image_sections[USER_TASK_LAYOUT_MAX_SECTIONS];
} UserTaskResourceInfo;
#endif

/* One loaded-module snapshot returned for a process by the task-module syscall. */
#ifndef APPLICATIONS_USER_TASK_MODULE_INFO_DEFINED
#define APPLICATIONS_USER_TASK_MODULE_INFO_DEFINED
#define USER_TASK_MODULE_FLAG_SHARED_BACKING 1UL
#define USER_TASK_MODULE_FLAG_PENDING_ATTACH 2UL
#define USER_TASK_MODULE_FLAG_PENDING_DETACH 4UL
#define USER_TASK_MODULE_FLAG_PRIVATE_WRITABLE 8UL
#define USER_TASK_MODULE_FLAG_SECTION_TRUNCATED 16UL
typedef struct UserTaskModuleInfo {
    unsigned long image_base;
    unsigned long image_bytes;
    unsigned long shared_backing_bytes;
    unsigned long private_backing_bytes;
    unsigned long shared_reference_count;
    unsigned long flags;
    char module_name[64];
    char path[260];
    unsigned long section_count;
    UserTaskSectionInfo sections[USER_TASK_LAYOUT_MAX_SECTIONS];
} UserTaskModuleInfo;
#endif

/* One memory usage snapshot returned by the mem-info syscall. */
#ifndef APPLICATIONS_USER_MEM_INFO_DEFINED
#define APPLICATIONS_USER_MEM_INFO_DEFINED
typedef struct UserMemInfo {
    unsigned long total_bytes;
    unsigned long free_bytes;
    unsigned long page_size;
    unsigned long free_pages;
} UserMemInfo;
#endif

/* One completed async process-launch result returned by the kernel launch worker. */
#ifndef APPLICATIONS_USER_ASYNC_SPAWN_RESULT_DEFINED
#define APPLICATIONS_USER_ASYNC_SPAWN_RESULT_DEFINED
typedef struct UserAsyncSpawnResult {
    unsigned long request_id;
    long status;
    long pid;
    unsigned long reserved0;
} UserAsyncSpawnResult;
#endif

/* Synthetic status returned to timeout-driven async launch error callbacks. */
#ifndef APPLICATIONS_USER_ASYNC_SPAWN_TIMEOUT_STATUS
#define APPLICATIONS_USER_ASYNC_SPAWN_TIMEOUT_STATUS (-1000L)
#endif

/* Opaque file-mapping handle returned by the new file-mapping syscalls. */
#ifndef APPLICATIONS_USER_FILE_MAPPING_HANDLE_DEFINED
#define APPLICATIONS_USER_FILE_MAPPING_HANDLE_DEFINED
typedef unsigned long FileMappingHandle;
#endif

/* Callback invoked when one queued async launch either succeeds or fails. */
typedef void (*UserLaunchProcessCallback)(
    const char* path,
    long pid,
    long status,
    const char* name,
    const char* args,
    void* context);

/* Spawn-task-specific callback alias for one successful async launch. */
typedef UserLaunchProcessCallback SpawnTaskAsyncSuccessCallback;

/* Spawn-task-specific callback alias for one failed or timed-out async launch. */
typedef UserLaunchProcessCallback SpawnTaskAsyncErrorCallback;

/* Compatibility alias for the older one-callback async launch helper. */
typedef UserLaunchProcessCallback SpawnTaskAsyncCompletionCallback;

#ifdef __cplusplus
extern "C" {
#endif

    /* Queue one async process launch with separate success and failure callbacks. */
    long SpawnTaskAsyncCallbacks(
        const char* path,
        const char* name,
        const char* args,
        SpawnTaskAsyncSuccessCallback success_callback,
        SpawnTaskAsyncErrorCallback error_callback,
        void* context);

    /* Queue one async process launch and invoke the callback when a result is ready. */
    long SpawnTaskAsyncCallback(
        const char* path,
        const char* name,
        const char* args,
        SpawnTaskAsyncCompletionCallback callback,
        void* context);

    /* Compatibility wrapper around the async spawn callback helper. */
    long UserLaunchProcess(
        const char* path,
        const char* name,
        const char* args,
        UserLaunchProcessCallback callback,
        void* context);

#ifdef __cplusplus
}
#endif

/*
 * One explicit export metadata record emitted into `.ldrmeta.exports`.
 *
 * The builder reads these records from module-owned object files and turns them
 * into the packed DLL0 export table. This keeps the public ABI explicit and
 * prevents runtime helper symbols from leaking into DLL or SYS exports.
 */
typedef struct __attribute__((packed)) UserLoaderExportMeta {
    unsigned short version;
    unsigned short kind;
    char export_name[USER_LDR_META_NAME_MAX];
    char symbol_name[USER_LDR_META_NAME_MAX];
} UserLoaderExportMeta;

/*
 * One explicit import metadata record emitted into `.ldrmeta.imports`.
 *
 * The slot symbol names one writable IAT-style pointer cell inside the image.
 * The kernel loader resolves the target export by name and writes the final
 * address into that slot before the module runs.
 */
typedef struct __attribute__((packed)) UserLoaderImportMeta {
    unsigned short version;
    unsigned short kind;
    char module_name[USER_LDR_META_NAME_MAX];
    char symbol_name[USER_LDR_META_NAME_MAX];
    char slot_symbol[USER_LDR_META_NAME_MAX];
} UserLoaderImportMeta;

#define LDR_EMIT_IMPORT(module_name_literal, symbol_name_literal, slot_symbol_literal, unique_suffix) \
    static const UserLoaderImportMeta __attribute__((used, aligned(1), section(".ldrmeta.imports"))) \
        import_##unique_suffix = { \
            USER_LDR_META_VERSION, \
            USER_LDR_META_KIND_IMPORT, \
            module_name_literal, \
            symbol_name_literal, \
            slot_symbol_literal, \
        }

/* Emit one typed IAT slot and matching import metadata record. */
#ifdef __cplusplus
#define DLL_IMPORT_FUNCTION(module_name_literal, symbol_name_literal, function_type, slot_symbol) \
    static function_type slot_symbol __asm__(#slot_symbol) __attribute__((used, section(".data"))) = (function_type)0; \
    LDR_EMIT_IMPORT(module_name_literal, symbol_name_literal, #slot_symbol, slot_symbol)
#else
#define DLL_IMPORT_FUNCTION(module_name_literal, symbol_name_literal, function_type, slot_symbol) \
    static function_type __attribute__((used, section(".data"))) slot_symbol = (function_type)0; \
    LDR_EMIT_IMPORT(module_name_literal, symbol_name_literal, #slot_symbol, slot_symbol)
#endif

#define LDR_EMIT_EXPORT(export_name_literal, symbol_name_literal, unique_suffix) \
	static const UserLoaderExportMeta __attribute__((used, aligned(1), section(".ldrmeta.exports"))) \
		export_##unique_suffix = { \
			USER_LDR_META_VERSION, \
			USER_LDR_META_KIND_EXPORT, \
			export_name_literal, \
			symbol_name_literal, \
		}

/* Public export helpers used by user DLL and SYS modules. */
#define DLL_EXPORT(symbol_name) \
	LDR_EMIT_EXPORT(#symbol_name, #symbol_name, symbol_name)

#define SYS_EXPORT(symbol_name) \
	LDR_EMIT_EXPORT(#symbol_name, #symbol_name, symbol_name)

#define DLL_EXPORT_AS(export_name_literal, symbol_name) \
	LDR_EMIT_EXPORT(export_name_literal, #symbol_name, symbol_name)

#define SYS_EXPORT_AS(export_name_literal, symbol_name) \
	LDR_EMIT_EXPORT(export_name_literal, #symbol_name, symbol_name)

/* single line import */
#define FROM
#define DECLARE(ret, name, args, non, module, symbol) \
    typedef ret (*name##_fn) args; \
    DLL_IMPORT_FUNCTION(module, symbol, name##_fn, name)


/* Forward declaration used by the cached export resolver. */
static inline unsigned long exportSharedLibrary(const char* path, const char* exportName);
static inline long freeLibraryByName(const char* path);

typedef unsigned long HMODULE;
typedef unsigned long FARPROC;

#ifndef APPLICATIONS_USER_SHARED_HEAP_ALLOCATOR_DEFINED
#define APPLICATIONS_USER_SHARED_HEAP_ALLOCATOR_DEFINED

typedef struct UserSharedHeapBlock UserSharedHeapBlock;

typedef struct UserSharedHeapState {
    unsigned long magic;
    volatile unsigned long lock_word;
    UserSharedHeapBlock* head;
} UserSharedHeapState;

struct UserSharedHeapBlock {
    size_t payload_bytes;
    unsigned long flags;
    UserSharedHeapBlock* next;
    UserSharedHeapBlock* prev;
};

#define USER_SHARED_HEAP_MAGIC 0x554845415031ULL
#define USER_SHARED_HEAP_BLOCK_FREE 0x1UL

/*
 * Align one heap byte count to the runtime allocator granularity.
 *
 * Every module in one process must agree on block alignment or they will walk
 * the shared heap metadata differently. Keeping this helper in the shared
 * runtime header guarantees EXEs and DLLs split blocks on the same 16-byte
 * boundaries while only depending on one runtime include.
 *
 * @param value Requested byte count.
 * @return 16-byte aligned byte count.
 */
static inline size_t user_shared_heap_align_up(size_t value) {
    const size_t mask = 15U;

    return (value + mask) & ~mask;
}

/*
 * Return the process-local shared heap state record stored at heap base.
 *
 * @return Pointer to the shared heap state header.
 */
static inline UserSharedHeapState* user_shared_heap_state(void) {
    return (UserSharedHeapState*)(uintptr_t)USER_HEAP_BASE;
}

/*
 * Return the first allocatable block inside the shared heap window.
 *
 * @return Pointer to the first heap block header.
 */
static inline UserSharedHeapBlock* user_shared_heap_first_block(void) {
    const uintptr_t block_address = (uintptr_t)USER_HEAP_BASE + user_shared_heap_align_up(sizeof(UserSharedHeapState));

    return (UserSharedHeapBlock*)block_address;
}

/*
 * Return whether one pointer lies inside the shared heap reservation.
 *
 * @param ptr Candidate heap pointer.
 * @return Non-zero when the pointer falls inside the heap reservation.
 */
static inline int user_shared_heap_contains_pointer(const void* ptr) {
    const uintptr_t address = (uintptr_t)ptr;

    return (address >= (uintptr_t)USER_HEAP_BASE) && (address < (uintptr_t)USER_HEAP_LIMIT);
}

/*
 * Return whether one block is currently free.
 *
 * @param block Heap block to classify.
 * @return Non-zero when the block is on the free list.
 */
static inline int user_shared_heap_block_is_free(const UserSharedHeapBlock* block) {
    return (block != NULL) && ((block->flags & USER_SHARED_HEAP_BLOCK_FREE) != 0UL);
}

/*
 * Attempt one acquire of the shared user-heap lock.
 *
 * All modules in the process share the same heap metadata, so allocator calls
 * from GWES worker threads must serialize around the block list or one thread
 * can observe partially split or coalesced neighbors from another thread.
 *
 * @param lock_word Shared heap lock word stored in the heap state header.
 * @return Non-zero when the caller acquired the lock.
 */
static inline int user_shared_heap_try_lock_once(volatile unsigned long* lock_word) {
    unsigned long observed = 0UL;
    unsigned int store_failed = 0U;
    const unsigned long locked = 1UL;

    if (lock_word == NULL) {
        return 0;
    }

    asm volatile(
        "ldaxr %0, [%2]\n"
        "cbnz %0, 1f\n"
        "stxr %w1, %3, [%2]\n"
        "b 2f\n"
        "1:\n"
        "mov %w1, #1\n"
        "2:\n"
        : "=&r"(observed), "=&r"(store_failed)
        : "r"(lock_word), "r"(locked)
        : "memory");

    return (observed == 0UL) && (store_failed == 0U);
}

/*
 * Initialize the shared heap state while the caller already holds the heap lock.
 *
 * Reusing the same header field for the lock and first-touch initialization keeps
 * the allocator process-wide without adding a second shared global outside the
 * heap reservation.
 *
 * @param state Shared heap state header guarded by the caller.
 * @return Pointer to the initialized shared heap state record.
 */
static inline UserSharedHeapState* user_shared_heap_initialize_locked(UserSharedHeapState* state) {
    if (state == NULL) {
        return NULL;
    }

    if ((state->magic == USER_SHARED_HEAP_MAGIC) && (state->head != NULL)) {
        return state;
    }

    state->magic = USER_SHARED_HEAP_MAGIC;
    state->head = user_shared_heap_first_block();
    state->head->payload_bytes = USER_HEAP_BYTES
        - user_shared_heap_align_up(sizeof(UserSharedHeapState))
        - sizeof(UserSharedHeapBlock);
    state->head->flags = USER_SHARED_HEAP_BLOCK_FREE;
    state->head->next = NULL;
    state->head->prev = NULL;
    return state;
}

/*
 * Acquire the shared user-heap lock and return the initialized heap state.
 *
 * The lock is coarse by design: correctness matters more than allocator
 * parallelism because every in-process module already shares one intrusive
 * block list at a fixed virtual address.
 *
 * @return Pointer to the initialized shared heap state record.
 */
static inline UserSharedHeapState* user_shared_heap_lock(void) {
    UserSharedHeapState* state = user_shared_heap_state();

    while (!user_shared_heap_try_lock_once(&state->lock_word)) {
        asm volatile("yield\n" ::: "memory");
    }

    return user_shared_heap_initialize_locked(state);
}

/*
 * Release the shared user-heap lock.
 *
 * @param state Shared heap state header that currently owns the lock.
 * @return Nothing.
 */
static inline void user_shared_heap_unlock(UserSharedHeapState* state) {
    const unsigned long unlocked = 0UL;

    if (state == NULL) {
        return;
    }

    asm volatile("stlr %1, [%0]" : : "r"(&state->lock_word), "r"(unlocked) : "memory");
}

/*
 * Split one oversized free block so the remainder stays reusable.
 *
 * @param block Free block selected for allocation.
 * @param payload_bytes Aligned payload size being consumed.
 * @return Nothing.
 */
static inline void user_shared_heap_split_block(UserSharedHeapBlock* block, size_t payload_bytes) {
    UserSharedHeapBlock* remainder;

    if (block == NULL) {
        return;
    }
    if (block->payload_bytes <= (payload_bytes + sizeof(UserSharedHeapBlock) + 16U)) {
        return;
    }

    remainder = (UserSharedHeapBlock*)((unsigned char*)(block + 1) + payload_bytes);
    remainder->payload_bytes = block->payload_bytes - payload_bytes - sizeof(UserSharedHeapBlock);
    remainder->flags = USER_SHARED_HEAP_BLOCK_FREE;
    remainder->next = block->next;
    remainder->prev = block;
    if (remainder->next != NULL) {
        remainder->next->prev = remainder;
    }

    block->payload_bytes = payload_bytes;
    block->next = remainder;
}

/*
 * Merge one free block with adjacent free neighbors.
 *
 * @param block Newly freed block.
 * @return Nothing.
 */
static inline void user_shared_heap_coalesce(UserSharedHeapBlock* block) {
    if (block == NULL) {
        return;
    }

    if ((block->next != NULL) && user_shared_heap_block_is_free(block->next)) {
        UserSharedHeapBlock* next = block->next;

        block->payload_bytes += sizeof(UserSharedHeapBlock) + next->payload_bytes;
        block->next = next->next;
        if (block->next != NULL) {
            block->next->prev = block;
        }
    }

    if ((block->prev != NULL) && user_shared_heap_block_is_free(block->prev)) {
        UserSharedHeapBlock* prev = block->prev;

        prev->payload_bytes += sizeof(UserSharedHeapBlock) + block->payload_bytes;
        prev->next = block->next;
        if (prev->next != NULL) {
            prev->next->prev = prev;
        }
    }
}

/*
 * Allocate one payload block while the caller already holds the heap lock.
 *
 * @param state Initialized shared heap state.
 * @param payload_bytes Aligned payload size requested by the caller.
 * @return Payload pointer on success, or null when the heap is exhausted.
 */
static inline void* user_shared_heap_malloc_locked(UserSharedHeapState* state, size_t payload_bytes) {
    UserSharedHeapBlock* block;

    if (state == NULL) {
        return NULL;
    }

    for (block = state->head; block != NULL; block = block->next) {
        if (!user_shared_heap_block_is_free(block) || (block->payload_bytes < payload_bytes)) {
            continue;
        }

        user_shared_heap_split_block(block, payload_bytes);
        block->flags &= ~USER_SHARED_HEAP_BLOCK_FREE;
        return (void*)(block + 1);
    }

    return NULL;
}

/*
 * Release one heap allocation while the caller already holds the heap lock.
 *
 * @param ptr Payload pointer previously returned by the shared heap.
 * @return Nothing.
 */
static inline void user_shared_heap_free_locked(void* ptr) {
    UserSharedHeapBlock* block;

    if ((ptr == NULL) || !user_shared_heap_contains_pointer(ptr)) {
        return;
    }

    block = ((UserSharedHeapBlock*)ptr) - 1;
    block->flags |= USER_SHARED_HEAP_BLOCK_FREE;
    user_shared_heap_coalesce(block);
}

/*
 * Allocate one payload block from the shared in-process heap.
 *
 * @param size Requested payload bytes.
 * @return Payload pointer on success, or null when the heap is exhausted.
 */
static inline void* user_shared_heap_malloc(size_t size) {
    const size_t payload_bytes = user_shared_heap_align_up(size ? size : 1U);
    UserSharedHeapState* state = user_shared_heap_lock();
    void* allocation;

    allocation = user_shared_heap_malloc_locked(state, payload_bytes);
    user_shared_heap_unlock(state);

    return allocation;
}

/*
 * Release one shared heap allocation.
 *
 * @param ptr Payload pointer previously returned by user_shared_heap_malloc.
 * @return Nothing.
 */
static inline void user_shared_heap_free(void* ptr) {
    UserSharedHeapState* state;

    if ((ptr == NULL) || !user_shared_heap_contains_pointer(ptr)) {
        return;
    }

    state = user_shared_heap_lock();
    user_shared_heap_free_locked(ptr);
    user_shared_heap_unlock(state);
}

/*
 * Resize one shared heap allocation.
 *
 * @param ptr Existing payload pointer, or null.
 * @param size New requested payload bytes.
 * @return Resized allocation, or null on failure.
 */
static inline void* user_shared_heap_realloc(void* ptr, size_t size) {
    UserSharedHeapState* state;
    UserSharedHeapBlock* block;
    size_t payload_bytes;
    void* replacement;

    if (ptr == NULL) {
        return user_shared_heap_malloc(size);
    }
    if (size == 0U) {
        user_shared_heap_free(ptr);
        return NULL;
    }
    if (!user_shared_heap_contains_pointer(ptr)) {
        return NULL;
    }

    state = user_shared_heap_lock();
    block = ((UserSharedHeapBlock*)ptr) - 1;
    payload_bytes = user_shared_heap_align_up(size);
    if (block->payload_bytes >= payload_bytes) {
        user_shared_heap_split_block(block, payload_bytes);
        user_shared_heap_unlock(state);
        return ptr;
    }

    if ((block->next != NULL)
        && user_shared_heap_block_is_free(block->next)
        && ((block->payload_bytes + sizeof(UserSharedHeapBlock) + block->next->payload_bytes) >= payload_bytes)) {
        UserSharedHeapBlock* next = block->next;

        block->payload_bytes += sizeof(UserSharedHeapBlock) + next->payload_bytes;
        block->next = next->next;
        if (block->next != NULL) {
            block->next->prev = block;
        }
        user_shared_heap_split_block(block, payload_bytes);
        user_shared_heap_unlock(state);
        return ptr;
    }

    replacement = user_shared_heap_malloc_locked(state, payload_bytes);
    if (replacement == NULL) {
        user_shared_heap_unlock(state);
        return NULL;
    }

    {
        unsigned char* destination = (unsigned char*)replacement;
        unsigned char* source = (unsigned char*)ptr;
        size_t copy_bytes = (block->payload_bytes < size) ? block->payload_bytes : size;
        size_t index;

        for (index = 0U; index < copy_bytes; ++index) {
            destination[index] = source[index];
        }
    }

    user_shared_heap_free_locked(ptr);
    user_shared_heap_unlock(state);
    return replacement;
}

#endif

/*
 * Resolve one shared-library export and cache the address in a caller-owned slot.
 *
 * The first call performs the export lookup syscall. Later calls reuse the
 * cached address without another kernel transition.
 */
static inline unsigned long resolveSharedLibraryCached(const char* path, const char* exportName, unsigned long* cachedAddress) {
    if (!cachedAddress) {
        return exportSharedLibrary(path, exportName);
    }
    if (*cachedAddress == 0UL) {
        *cachedAddress = exportSharedLibrary(path, exportName);
    }

    return *cachedAddress;
}

/* One per-symbol cache slot used by DLL callers. */
#define DLL_CACHE(cache_name) \
	static unsigned long cache_name = 0UL

/* Typed helper for resolving one cached DLL function pointer. */
#define DLL_RESOLVE(path_literal, export_name_literal, function_type, cache_name) \
	((function_type)(unsigned long)resolveSharedLibraryCached((path_literal), (export_name_literal), &(cache_name)))

#ifndef DEBUG_ENABLE_USER_LOADER_TRACE
#define DEBUG_ENABLE_USER_LOADER_TRACE 0
#endif

/*
 * Issue one zero-argument syscall.
 */
static inline unsigned long invokeSyscall0(unsigned long number) {
    register unsigned long x0 asm("x0");
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

/*
 * Issue one one-argument syscall.
 */
static inline unsigned long invokeSyscall1(unsigned long number, unsigned long arg0) {
    register unsigned long x0 asm("x0") = arg0;
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

/*
 * Issue one two-argument syscall.
 */
static inline unsigned long invokeSyscall2(unsigned long number, unsigned long arg0, unsigned long arg1) {
    register unsigned long x0 asm("x0") = arg0;
    register unsigned long x1 asm("x1") = arg1;
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
    return x0;
}

/*
 * Issue one three-argument syscall.
 */
static inline unsigned long invokeSyscall3(unsigned long number, unsigned long arg0, unsigned long arg1, unsigned long arg2) {
    register unsigned long x0 asm("x0") = arg0;
    register unsigned long x1 asm("x1") = arg1;
    register unsigned long x2 asm("x2") = arg2;
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

/*
 * Issue one four-argument syscall.
 */
static inline unsigned long invokeSyscall4(unsigned long number, unsigned long arg0, unsigned long arg1, unsigned long arg2, unsigned long arg3) {
    register unsigned long x0 asm("x0") = arg0;
    register unsigned long x1 asm("x1") = arg1;
    register unsigned long x2 asm("x2") = arg2;
    register unsigned long x3 asm("x3") = arg3;
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x8) : "memory");
    return x0;
}

/*
 * Issue one five-argument syscall.
 */
static inline unsigned long invokeSyscall5(unsigned long number, unsigned long arg0, unsigned long arg1, unsigned long arg2, unsigned long arg3, unsigned long arg4) {
    register unsigned long x0 asm("x0") = arg0;
    register unsigned long x1 asm("x1") = arg1;
    register unsigned long x2 asm("x2") = arg2;
    register unsigned long x3 asm("x3") = arg3;
    register unsigned long x4 asm("x4") = arg4;
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x8) : "memory");
    return x0;
}

/* Return the length of one C string without depending on libc. */
static inline unsigned long textLength(const char* text) {
    unsigned long length = 0UL;

    if (!text) {
        return 0UL;
    }

    while (text[length] != '\0') {
        length++;
    }

    return length;
}

/* Append one source string into a destination cursor and return the new cursor. */
static inline char* appendText(char* destination, const char* text) {
    if (!destination || !text) {
        return destination;
    }

    while (*text != '\0') {
        *destination++ = *text++;
    }

    return destination;
}

/* Append one unsigned decimal value into a destination cursor. */
static inline char* appendUnsignedLong(char* destination, unsigned long value) {
    char digits[32];
    unsigned long count = 0UL;

    if (!destination) {
        return destination;
    }
    if (value == 0UL) {
        *destination++ = '0';
        return destination;
    }

    while (value != 0UL) {
        digits[count++] = (char)('0' + (value % 10UL));
        value /= 10UL;
    }
    while (count > 0UL) {
        *destination++ = digits[--count];
    }

    return destination;
}

/* Append one unsigned hexadecimal value without a 0x prefix. */
static inline char* appendHex(char* destination, unsigned long value) {
    char digits[32];
    unsigned long count = 0UL;

    if (!destination) {
        return destination;
    }
    if (value == 0UL) {
        *destination++ = '0';
        return destination;
    }

    while (value != 0UL) {
        unsigned long nibble = (value & 0xFUL);

        digits[count++] = (char)(nibble < 10UL ? ('0' + nibble) : ('A' + (nibble - 10UL)));
        value >>= 4;
    }
    while (count > 0UL) {
        *destination++ = digits[--count];
    }

    return destination;
}

/* Write one raw text buffer to the kernel console and return the kernel status. */
static inline long writeText(const char* text) {
    return (long)invokeSyscall1(USER_SYS_WRITE, (unsigned long)text);
}

/* Write one text line followed by CRLF and stop at the first kernel-side failure. */
static inline long writeLine(const char* text) {
    long status = writeText(text);

    if (status < 0) {
        return status;
    }

    return writeText("\r\n");
}

/* Terminate the current user process and never return. */
static inline void __attribute__((noreturn)) exitProcess(unsigned long result) {
    (void)invokeSyscall1(USER_SYS_EXIT, result);

    for (;;) {
        asm volatile("wfe" ::: "memory");
    }
}

/* Sleep the current task for at least the requested number of milliseconds. */
static inline long sleepMs(unsigned long milliseconds) {
    return (long)invokeSyscall1(USER_SYS_SLEEP, milliseconds);
}

/* Copy the current task name into a caller-provided buffer. */
static inline long getTaskName(char* buffer, unsigned long size) {
    return (long)invokeSyscall2(USER_SYS_TASK_NAME, (unsigned long)buffer, size);
}

/* Copy the current task launch arguments into a caller-provided buffer. */
static inline long getTaskArgs(char* buffer, unsigned long size) {
    return (long)invokeSyscall2(USER_SYS_TASK_ARGS, (unsigned long)buffer, size);
}

/*
 * Map one named shared-memory object into the current process.
 *
 * Pass a non-zero `size` to create the object when it does not exist yet, or
 * zero to open an already-created object without changing its size.
 */
static inline long acquireSharedMemoryRegion(const char* name, unsigned long size, void** addressOut) {
    unsigned long raw;

    if (addressOut == 0) {
        return -1;
    }

    *addressOut = 0;
    raw = invokeSyscall2(USER_SYS_SHLIB_LOCAL, (unsigned long)name, size);
    if ((long)raw < 0) {
        return (long)raw;
    }

    *addressOut = (void*)(unsigned long)raw;
    return 0;
}

/*
 * Convenience wrapper around `acquireSharedMemoryRegion`.
 *
 * The pointer form matches the existing loader helpers that return NULL on
 * failure when the caller does not need the exact kernel status code.
 */
static inline void* acquireSharedMemory(const char* name, unsigned long size) {
    void* address = 0;

    if (acquireSharedMemoryRegion(name, size, &address) < 0) {
        return 0;
    }

    return address;
}

/* Create one file-mapping object and return its handle to the caller. */
static inline long createFileMapping(const char* path, unsigned long size, FileMappingHandle* handleOut) {
    unsigned long raw;

    if (handleOut == 0) {
        return -1;
    }

    *handleOut = 0;
    raw = invokeSyscall2(USER_SYS_FILE_MAPPING_CREATE, (unsigned long)path, size);
    if ((long)raw < 0) {
        return (long)raw;
    }

    *handleOut = (FileMappingHandle)raw;
    return 0;
}

/* Open one existing file-mapping object and return its handle to the caller. */
static inline long openFileMapping(const char* path, unsigned long size, FileMappingHandle* handleOut) {
    unsigned long raw;

    if (handleOut == 0) {
        return -1;
    }

    *handleOut = 0;
    raw = invokeSyscall2(USER_SYS_FILE_MAPPING_OPEN, (unsigned long)path, size);
    if ((long)raw < 0) {
        return (long)raw;
    }

    *handleOut = (FileMappingHandle)raw;
    return 0;
}

/* Release one file-mapping handle. */
static inline long closeFileMapping(FileMappingHandle handle) {
    return (long)invokeSyscall1(USER_SYS_FILE_MAPPING_CLOSE, handle);
}

/* Grow one file-mapping object to a larger logical size. */
static inline long resizeFileMapping(FileMappingHandle handle, unsigned long size) {
    return (long)invokeSyscall2(USER_SYS_FILE_MAPPING_RESIZE, handle, size);
}

/* Map one file-mapping handle into the current process and return the view address. */
static inline long mapFileMappingView(FileMappingHandle handle, unsigned long offset, unsigned long size, void** addressOut) {
    unsigned long raw;

    if (addressOut == 0) {
        return -1;
    }

    *addressOut = 0;
    raw = invokeSyscall3(USER_SYS_FILE_MAPPING_MAP, handle, offset, size);
    if ((long)raw < 0) {
        return (long)raw;
    }

    *addressOut = (void*)raw;
    return 0;
}

/* Unmap one previously returned file-mapping view address. */
static inline long unmapFileMappingView(void* address) {
    return (long)invokeSyscall1(USER_SYS_FILE_MAPPING_UNMAP, (unsigned long)address);
}

/* Read one character from the console input stream. */
static inline long readConsole(void) {
    return (long)invokeSyscall0(USER_SYS_CONSOLE_READ);
}

/* Read bytes from one file path into a caller-provided buffer. */
static inline long readFile(const char* path, unsigned long offset, char* buffer, unsigned long size) {
    return (long)invokeSyscall4(USER_SYS_READ_FILE, (unsigned long)path, offset, (unsigned long)buffer, size);
}

/*
 * Read one directory entry by index from a filesystem path.
 *
 * Returns a positive value when one entry was copied, zero when the directory
 * exists but the requested index is past the end, and a negative status code
 * for real errors such as an invalid or missing path.
 */
static inline long readDirectoryEntry(const char* path, unsigned long entryIndex, UserDirectoryEntry* entry) {
    return (long)invokeSyscall3(USER_SYS_DIR_ENTRY, (unsigned long)path, entryIndex, (unsigned long)entry);
}

/* Create one directory path in the userspace VFS. */
static inline long makeDirectory(const char* path) {
    return (long)invokeSyscall1(USER_SYS_MKDIR, (unsigned long)path);
}

/* Remove one file or directory path from the userspace VFS. */
static inline long removePath(const char* path) {
    return (long)invokeSyscall1(USER_SYS_REMOVE, (unsigned long)path);
}

/* Route one kernel-brokered IPC packet to another process. */
static inline long sendIpc(long receiverPid, const UserIpcMessage* message) {
    return (long)invokeSyscall2(USER_SYS_IPC_SEND, (unsigned long)receiverPid, (unsigned long)message);
}

/* Receive one kernel-brokered IPC packet for the current process. */
static inline long receiveIpc(UserIpcMessage* message, unsigned long flags) {
    return (long)invokeSyscall2(USER_SYS_IPC_RECV, (unsigned long)message, flags);
}

/* Canonical callback signature for one additional EL0 worker thread. */
typedef void (*UserThreadEntryPoint)(unsigned long argument);

/* One heap-backed bootstrap record passed to the shared thread trampoline. */
typedef struct UserThreadStartContext {
    UserThreadEntryPoint entry;
    unsigned long argument;
} UserThreadStartContext;

/*
 * Terminate the current EL0 thread without exiting the whole process.
 *
 * Background helpers inside EXEs and DLLs need a real thread-return path so a
 * worker can stop cleanly during module or process teardown instead of parking
 * forever inside module text that may later be unloaded.
 *
 * @return Zero on success, or a negative status code on failure.
 */
static inline long exitCurrentThread(void) {
    return (long)invokeSyscall0(USER_SYS_EXIT_THREAD);
}

/*
 * Enter one worker thread through a runtime-owned trampoline.
 *
 * The kernel thread syscall starts execution directly at the supplied program
 * counter. Routing worker threads through one small bootstrap helper gives the
 * runtime a guaranteed post-return path so a normal C function can finish and
 * then terminate the thread with the dedicated exit-thread syscall.
 *
 * @param bootstrap_raw Heap-backed `UserThreadStartContext` pointer.
 * @return Never returns.
 */
static void userThreadEntryTrampoline(unsigned long bootstrap_raw) {
    UserThreadStartContext* bootstrap = (UserThreadStartContext*)bootstrap_raw;
    UserThreadEntryPoint entry = 0;
    unsigned long argument = 0UL;

    if (bootstrap != 0) {
        entry = bootstrap->entry;
        argument = bootstrap->argument;
        user_shared_heap_free(bootstrap);
    }

    if (entry != 0) {
        entry(argument);
    }

    (void)exitCurrentThread();
    for (;;) {
        asm volatile("wfe" ::: "memory");
    }
}

/*
 * Start one additional EL0 thread inside the current process.
 *
 * The public helper now allocates a tiny bootstrap record and points the new
 * thread at the shared trampoline above, which means user worker functions can
 * simply return instead of depending on an ad hoc park-forever pattern.
 *
 * @param entryPoint Worker entrypoint with signature `void (*)(unsigned long)`.
 * @param argument Caller-supplied worker argument.
 * @param name Optional thread name copied into the kernel thread object.
 * @return Non-negative thread id on success, or a negative status code on failure.
 */
static inline long startUserThread(unsigned long entryPoint, unsigned long argument, const char* name) {
    UserThreadStartContext* bootstrap;
    long thread_id;

    if (entryPoint == 0UL) {
        return -4L;
    }

    bootstrap = (UserThreadStartContext*)user_shared_heap_malloc(sizeof(*bootstrap));
    if (bootstrap == 0) {
        return -6L;
    }

    bootstrap->entry = (UserThreadEntryPoint)entryPoint;
    bootstrap->argument = argument;
    thread_id = (long)invokeSyscall3(
        USER_SYS_CREATE_THREAD,
        (unsigned long)&userThreadEntryTrampoline,
        (unsigned long)bootstrap,
        (unsigned long)name);
    if (thread_id < 0L) {
        user_shared_heap_free(bootstrap);
    }

    return thread_id;
}

/* Change the scheduler priority of the currently running EL0 thread. */
static inline long setCurrentThreadPriority(unsigned long priority) {
    return (long)invokeSyscall1(USER_SYS_SET_THREAD_PRIORITY, priority);
}

/* Change the scheduler priority of one EL0 thread in the current process. */
static inline long setThreadPriority(long threadId, unsigned long priority) {
    return (long)invokeSyscall2(USER_SYS_SET_THREAD_PRIORITY_BY_ID, (unsigned long)threadId, priority);
}

/* Query one task descriptor by PID. */
static inline long getTaskInfo(long pid, UserTaskInfo* info) {
    return (long)invokeSyscall2(USER_SYS_TASK_INFO, (unsigned long)pid, (unsigned long)info);
}

/* Query one task resource snapshot by PID. */
static inline long getTaskResourceInfo(long pid, UserTaskResourceInfo* info) {
    return (long)invokeSyscall2(USER_SYS_TASK_RESOURCE_INFO, (unsigned long)pid, (unsigned long)info);
}

/* Query the loaded-module list for one task by PID. */
static inline long getTaskModuleInfo(long pid, UserTaskModuleInfo* modules, unsigned long capacity, unsigned long* countOut) {
    return (long)invokeSyscall4(USER_SYS_TASK_MODULES, (unsigned long)pid, (unsigned long)modules, capacity, (unsigned long)countOut);
}

/* Resolve one VFS path and return its userspace-visible node metadata. */
static inline long getPathInfo(const char* path, UserPathInfo* info) {
    return (long)invokeSyscall2(USER_SYS_PATH_INFO, (unsigned long)path, (unsigned long)info);
}

/* Wait for one child process to terminate and collect its result code. */
static inline long waitPid(long pid, long* result) {
    return (long)invokeSyscall2(USER_SYS_WAIT_PID, (unsigned long)pid, (unsigned long)result);
}

/* Send one GUI control command to the kernel display service. */
static inline long controlGui(unsigned long command, unsigned long value) {
    return (long)invokeSyscall2(USER_SYS_GUI_CONTROL, command, value);
}

/* Update the retained taskbar clock and date text. */
static inline long setGuiTaskbarText(const char* clockText, const char* dateText) {
    return (long)invokeSyscall2(USER_SYS_GUI_SET_TASKBAR_TEXT, (unsigned long)clockText, (unsigned long)dateText);
}

/* Read the kernel uptime in milliseconds. */
static inline unsigned long getUptimeMs(void) {
    return invokeSyscall0(USER_SYS_UPTIME_MSEC);
}

/* Queue one formatted log line for the core log broker. */
static inline long sendLog(const char* text) {
    return (long)invokeSyscall1(USER_SYS_LOG_SEND, (unsigned long)text);
}

/* Receive one queued log line from the kernel log broker. */
static inline long receiveLog(char* buffer, unsigned long size) {
    return (long)invokeSyscall2(USER_SYS_LOG_RECV, (unsigned long)buffer, size);
}

/* Write one log line directly to the console fallback path. */
static inline long writeLog(const char* text) {
    return (long)invokeSyscall1(USER_SYS_LOG_WRITE, (unsigned long)text);
}

/* Query the userspace-visible memory usage snapshot. */
static inline long getMemoryInfo(UserMemInfo* info) {
    return (long)invokeSyscall1(USER_SYS_MEM_INFO, (unsigned long)info);
}

/* Request termination of one task by PID. */
static inline long killTask(long pid) {
    return (long)invokeSyscall1(USER_SYS_KILL, (unsigned long)pid);
}

/* Reboot the machine through the kernel service path and return the kernel status. */
static inline long rebootSystem(void) {
    return (long)invokeSyscall0(USER_SYS_REBOOT);
}

/* Execute one kernel debug-shell command. */
static inline long runDebugShell(const char* line) {
    return (long)invokeSyscall1(USER_SYS_DEBUG_SHELL, (unsigned long)line);
}

static inline const dll_header* loaderImageHeader(unsigned long imageBase) {
    const dll_header* header = (const dll_header*)(unsigned long)imageBase;

    if (header == 0 || header->magic != DLL_IMAGE_MAGIC) {
        return 0;
    }

    return header;
}

static inline const char* loaderStringAt(const dll_header* header, unsigned long stringOffset) {
    const char* stringTable = (const char*)((const unsigned char*)header + header->string_table_offset);
    unsigned long index;

    if (header == 0 || stringOffset >= header->string_table_size) {
        return 0;
    }

    for (index = stringOffset; index < header->string_table_size; ++index) {
        if (stringTable[index] == '\0') {
            return stringTable + stringOffset;
        }
    }

    return 0;
}

static inline int loaderCallModuleEntry(HMODULE module, unsigned long reason) {
    const dll_header* header = loaderImageHeader(module);

    if (header == 0 || header->entry_point_rva == 0ULL) {
        return 0;
    }

    return ((dll_entry_fn)(unsigned long)(module + (unsigned long)header->entry_point_rva))((void*)(unsigned long)module, (U32)reason);
}

static inline int loaderAttachImportsForModule(HMODULE module);
static inline int loaderDetachImportsForModule(HMODULE module);

static inline unsigned long loadLibraryRaw(const char* path) {
    return invokeSyscall1(USER_SYS_SHLIB_OPEN, (unsigned long)path);
}

static inline unsigned long closeLibraryRaw(unsigned long identifier) {
    return invokeSyscall1(USER_SYS_SHLIB_CLOSE, identifier);
}

/* Load one DLL and run its PROCESS_ATTACH callback from EL0 when requested. */
static inline HMODULE loadLibrary(const char* path) {
    unsigned long raw = loadLibraryRaw(path);
    HMODULE module = raw & ~USER_LOADER_ACTION_BIT;

#if DEBUG_ENABLE_USER_LOADER_TRACE
    writeText("user-loader: open ");
    writeLine(path ? path : "<null>");
#endif
    if ((raw & USER_LOADER_ACTION_BIT) != 0UL && module != 0UL) {
#if DEBUG_ENABLE_USER_LOADER_TRACE
        writeText("user-loader: attach ");
        writeLine(path ? path : "<null>");
#endif
        (void)loaderAttachImportsForModule(module);
        (void)loaderCallModuleEntry(module, DLL_REASON_PROCESS_ATTACH);
    }

    return module;
}

/* Resolve one export by HMODULE in the same style as GetProcAddress. */
static inline FARPROC getProcAddress(HMODULE module, const char* exportName) {
    return invokeSyscall2(USER_SYS_SHLIB_EXPORT, (unsigned long)module, (unsigned long)exportName);
}

/* Spawn one new user task with an optional visible name and launch arguments. */
static inline long spawnTask(const char* path, const char* name, const char* args) {
    return (long)invokeSyscall3(USER_SYS_SPAWN, (unsigned long)path, (unsigned long)name, (unsigned long)args);
}

/* Queue one new user task on the kernel launch worker and return a request id immediately. */
static inline long spawnTaskAsync(const char* path, const char* name, const char* args) {
    return (long)invokeSyscall3(USER_SYS_SPAWN_ASYNC, (unsigned long)path, (unsigned long)name, (unsigned long)args);
}

/* Queue one async user task and invoke a callback when the result becomes available. */
static inline long spawnTaskAsyncCallbacks(
    const char* path,
    const char* name,
    const char* args,
    SpawnTaskAsyncSuccessCallback success_callback,
    SpawnTaskAsyncErrorCallback error_callback,
    void* context) {
    return SpawnTaskAsyncCallbacks(path, name, args, success_callback, error_callback, context);
}

/* Queue one async user task and invoke one callback on both success and failure. */
static inline long spawnTaskAsyncCallback(
    const char* path,
    const char* name,
    const char* args,
    SpawnTaskAsyncCompletionCallback callback,
    void* context) {
    return SpawnTaskAsyncCallbacks(path, name, args, callback, callback, context);
}

/* Poll one completed async process-launch result for the current process. */
static inline long pollAsyncSpawnResult(UserAsyncSpawnResult* result) {
    return (long)invokeSyscall1(USER_SYS_SPAWN_ASYNC_RESULT, (unsigned long)result);
}

/* Resolve one exported symbol from a loaded shared library image by path. */
static inline unsigned long exportSharedLibrary(const char* path, const char* exportName) {
    return invokeSyscall2(USER_SYS_SHLIB_EXPORT, (unsigned long)path, (unsigned long)exportName);
}

/* Release one module by HMODULE and run PROCESS_DETACH when requested. */
static inline long freeLibrary(HMODULE module) {
    unsigned long raw = closeLibraryRaw((unsigned long)module);
    long result = (long)raw;

    if ((raw & USER_LOADER_ACTION_BIT) != 0UL && (raw & ~USER_LOADER_ACTION_BIT) != 0UL) {
        HMODULE handle = raw & ~USER_LOADER_ACTION_BIT;

        (void)loaderCallModuleEntry(handle, DLL_REASON_PROCESS_DETACH);
        (void)loaderDetachImportsForModule(handle);
        raw = closeLibraryRaw((unsigned long)handle);
        result = (long)raw;
    }

    return result;
}

/* Release one module by path for compatibility with the older runtime helpers. */
static inline long freeLibraryByName(const char* path) {
    unsigned long raw = closeLibraryRaw((unsigned long)path);
    long result = (long)raw;

    if ((raw & USER_LOADER_ACTION_BIT) != 0UL && (raw & ~USER_LOADER_ACTION_BIT) != 0UL) {
        HMODULE module = raw & ~USER_LOADER_ACTION_BIT;

        (void)loaderCallModuleEntry(module, DLL_REASON_PROCESS_DETACH);
        (void)loaderDetachImportsForModule(module);
        raw = closeLibraryRaw((unsigned long)module);
        result = (long)raw;
    }

    return result;
}

static inline int loaderAttachImportsForModule(HMODULE module) {
    const dll_header* header = loaderImageHeader(module);
    const dll_import_module* importModules;
    unsigned long moduleIndex;

    if (header == 0 || header->import_module_count == 0U) {
        return 0;
    }

    importModules = (const dll_import_module*)((const unsigned char*)header + header->import_module_table_offset);
    for (moduleIndex = 0UL; moduleIndex < header->import_module_count; ++moduleIndex) {
        const char* importName = loaderStringAt(header, importModules[moduleIndex].module_name_offset);

        if (importName == 0 || importName[0] == '\0') {
            return -1;
        }
        if (loadLibrary(importName) == 0UL) {
            return -1;
        }
    }

    return 0;
}

static inline int loaderDetachImportsForModule(HMODULE module) {
    const dll_header* header = loaderImageHeader(module);
    const dll_import_module* importModules;
    long moduleIndex;

    if (header == 0 || header->import_module_count == 0U) {
        return 0;
    }

    importModules = (const dll_import_module*)((const unsigned char*)header + header->import_module_table_offset);
    for (moduleIndex = (long)header->import_module_count - 1; moduleIndex >= 0; --moduleIndex) {
        const char* importName = loaderStringAt(header, importModules[moduleIndex].module_name_offset);

        if (importName == 0 || importName[0] == '\0') {
            return -1;
        }
        if (freeLibraryByName(importName) < 0) {
            return -1;
        }
    }

    return 0;
}

static inline void attachCurrentImageImports(void) {
    (void)loaderAttachImportsForModule((HMODULE)USER_EXE_IMAGE_BASE);
}

static inline void detachCurrentImageImports(void) {
    (void)loaderDetachImportsForModule((HMODULE)USER_EXE_IMAGE_BASE);
}

/* Compatibility wrapper around the newer handle-style load helper. */
static inline unsigned long openSharedLibrary(const char* path) {
    return loadLibrary(path);
}

/* Compatibility wrapper around the newer handle-style close helper. */
static inline long closeSharedLibrary(const char* path, unsigned long base) {
    if (base != 0UL) {
        return freeLibrary((HMODULE)base);
    }

    return freeLibraryByName(path);
}

/*
 * Unload one user-space driver image.
 *
 * The driver path mirrors the shared-library close path and invokes Deinit when
 * the kernel asks userspace to perform the final teardown hook.
 */
static inline long unloadDriver(const char* path, unsigned long base) {
    (void)path;
    return freeLibrary((HMODULE)base);
}

#endif
