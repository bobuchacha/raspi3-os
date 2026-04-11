#ifndef APPLICATIONS_USER_RUNTIME_H
#define APPLICATIONS_USER_RUNTIME_H

#include "dll_image.h"
#include "user_ipc.h"
#include <stddef.h>

/* Loader metadata constants consumed by the user-image packer. */
#define USER_LDR_META_VERSION 2U
#define USER_LDR_META_KIND_IMPORT 1U
#define USER_LDR_META_KIND_EXPORT 2U
#define USER_LDR_META_NAME_MAX 64U

/* Keep the executable image header at the aligned EL0 image base used by the kernel loader. */
#define USER_EXE_IMAGE_BASE 0x00200000UL

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
#define DLL_IMPORT_FUNCTION(module_name_literal, symbol_name_literal, function_type, slot_symbol) \
    static function_type __attribute__((used, section(".data"))) slot_symbol = (function_type)0; \
    LDR_EMIT_IMPORT(module_name_literal, symbol_name_literal, #slot_symbol, slot_symbol)

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

/* Issue one zero-argument syscall. */
static inline unsigned long invokeSyscall0(unsigned long number) {
    register unsigned long x0 asm("x0");
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

/* Issue one one-argument syscall. */
static inline unsigned long invokeSyscall1(unsigned long number, unsigned long arg0) {
    register unsigned long x0 asm("x0") = arg0;
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

/* Issue one two-argument syscall. */
static inline unsigned long invokeSyscall2(unsigned long number, unsigned long arg0, unsigned long arg1) {
    register unsigned long x0 asm("x0") = arg0;
    register unsigned long x1 asm("x1") = arg1;
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
    return x0;
}

/* Issue one three-argument syscall. */
static inline unsigned long invokeSyscall3(unsigned long number, unsigned long arg0, unsigned long arg1, unsigned long arg2) {
    register unsigned long x0 asm("x0") = arg0;
    register unsigned long x1 asm("x1") = arg1;
    register unsigned long x2 asm("x2") = arg2;
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

/* Issue one four-argument syscall. */
static inline unsigned long invokeSyscall4(unsigned long number, unsigned long arg0, unsigned long arg1, unsigned long arg2, unsigned long arg3) {
    register unsigned long x0 asm("x0") = arg0;
    register unsigned long x1 asm("x1") = arg1;
    register unsigned long x2 asm("x2") = arg2;
    register unsigned long x3 asm("x3") = arg3;
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x8) : "memory");
    return x0;
}

/* Issue one five-argument syscall. */
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

/* Query one task descriptor by PID. */
static inline long getTaskInfo(long pid, UserTaskInfo* info) {
    return (long)invokeSyscall2(USER_SYS_TASK_INFO, (unsigned long)pid, (unsigned long)info);
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
