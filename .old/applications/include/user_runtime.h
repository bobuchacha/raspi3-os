#ifndef APPLICATIONS_USER_RUNTIME_H
#define APPLICATIONS_USER_RUNTIME_H

#include <stddef.h>

/* Loader metadata constants consumed by the user-image packer. */
#define USER_LDR_META_VERSION 2U
#define USER_LDR_META_KIND_EXPORT 2U
#define USER_LDR_META_NAME_MAX 64U

/* Userspace syscall numbers mirrored from the kernel ABI. */
#define USER_SYS_WRITE 0UL
#define USER_SYS_EXIT 4UL
#define USER_SYS_SLEEP 6UL
#define USER_SYS_SHLIB_OPEN 8UL
#define USER_SYS_TASK_NAME 9UL
#define USER_SYS_SPAWN 10UL
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
    long state;
    long exit_code;
    long counter;
    long priority;
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
 * into the packed LRD0 export table. This keeps the public ABI explicit and
 * prevents runtime helper symbols from leaking into DLL or SYS exports.
 */
typedef struct __attribute__((packed)) UserLoaderExportMeta {
    unsigned short version;
    unsigned short kind;
    char export_name[USER_LDR_META_NAME_MAX];
    char symbol_name[USER_LDR_META_NAME_MAX];
} UserLoaderExportMeta;

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

/* Forward declaration used by the cached export resolver. */
static inline unsigned long exportSharedLibrary(const char* path, const char* exportName);

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

/* Write one raw text buffer to the kernel console. */
static inline void writeText(const char* text) {
    invokeSyscall1(USER_SYS_WRITE, (unsigned long)text);
}

/* Write one text line followed by CRLF to the kernel console. */
static inline void writeLine(const char* text) {
    writeText(text);
    writeText("\r\n");
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

/* Read one character from the console input stream. */
static inline long readConsole(void) {
    return (long)invokeSyscall0(USER_SYS_CONSOLE_READ);
}

/* Read bytes from one file path into a caller-provided buffer. */
static inline long readFile(const char* path, unsigned long offset, char* buffer, unsigned long size) {
    return (long)invokeSyscall4(USER_SYS_READ_FILE, (unsigned long)path, offset, (unsigned long)buffer, size);
}

/* Read one directory entry by index from a filesystem path. */
static inline long readDirectoryEntry(const char* path, unsigned long entryIndex, UserDirectoryEntry* entry) {
    return (long)invokeSyscall3(USER_SYS_DIR_ENTRY, (unsigned long)path, entryIndex, (unsigned long)entry);
}

/* Create one directory path in the userspace VFS. */
static inline long makeDirectory(const char* path) {
    return (long)invokeSyscall1(USER_SYS_MKDIR, (unsigned long)path);
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

/* Reboot the machine through the kernel service path. */
static inline void rebootSystem(void) {
    (void)invokeSyscall0(USER_SYS_REBOOT);
}

/* Execute one kernel debug-shell command. */
static inline long runDebugShell(const char* line) {
    return (long)invokeSyscall1(USER_SYS_DEBUG_SHELL, (unsigned long)line);
}

/*
 * Open one shared library or user driver image.
 *
 * When the loader returns the action bit, this helper resolves the Init export
 * and invokes it immediately with the mapped base address.
 */
static inline unsigned long openSharedLibrary(const char* path) {
    unsigned long raw = invokeSyscall1(USER_SYS_SHLIB_OPEN, (unsigned long)path);
    unsigned long base = (raw & ~USER_LOADER_ACTION_BIT);

    if ((raw & USER_LOADER_ACTION_BIT) != 0UL && base != 0UL) {
        unsigned long initAddress = exportSharedLibrary(path, "Init");

        if (initAddress != 0UL) {
            typedef int (*ModuleInitFn)(void* baseAddress);
            ((ModuleInitFn)initAddress)((void*)base);
        }
    }

    return base;
}

/* Spawn one new user task with an optional visible name and launch arguments. */
static inline long spawnTask(const char* path, const char* name, const char* args) {
    return (long)invokeSyscall3(USER_SYS_SPAWN, (unsigned long)path, (unsigned long)name, (unsigned long)args);
}

/* Resolve one exported symbol from a loaded shared library image. */
static inline unsigned long exportSharedLibrary(const char* path, const char* exportName) {
    return invokeSyscall2(USER_SYS_SHLIB_EXPORT, (unsigned long)path, (unsigned long)exportName);
}

/*
 * Close one shared library mapping.
 *
 * When the loader requests a Deinit call, this helper resolves and invokes the
 * export before retrying the close syscall to release the last reference.
 */
static inline long closeSharedLibrary(const char* path, unsigned long base) {
    unsigned long raw = invokeSyscall1(USER_SYS_SHLIB_CLOSE, (unsigned long)path);
    long result = (long)raw;

    if ((raw & USER_LOADER_ACTION_BIT) != 0UL && (raw & ~USER_LOADER_ACTION_BIT) != 0UL) {
        unsigned long deinitAddress = exportSharedLibrary(path, "Deinit");

        if (deinitAddress != 0UL) {
            typedef int (*ModuleDeinitFn)(void* baseAddress);
            ((ModuleDeinitFn)deinitAddress)((void*)(base != 0UL ? base : (raw & ~USER_LOADER_ACTION_BIT)));
        }

        raw = invokeSyscall1(USER_SYS_SHLIB_CLOSE, (unsigned long)path);
        result = (long)raw;
    }

    return result;
}

/*
 * Unload one user-space driver image.
 *
 * The driver path mirrors the shared-library close path and invokes Deinit when
 * the kernel asks userspace to perform the final teardown hook.
 */
static inline long unloadDriver(const char* path, unsigned long base) {
    unsigned long raw = invokeSyscall1(USER_SYS_DRIVER_UNLOAD, (unsigned long)path);
    long result = (long)raw;

    if ((raw & USER_LOADER_ACTION_BIT) != 0UL && (raw & ~USER_LOADER_ACTION_BIT) != 0UL) {
        unsigned long deinitAddress = exportSharedLibrary(path, "Deinit");

        if (deinitAddress != 0UL) {
            typedef int (*ModuleDeinitFn)(void* baseAddress);
            ((ModuleDeinitFn)deinitAddress)((void*)(base != 0UL ? base : (raw & ~USER_LOADER_ACTION_BIT)));
        }

        raw = invokeSyscall1(USER_SYS_DRIVER_UNLOAD, (unsigned long)path);
        result = (long)raw;
    }

    return result;
}

#endif
