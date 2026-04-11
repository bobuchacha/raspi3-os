#include "app/syscall.h"
#include "kernel_event.h"
#include "user_runtime.h"
#include "stddef.h"
#include "stdarg.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

extern void __attribute__((weak)) runtimeRunInitArray(void);
extern void __attribute__((weak)) runtimeRunFiniArray(void);
extern int __attribute__((weak)) main(void);

typedef struct FormatSink {
    char* buffer;
    size_t capacity;
    size_t length;
} FormatSink;

/* Use the shared invokeSyscall helpers instead of ros_* wrappers. */

static void format_push_char(FormatSink* sink, char ch) {
    if (!sink) {
        return;
    }

    if (sink->buffer && sink->capacity > 0 && sink->length + 1 < sink->capacity) {
        sink->buffer[sink->length] = ch;
    }
    sink->length++;
}

static void format_push_text(FormatSink* sink, const char* text) {
    if (!text) {
        text = "(null)";
    }

    while (*text) {
        format_push_char(sink, *text++);
    }
}

static void format_push_padding(FormatSink* sink, int count, char pad) {
    while (count-- > 0) {
        format_push_char(sink, pad);
    }
}

static size_t format_text_length(const char* text) {
    size_t length = 0;

    if (!text) {
        return 6;
    }

    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static size_t format_unsigned(char* buffer, unsigned long long value, unsigned int base, int uppercase) {
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char* digits = uppercase ? digits_upper : digits_lower;
    char scratch[32];
    size_t length = 0;

    if (base < 2 || base > 16) {
        return 0;
    }
    if (value == 0) {
        buffer[0] = '0';
        return 1;
    }

    while (value != 0) {
        scratch[length++] = digits[value % base];
        value /= base;
    }

    for (size_t index = 0; index < length; index++) {
        buffer[index] = scratch[length - 1 - index];
    }
    return length;
}

static void format_number(
    FormatSink* sink,
    unsigned long long value,
    int negative,
    unsigned int base,
    int uppercase,
    int width,
    int zero_pad,
    int prefix_hex
) {
    char digits[32];
    size_t length = format_unsigned(digits, value, base, uppercase);
    int prefix_length = 0;

    if (negative) {
        prefix_length++;
    }
    if (prefix_hex) {
        prefix_length += 2;
    }

    if (!zero_pad) {
        format_push_padding(sink, width - (int)(length + (size_t)prefix_length), ' ');
    }
    if (negative) {
        format_push_char(sink, '-');
    }
    if (prefix_hex) {
        format_push_char(sink, '0');
        format_push_char(sink, uppercase ? 'X' : 'x');
    }
    if (zero_pad) {
        format_push_padding(sink, width - (int)(length + (size_t)prefix_length), '0');
    }

    for (size_t index = 0; index < length; index++) {
        format_push_char(sink, digits[index]);
    }
}

static int format_vprintf(FormatSink* sink, const char* fmt, va_list args) {
    while (fmt && *fmt) {
        int zero_pad = 0;
        int width = 0;
        int long_count = 0;
        char spec;

        if (*fmt != '%') {
            format_push_char(sink, *fmt++);
            continue;
        }

        fmt++;
        if (*fmt == '%') {
            format_push_char(sink, *fmt++);
            continue;
        }
        if (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = (width * 10) + (*fmt - '0');
            fmt++;
        }
        while (*fmt == 'l' || *fmt == 'z') {
            if (*fmt == 'l') {
                long_count++;
            }
            else {
                long_count = 1;
            }
            fmt++;
        }

        spec = *fmt ? *fmt++ : '\0';
        switch (spec) {
        case '\0':
            break;
        case 'c':
            format_push_char(sink, (char)va_arg(args, int));
            break;
        case 's': {
            const char* text = va_arg(args, const char*);
            size_t length = format_text_length(text);

            format_push_padding(sink, width - (int)length, ' ');
            format_push_text(sink, text);
            break;
        }
        case 'd':
        case 'i': {
            long long value = 0;
            unsigned long long magnitude;

            if (long_count > 1) {
                value = va_arg(args, long long);
            }
            else if (long_count == 1) {
                value = va_arg(args, long);
            }
            else {
                value = va_arg(args, int);
            }
            magnitude = value < 0 ? (unsigned long long)(-value) : (unsigned long long)value;
            format_number(sink, magnitude, value < 0, 10, 0, width, zero_pad, 0);
            break;
        }
        case 'u': {
            unsigned long long value;

            if (long_count > 1) {
                value = va_arg(args, unsigned long long);
            }
            else if (long_count == 1) {
                value = va_arg(args, unsigned long);
            }
            else {
                value = va_arg(args, unsigned int);
            }
            format_number(sink, value, 0, 10, 0, width, zero_pad, 0);
            break;
        }
        case 'x':
        case 'X': {
            unsigned long long value;

            if (long_count > 1) {
                value = va_arg(args, unsigned long long);
            }
            else if (long_count == 1) {
                value = va_arg(args, unsigned long);
            }
            else {
                value = va_arg(args, unsigned int);
            }
            format_number(sink, value, 0, 16, spec == 'X', width, zero_pad, 0);
            break;
        }
        case 'p': {
            unsigned long long value = (unsigned long long)(uintptr_t)va_arg(args, void*);
            format_number(sink, value, 0, 16, 0, width > 0 ? width : 2, 1, 1);
            break;
        }
        default:
            format_push_char(sink, '%');
            format_push_char(sink, spec);
            break;
        }
    }

    if (sink && sink->buffer && sink->capacity > 0) {
        size_t terminator = sink->length < sink->capacity ? sink->length : sink->capacity - 1;
        sink->buffer[terminator] = '\0';
    }
    return sink ? (int)sink->length : 0;
}

long syscall(long number, ...) {
    va_list args;
    unsigned long arg0 = 0;
    unsigned long arg1 = 0;
    unsigned long arg2 = 0;
    unsigned long arg3 = 0;
    unsigned long arg4 = 0;

    va_start(args, number);
    switch (number) {
    case SYS_CONSOLE_READ:
    case SYS_REBOOT:
    case SYS_UPTIME_MSEC:
        va_end(args);
        return (long)invokeSyscall0((unsigned long)number);
    case SYS_WRITE:
    case SYS_MALLOC:
    case SYS_FREE:
    case SYS_EXIT:
    case SYS_GET_PARAM:
    case SYS_SLEEP:
    case SYS_EXEC:
    case SYS_SHLIB_OPEN:
    case SYS_MKDIR:
    case SYS_DEBUG_SHELL:
    case SYS_SHLIB_CLOSE:
    case SYS_DRIVER_UNLOAD:
    case SYS_LOG_SEND:
    case SYS_LOG_WRITE:
    case SYS_EVENT_WAIT:
    case SYS_EVENT_UNSUBSCRIBE:
    case SYS_REMOVE:
        arg0 = va_arg(args, unsigned long);
        va_end(args);
        return (long)invokeSyscall1((unsigned long)number, arg0);
    case SYS_TASK_NAME:
    case SYS_TASK_ARGS:
    case SYS_SHLIB_LOCAL:
    case SYS_TASK_INFO:
    case SYS_WAIT_PID:
    case SYS_MEM_INFO:
    case SYS_KILL:
    case SYS_SHLIB_EXPORT:
    case SYS_GUI_CONTROL:
    case SYS_GUI_SET_TASKBAR_TEXT:
    case SYS_LOG_RECV:
    case SYS_EVENT_SUBSCRIBE:
    case SYS_EVENT_QUERY:
    case SYS_IPC_SEND:
    case SYS_IPC_RECV:
        arg0 = va_arg(args, unsigned long);
        arg1 = va_arg(args, unsigned long);
        va_end(args);
        return (long)invokeSyscall2((unsigned long)number, arg0, arg1);
    case SYS_SPAWN:
    case SYS_DIR_ENTRY:
        arg0 = va_arg(args, unsigned long);
        arg1 = va_arg(args, unsigned long);
        arg2 = va_arg(args, unsigned long);
        va_end(args);
        return (long)invokeSyscall3((unsigned long)number, arg0, arg1, arg2);
    case SYS_READ_FILE:
    case SYS_EXT_INVOKE:
    case SYS_EVENT_READ:
        arg0 = va_arg(args, unsigned long);
        arg1 = va_arg(args, unsigned long);
        arg2 = va_arg(args, unsigned long);
        arg3 = va_arg(args, unsigned long);
        va_end(args);
        return (long)invokeSyscall4((unsigned long)number, arg0, arg1, arg2, arg3);
    case SYS_MODULE_INVOKE:
        arg0 = va_arg(args, unsigned long);
        arg1 = va_arg(args, unsigned long);
        arg2 = va_arg(args, unsigned long);
        arg3 = va_arg(args, unsigned long);
        arg4 = va_arg(args, unsigned long);
        va_end(args);
        return (long)invokeSyscall5((unsigned long)number, arg0, arg1, arg2, arg3, arg4);
    default:
        va_end(args);
        return -1;
    }
}

long call_sys_write(char* buf) {
    return syscall(SYS_WRITE, (unsigned long)buf);
}

unsigned long call_sys_malloc(unsigned long value) {
    long result = syscall(SYS_MALLOC, value);

    return (result < 0L) ? 0UL : (unsigned long)result;
}

long call_sys_free(void* ptr) {
    return syscall(SYS_FREE, (unsigned long)ptr);
}

long call_sys_exit(unsigned long result) {
    return syscall(SYS_EXIT, result);
}

long call_sys_fork(void) {
    return -1;
}

int call_sys_get_param(int param) {
    return (int)syscall(SYS_GET_PARAM, (unsigned long)param);
}

long call_sys_sleep(unsigned long msec) {
    return syscall(SYS_SLEEP, msec);
}

long call_sys_exec(const char* path) {
    return syscall(SYS_EXEC, (unsigned long)path);
}

unsigned long call_sys_shlib_open(const char* path) {
    return (unsigned long)syscall(SYS_SHLIB_OPEN, (unsigned long)path);
}

unsigned long call_sys_shlib_local(const char* path, unsigned long size) {
    return (unsigned long)syscall(SYS_SHLIB_LOCAL, (unsigned long)path, size);
}

long call_sys_task_name(char* buf, unsigned long size) {
    return syscall(SYS_TASK_NAME, (unsigned long)buf, size);
}

long call_sys_spawn(const char* path, const char* name, const char* args) {
    return syscall(SYS_SPAWN, (unsigned long)path, (unsigned long)name, (unsigned long)args);
}

long call_sys_task_args(char* buf, unsigned long size) {
    return syscall(SYS_TASK_ARGS, (unsigned long)buf, size);
}

long call_sys_console_read(void) {
    return syscall(SYS_CONSOLE_READ);
}

long call_sys_read_file(const char* path, unsigned long offset, char* buf, unsigned long size) {
    return syscall(SYS_READ_FILE, (unsigned long)path, offset, (unsigned long)buf, size);
}

long call_sys_dir_entry(const char* path, unsigned long entry_index, UserDirectoryEntry* entry) {
    return syscall(SYS_DIR_ENTRY, (unsigned long)path, entry_index, (unsigned long)entry);
}

long call_sys_mkdir(const char* path) {
    return syscall(SYS_MKDIR, (unsigned long)path);
}

long call_sys_remove(const char* path) {
    return syscall(SYS_REMOVE, (unsigned long)path);
}

long call_sys_task_info(long pid, UserTaskInfo* info) {
    return syscall(SYS_TASK_INFO, (unsigned long)pid, (unsigned long)info);
}

long call_sys_wait_pid(long pid, long* result) {
    return syscall(SYS_WAIT_PID, (unsigned long)pid, (unsigned long)result);
}

long call_sys_mem_info(UserMemInfo* info) {
    return syscall(SYS_MEM_INFO, (unsigned long)info, 0UL);
}

long call_sys_kill(long pid) {
    return syscall(SYS_KILL, (unsigned long)pid, 0UL);
}

long call_sys_reboot(void) {
    return syscall(SYS_REBOOT);
}

long call_sys_debug_shell(const char* line) {
    return syscall(SYS_DEBUG_SHELL, (unsigned long)line);
}

long call_sys_module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result) {
    return syscall(SYS_MODULE_INVOKE, (unsigned long)module_name, (unsigned long)export_name, a, b, (unsigned long)result);
}

unsigned long call_sys_shlib_export(const char* path, const char* export_name) {
    return (unsigned long)syscall(SYS_SHLIB_EXPORT, (unsigned long)path, (unsigned long)export_name);
}

long call_sys_shlib_close(const char* path) {
    return syscall(SYS_SHLIB_CLOSE, (unsigned long)path);
}

long call_sys_driver_unload(const char* path) {
    return syscall(SYS_DRIVER_UNLOAD, (unsigned long)path);
}

long call_sys_ext_invoke(const char* ext, const char* func, unsigned long a, unsigned long b) {
    return syscall(SYS_EXT_INVOKE, (unsigned long)ext, (unsigned long)func, a, b);
}

long call_sys_gui_control(unsigned long command, unsigned long value) {
    return syscall(SYS_GUI_CONTROL, command, value);
}

long call_sys_gui_set_taskbar_text(const char* clock_text, const char* date_text) {
    return syscall(SYS_GUI_SET_TASKBAR_TEXT, (unsigned long)clock_text, (unsigned long)date_text);
}

unsigned long call_sys_uptime_msec(void) {
    return (unsigned long)syscall(SYS_UPTIME_MSEC);
}

long call_sys_log_send(const char* text) {
    return syscall(SYS_LOG_SEND, (unsigned long)text);
}

long call_sys_log_recv(char* buffer, unsigned long size) {
    return syscall(SYS_LOG_RECV, (unsigned long)buffer, size);
}

long call_sys_log_write(const char* text) {
    return syscall(SYS_LOG_WRITE, (unsigned long)text);
}

long call_sys_event_subscribe(const KernelEventSubscriptionRequest* request, KernelEventSubscriptionId* subscription_id) {
    return syscall(SYS_EVENT_SUBSCRIBE, (unsigned long)request, (unsigned long)subscription_id);
}

long call_sys_event_read(KernelEventSubscriptionId subscription_id, KernelEventRecord* records, U32 capacity, U32* record_count) {
    return syscall(SYS_EVENT_READ, (unsigned long)subscription_id, (unsigned long)records, (unsigned long)capacity, (unsigned long)record_count);
}

long call_sys_event_query(KernelEventSubscriptionId subscription_id, KernelEventSubscriptionInfo* info) {
    return syscall(SYS_EVENT_QUERY, (unsigned long)subscription_id, (unsigned long)info);
}

long call_sys_event_wait(unsigned long subscription_id) {
    return syscall(SYS_EVENT_WAIT, subscription_id);
}

long call_sys_event_unsubscribe(KernelEventSubscriptionId subscription_id) {
    return syscall(SYS_EVENT_UNSUBSCRIBE, (unsigned long)subscription_id);
}

long call_sys_ipc_send(long receiver_pid, const UserIpcMessage* message) {
    return syscall(SYS_IPC_SEND, (unsigned long)receiver_pid, (unsigned long)message);
}

long call_sys_ipc_recv(UserIpcMessage* message, unsigned long flags) {
    return syscall(SYS_IPC_RECV, (unsigned long)message, flags);
}

void user_delay(unsigned long msec) {
    (void)sleepMs(msec);
}

void user_spin_delay(unsigned long count) {
    for (volatile unsigned long spin = 0; spin < count; spin++) {
        asm volatile("" ::: "memory");
    }
}

void* memcpy(void* dest, const void* src, size_t size) {
    unsigned char* dst = (unsigned char*)dest;
    const unsigned char* source = (const unsigned char*)src;

    while (size-- > 0) {
        *dst++ = *source++;
    }
    return dest;
}

void* memmove(void* dest, const void* src, size_t size) {
    unsigned char* dst = (unsigned char*)dest;
    const unsigned char* source = (const unsigned char*)src;

    if (source < dst && source + size > dst) {
        dst += size;
        source += size;
        while (size-- > 0) {
            *--dst = *--source;
        }
        return dest;
    }

    while (size-- > 0) {
        *dst++ = *source++;
    }
    return dest;
}

void* memset(void* dest, int value, size_t size) {
    unsigned char* dst = (unsigned char*)dest;

    while (size-- > 0) {
        *dst++ = (unsigned char)value;
    }
    return dest;
}

int memcmp(const void* lhs, const void* rhs, size_t size) {
    const unsigned char* left = (const unsigned char*)lhs;
    const unsigned char* right = (const unsigned char*)rhs;

    while (size-- > 0) {
        if (*left != *right) {
            return (int)*left - (int)*right;
        }
        left++;
        right++;
    }
    return 0;
}

size_t strlen(const char* text) {
    size_t length = 0;

    while (text && text[length] != '\0') {
        length++;
    }
    return length;
}

size_t strnlen(const char* text, size_t max_size) {
    size_t length = 0;

    while (text && length < max_size && text[length] != '\0') {
        length++;
    }
    return length;
}

int strcmp(const char* lhs, const char* rhs) {
    while (*lhs != '\0' && *lhs == *rhs) {
        lhs++;
        rhs++;
    }
    return (int)(unsigned char)*lhs - (int)(unsigned char)*rhs;
}

int strncmp(const char* lhs, const char* rhs, size_t size) {
    while (size-- > 0) {
        if (*lhs != *rhs) {
            return (int)(unsigned char)*lhs - (int)(unsigned char)*rhs;
        }
        if (*lhs == '\0') {
            return 0;
        }
        lhs++;
        rhs++;
    }
    return 0;
}

char* strcpy(char* dest, const char* src) {
    char* cursor = dest;

    while ((*cursor++ = *src++) != '\0') {
    }
    return dest;
}

char* strncpy(char* dest, const char* src, size_t size) {
    char* cursor = dest;

    while (size > 0 && *src != '\0') {
        *cursor++ = *src++;
        size--;
    }
    while (size > 0) {
        *cursor++ = '\0';
        size--;
    }
    return dest;
}

void* malloc(size_t size) {
    typedef struct UserAllocationHeader {
        size_t payload_bytes;
    } UserAllocationHeader;

    const size_t payload_bytes = size ? size : 1U;
    const size_t total_bytes = payload_bytes + sizeof(UserAllocationHeader);
    UserAllocationHeader* header = (UserAllocationHeader*)(unsigned long)call_sys_malloc((unsigned long)total_bytes);

    if (!header) {
        return NULL;
    }

    header->payload_bytes = payload_bytes;
    return (void*)(header + 1);
}

void free(void* ptr) {
    typedef struct UserAllocationHeader {
        size_t payload_bytes;
    } UserAllocationHeader;
    UserAllocationHeader* header;

    if (!ptr) {
        return;
    }

    header = ((UserAllocationHeader*)ptr) - 1;
    if (call_sys_free(header) < 0) {
        // `free` cannot propagate a failure in the freestanding C ABI, but the
        // runtime still checks the syscall result instead of dropping it.
    }
}

void* realloc(void* ptr, size_t size) {
    typedef struct UserAllocationHeader {
        size_t payload_bytes;
    } UserAllocationHeader;
    UserAllocationHeader* header;
    size_t copy_bytes;
    void* replacement;

    if (ptr == NULL) {
        return malloc(size);
    }
    if (size == 0U) {
        free(ptr);
        return NULL;
    }

    header = ((UserAllocationHeader*)ptr) - 1;
    copy_bytes = header->payload_bytes < size ? header->payload_bytes : size;
    replacement = malloc(size);
    if (replacement == NULL) {
        return NULL;
    }

    memmove(replacement, ptr, copy_bytes);
    free(ptr);
    return replacement;
}

void* calloc(size_t count, size_t size) {
    size_t total = count * size;
    void* memory = malloc(total ? total : 1);

    if (memory) {
        memset(memory, 0, total);
    }
    return memory;
}

void abort(void) {
    if (writeLine("user abort") < 0) {
        // Aborting still needs to terminate the process even if the diagnostic write fails.
    }
    exitProcess(255);
}

void exit(int code) {
    exitProcess((unsigned long)(unsigned int)code);
}

/*
 * Weak fallback for executables that only provide `main`.
 *
 * The packed image now enters through `_start`, not directly through an
 * application-owned `AppMain`. Keeping this fallback weak preserves the older
 * `AppMain` contract while still letting plain hosted-style programs expose a
 * normal `main` function.
 */
int __attribute__((weak)) AppMain(void) {
    if (!main) {
        if (writeLine("missing main/AppMain entry") < 0) {
            // There is no recovery path here besides termination.
        }
        return 127;
    }

    return main();
}

/*
 * Canonical executable entrypoint for all packed user EXEs.
 *
 * Launching EL0 code directly at an application-owned `AppMain` leaves the
 * initial link register undefined, so a returning app can jump to stale kernel
 * state instead of terminating cleanly. Entering through `_start` centralizes
 * constructor/destructor sequencing and guarantees every returning executable
 * leaves through the exit syscall.
 */
void __attribute__((noreturn, used)) _start(void) {
    int status;

    attachCurrentImageImports();

    if (runtimeRunInitArray) {
        runtimeRunInitArray();
    }

    status = AppMain();

    if (runtimeRunFiniArray) {
        runtimeRunFiniArray();
    }

    detachCurrentImageImports();

    exitProcess((unsigned long)(unsigned int)status);
}

int vsnprintf(char* buffer, size_t size, const char* fmt, va_list args) {
    FormatSink sink;

    sink.buffer = buffer;
    sink.capacity = size;
    sink.length = 0;
    return format_vprintf(&sink, fmt, args);
}

int snprintf(char* buffer, size_t size, const char* fmt, ...) {
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(buffer, size, fmt, args);
    va_end(args);
    return written;
}

int printf(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (writeText(buffer) < 0) {
        return -1;
    }
    return written;
}

int puts(const char* text) {
    if (writeText(text ? text : "(null)") < 0) {
        return -1;
    }
    if (writeText("\r\n") < 0) {
        return -1;
    }
    return 0;
}

void fprint(char* buffer, const char* fmt, ...) {
    va_list args;

    va_start(args, fmt);
    (void)vsnprintf(buffer, (size_t)-1, fmt, args);
    va_end(args);
}