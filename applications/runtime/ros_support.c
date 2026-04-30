#include "app/syscall.h"
#include "kernel_event.h"
#include "user_runtime.h"
#include "stddef.h"
#include "stdarg.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

/*
 * The host editor parses this file with macOS SDK headers, which may expose
 * fortified libc entry points as function-like macros. The runtime provides its
 * own freestanding implementations below, so clear any host-side macro aliases
 * first to keep editor diagnostics aligned with the real cross-compiler build.
 */
#ifdef memcpy
#undef memcpy
#endif
#ifdef memmove
#undef memmove
#endif
#ifdef memset
#undef memset
#endif
#ifdef strcpy
#undef strcpy
#endif
#ifdef strncpy
#undef strncpy
#endif
#ifdef vsnprintf
#undef vsnprintf
#endif
#ifdef snprintf
#undef snprintf
#endif

extern void __attribute__((weak)) runtimeRunInitArray(void);
extern void __attribute__((weak)) runtimeRunFiniArray(void);
extern int __attribute__((weak)) main(void);

typedef struct FormatSink {
    char* buffer;
    size_t capacity;
    size_t length;
} FormatSink;

typedef struct UserLaunchPendingRequest {
    unsigned long request_id;
    SpawnTaskAsyncSuccessCallback success_callback;
    SpawnTaskAsyncErrorCallback error_callback;
    void* context;
    char* path;
    char* name;
    char* args;
    unsigned long deadline_msec;
    struct UserLaunchPendingRequest* next;
    struct UserLaunchPendingRequest* prev;
} UserLaunchPendingRequest;

typedef struct UserLaunchDeferredCompletion {
    UserAsyncSpawnResult result;
    struct UserLaunchDeferredCompletion* next;
    struct UserLaunchDeferredCompletion* prev;
} UserLaunchDeferredCompletion;

typedef struct UserLaunchIgnoredCompletion {
    unsigned long request_id;
    unsigned long expires_msec;
    struct UserLaunchIgnoredCompletion* next;
    struct UserLaunchIgnoredCompletion* prev;
} UserLaunchIgnoredCompletion;

#define USER_LAUNCH_CALLBACK_TIMEOUT_MSEC 5000UL
#define USER_LAUNCH_IGNORED_COMPLETION_GRACE_MSEC 30000UL

static volatile unsigned long g_user_launch_lock_word = 0UL;
static UserLaunchPendingRequest* g_user_launch_pending_head = NULL;
static UserLaunchPendingRequest* g_user_launch_pending_tail = NULL;
static UserLaunchDeferredCompletion* g_user_launch_completion_head = NULL;
static UserLaunchDeferredCompletion* g_user_launch_completion_tail = NULL;
static UserLaunchIgnoredCompletion* g_user_launch_ignored_head = NULL;
static UserLaunchIgnoredCompletion* g_user_launch_ignored_tail = NULL;
static volatile int g_user_launch_dispatcher_started = 0;

static void user_launch_lock(void);
static void user_launch_unlock(void);
static int user_launch_try_lock_once(void);
static char* user_launch_duplicate_text(const char* text);
static void user_launch_free_pending_request(UserLaunchPendingRequest* request);
static void user_launch_append_pending_request(UserLaunchPendingRequest* request);
static UserLaunchPendingRequest* user_launch_take_pending_request(unsigned long request_id);
static UserLaunchPendingRequest* user_launch_take_timed_out_request(unsigned long now_msec);
static void user_launch_append_deferred_completion(const UserAsyncSpawnResult* result);
static int user_launch_take_deferred_completion(unsigned long request_id, UserAsyncSpawnResult* result);
static void user_launch_append_ignored_completion(unsigned long request_id, unsigned long expires_msec);
static int user_launch_take_ignored_completion(unsigned long request_id);
static void user_launch_prune_ignored_completions(unsigned long now_msec);
static void user_launch_dispatch_result(UserLaunchPendingRequest* request, const UserAsyncSpawnResult* result);
static void user_launch_dispatch_timeout(UserLaunchPendingRequest* request);
static long user_launch_ensure_dispatcher_thread(void);
static void user_launch_dispatcher_thread_entry(unsigned long argument);

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

/*
 * Acquire the process-local async-launch registry lock.
 *
 * The runtime helper can be called from the UI thread while the completion
 * dispatcher is retiring finished launches on a worker thread. The registry is
 * tiny, but its intrusive lists still need one serialized owner.
 *
 * @return Nothing.
 */
static void user_launch_lock(void) {
    while (!user_launch_try_lock_once()) {
    }
}

/*
 * Release the process-local async-launch registry lock.
 *
 * @return Nothing.
 */
static void user_launch_unlock(void) {
    const unsigned long unlocked = 0UL;

    asm volatile("stlr %1, [%0]" : : "r"(&g_user_launch_lock_word), "r"(unlocked) : "memory");
}

/*
 * Attempt one lock acquire on the async-launch registry.
 *
 * Keeping the lock primitive local avoids libatomic helper imports, which is
 * the same constraint that already exists in the window runtime.
 *
 * @return Non-zero when the lock was acquired on this attempt.
 */
static int user_launch_try_lock_once(void) {
    unsigned long observed = 0UL;
    unsigned int store_failed = 0U;
    const unsigned long locked = 1UL;

    asm volatile(
        "ldaxr %0, [%2]\n"
        "cbnz %0, 1f\n"
        "stxr %w1, %3, [%2]\n"
        "b 2f\n"
        "1:\n"
        "mov %w1, #1\n"
        "2:\n"
        : "=&r"(observed), "=&r"(store_failed)
        : "r"(&g_user_launch_lock_word), "r"(locked)
        : "memory");

    return (observed == 0UL) && (store_failed == 0U);
}

/*
 * Duplicate one optional text value for the async-launch registry.
 *
 * The public helper accepts caller-owned strings. The registry must keep its
 * own durable copies because the callback may run long after the original
 * stack or buffer went out of scope.
 *
 * @param text Optional null-terminated source string.
 * @return Heap-owned copy, or NULL when the input is null or allocation fails.
 */
static char* user_launch_duplicate_text(const char* text) {
    size_t length;
    char* copy;

    if (text == NULL) {
        return NULL;
    }

    length = strlen(text);
    copy = (char*)malloc(length + 1U);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, length + 1U);
    return copy;
}

/*
 * Release one pending async-launch request record.
 *
 * @param request Registry node to release.
 * @return Nothing.
 */
static void user_launch_free_pending_request(UserLaunchPendingRequest* request) {
    if (request == NULL) {
        return;
    }

    free(request->path);
    free(request->name);
    free(request->args);
    free(request);
}

/*
 * Append one pending async-launch request to the process-local registry.
 *
 * @param request Fully initialized request node.
 * @return Nothing.
 */
static void user_launch_append_pending_request(UserLaunchPendingRequest* request) {
    if (request == NULL) {
        return;
    }

    request->next = NULL;
    request->prev = g_user_launch_pending_tail;
    if (g_user_launch_pending_tail != NULL) {
        g_user_launch_pending_tail->next = request;
    }
    else {
        g_user_launch_pending_head = request;
    }
    g_user_launch_pending_tail = request;
}

/*
 * Detach one pending async-launch request by request id.
 *
 * @param request_id Request identifier to remove.
 * @return Matching node, or NULL when it is not pending locally.
 */
static UserLaunchPendingRequest* user_launch_take_pending_request(unsigned long request_id) {
    UserLaunchPendingRequest* request;

    for (request = g_user_launch_pending_head; request != NULL; request = request->next) {
        if (request->request_id != request_id) {
            continue;
        }

        if (request->prev != NULL) {
            request->prev->next = request->next;
        }
        else {
            g_user_launch_pending_head = request->next;
        }
        if (request->next != NULL) {
            request->next->prev = request->prev;
        }
        else {
            g_user_launch_pending_tail = request->prev;
        }

        request->next = NULL;
        request->prev = NULL;
        return request;
    }

    return NULL;
}

/*
 * Detach one pending async-launch request whose timeout deadline already passed.
 *
 * The timeout path exists to surface failures even when no completion ever
 * arrives back in user mode. Returning only one expired request per dispatcher
 * loop keeps callback ordering deterministic and avoids monopolizing the helper
 * thread when many launches expire together.
 *
 * @param now_msec Current monotonic uptime snapshot.
 * @return Matching request, or NULL when nothing has timed out yet.
 */
static UserLaunchPendingRequest* user_launch_take_timed_out_request(unsigned long now_msec) {
    UserLaunchPendingRequest* request;

    for (request = g_user_launch_pending_head; request != NULL; request = request->next) {
        if (request->deadline_msec == 0UL || now_msec < request->deadline_msec) {
            continue;
        }

        if (request->prev != NULL) {
            request->prev->next = request->next;
        }
        else {
            g_user_launch_pending_head = request->next;
        }
        if (request->next != NULL) {
            request->next->prev = request->prev;
        }
        else {
            g_user_launch_pending_tail = request->prev;
        }

        request->next = NULL;
        request->prev = NULL;
        return request;
    }

    return NULL;
}

/*
 * Preserve one completion that arrived before its local request record became visible.
 *
 * The kernel can finish a very fast async launch before the user helper has
 * linked the callback registration. Preserving unmatched completions closes
 * that race without forcing the kernel to know about user callback state.
 *
 * @param result Completion record to preserve.
 * @return Nothing.
 */
static void user_launch_append_deferred_completion(const UserAsyncSpawnResult* result) {
    UserLaunchDeferredCompletion* node;

    if (result == NULL) {
        return;
    }

    node = (UserLaunchDeferredCompletion*)malloc(sizeof(*node));
    if (node == NULL) {
        return;
    }

    memset(node, 0, sizeof(*node));
    node->result = *result;
    node->next = NULL;
    node->prev = g_user_launch_completion_tail;
    if (g_user_launch_completion_tail != NULL) {
        g_user_launch_completion_tail->next = node;
    }
    else {
        g_user_launch_completion_head = node;
    }
    g_user_launch_completion_tail = node;
}

/*
 * Remove one preserved completion by request id.
 *
 * @param request_id Request identifier to match.
 * @param result Receives the preserved completion on success.
 * @return Non-zero when a preserved completion was removed.
 */
static int user_launch_take_deferred_completion(unsigned long request_id, UserAsyncSpawnResult* result) {
    UserLaunchDeferredCompletion* node;

    if (result == NULL) {
        return 0;
    }

    for (node = g_user_launch_completion_head; node != NULL; node = node->next) {
        if (node->result.request_id != request_id) {
            continue;
        }

        *result = node->result;
        if (node->prev != NULL) {
            node->prev->next = node->next;
        }
        else {
            g_user_launch_completion_head = node->next;
        }
        if (node->next != NULL) {
            node->next->prev = node->prev;
        }
        else {
            g_user_launch_completion_tail = node->prev;
        }

        free(node);
        return 1;
    }

    return 0;
}

/*
 * Remember one request id whose timeout already notified user mode.
 *
 * Late kernel completions for timed-out requests should be discarded instead of
 * resurfacing as deferred completions after the caller already handled the
 * failure path. The grace window keeps the ignore set bounded even if a kernel
 * completion never materializes.
 *
 * @param request_id Timed-out request identifier.
 * @param expires_msec Monotonic uptime after which the ignore marker can be dropped.
 * @return Nothing.
 */
static void user_launch_append_ignored_completion(unsigned long request_id, unsigned long expires_msec) {
    UserLaunchIgnoredCompletion* node;

    node = (UserLaunchIgnoredCompletion*)malloc(sizeof(*node));
    if (node == NULL) {
        return;
    }

    memset(node, 0, sizeof(*node));
    node->request_id = request_id;
    node->expires_msec = expires_msec;
    node->next = NULL;
    node->prev = g_user_launch_ignored_tail;
    if (g_user_launch_ignored_tail != NULL) {
        g_user_launch_ignored_tail->next = node;
    }
    else {
        g_user_launch_ignored_head = node;
    }
    g_user_launch_ignored_tail = node;
}

/*
 * Remove one ignore marker when a late completion finally arrives.
 *
 * @param request_id Completed request identifier.
 * @return Non-zero when the completion should be discarded.
 */
static int user_launch_take_ignored_completion(unsigned long request_id) {
    UserLaunchIgnoredCompletion* node;

    for (node = g_user_launch_ignored_head; node != NULL; node = node->next) {
        if (node->request_id != request_id) {
            continue;
        }

        if (node->prev != NULL) {
            node->prev->next = node->next;
        }
        else {
            g_user_launch_ignored_head = node->next;
        }
        if (node->next != NULL) {
            node->next->prev = node->prev;
        }
        else {
            g_user_launch_ignored_tail = node->prev;
        }

        free(node);
        return 1;
    }

    return 0;
}

/*
 * Drop stale ignore markers after their grace window expires.
 *
 * @param now_msec Current monotonic uptime snapshot.
 * @return Nothing.
 */
static void user_launch_prune_ignored_completions(unsigned long now_msec) {
    UserLaunchIgnoredCompletion* node = g_user_launch_ignored_head;

    while (node != NULL) {
        UserLaunchIgnoredCompletion* next = node->next;

        if (now_msec < node->expires_msec) {
            node = next;
            continue;
        }

        if (node->prev != NULL) {
            node->prev->next = node->next;
        }
        else {
            g_user_launch_ignored_head = node->next;
        }
        if (node->next != NULL) {
            node->next->prev = node->prev;
        }
        else {
            g_user_launch_ignored_tail = node->prev;
        }

        free(node);
        node = next;
    }
}

/*
 * Invoke one registered async-launch callback and then release its registry node.
 *
 * @param request Request node that owns the callback metadata.
 * @param result Completion record returned by the kernel.
 * @return Nothing.
 */
static void user_launch_dispatch_result(UserLaunchPendingRequest* request, const UserAsyncSpawnResult* result) {
    UserLaunchProcessCallback callback;

    if ((request == NULL) || (result == NULL)) {
        user_launch_free_pending_request(request);
        return;
    }

    callback = ((result->status >= 0L) && (result->pid >= 0L)) ? request->success_callback : request->error_callback;
    if (callback != NULL) {
        callback(
            request->path,
            result->pid,
            result->status,
            request->name,
            request->args,
            request->context);
    }

    user_launch_free_pending_request(request);
}

/*
 * Synthesize one timeout failure result and dispatch the error callback.
 *
 * @param request Request node that outlived the callback timeout window.
 * @return Nothing.
 */
static void user_launch_dispatch_timeout(UserLaunchPendingRequest* request) {
    UserAsyncSpawnResult result;

    if (request == NULL) {
        return;
    }

    memset(&result, 0, sizeof(result));
    result.request_id = request->request_id;
    result.status = APPLICATIONS_USER_ASYNC_SPAWN_TIMEOUT_STATUS;
    result.pid = -1L;
    user_launch_dispatch_result(request, &result);
}

/*
 * Start the process-local async-launch completion dispatcher thread once.
 *
 * @return Zero on success, or a negative kernel status code on failure.
 */
static long user_launch_ensure_dispatcher_thread(void) {
    long status = 0L;

    user_launch_lock();
    if (!g_user_launch_dispatcher_started) {
        status = startUserThread((unsigned long)&user_launch_dispatcher_thread_entry, 0UL, "launch-cb");
        if (status >= 0L) {
            g_user_launch_dispatcher_started = 1;
            status = 0L;
        }
    }
    user_launch_unlock();
    return status;
}

/*
 * Drain completed async-launch results and invoke registered callbacks.
 *
 * @param argument Unused thread argument.
 * @return Nothing.
 */
static void user_launch_dispatcher_thread_entry(unsigned long argument) {
    (void)argument;
    // Keep completion delivery at normal priority because the current
    // cooperative scheduler can starve below-normal helper threads behind an
    // active shell or UI message loop, which makes async launches appear to
    // fail even after the kernel worker successfully created the child.
    (void)setCurrentThreadPriority(USER_THREAD_PRIORITY_NORMAL);

    for (;;) {
        UserAsyncSpawnResult result;
        UserLaunchPendingRequest* request = NULL;
        long status;

        memset(&result, 0, sizeof(result));
        status = pollAsyncSpawnResult(&result);
        if (status == 0L) {
            user_launch_lock();
            user_launch_prune_ignored_completions(getUptimeMs());
            if (user_launch_take_ignored_completion(result.request_id)) {
                user_launch_unlock();
                continue;
            }
            request = user_launch_take_pending_request(result.request_id);
            if (request == NULL) {
                user_launch_append_deferred_completion(&result);
            }
            user_launch_unlock();

            if (request != NULL) {
                user_launch_dispatch_result(request, &result);
            }
            continue;
        }

        user_launch_lock();
        user_launch_prune_ignored_completions(getUptimeMs());
        request = user_launch_take_timed_out_request(getUptimeMs());
        if (request != NULL) {
            user_launch_append_ignored_completion(request->request_id, getUptimeMs() + USER_LAUNCH_IGNORED_COMPLETION_GRACE_MSEC);
        }
        user_launch_unlock();

        if (request != NULL) {
            user_launch_dispatch_timeout(request);
            continue;
        }

        (void)sleepMs(10UL);
    }
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
    case SYS_SPAWN_ASYNC_RESULT:
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
    case SYS_SPAWN_ASYNC:
    case SYS_DIR_ENTRY:
    case SYS_CREATE_THREAD:
        arg0 = va_arg(args, unsigned long);
        arg1 = va_arg(args, unsigned long);
        arg2 = va_arg(args, unsigned long);
        va_end(args);
        return (long)invokeSyscall3((unsigned long)number, arg0, arg1, arg2);
    case SYS_READ_FILE:
    case SYS_EXT_INVOKE:
    case SYS_EVENT_READ:
    case SYS_TASK_MODULES:
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

/*
 * Queue one async process launch on the kernel worker and return the request id.
 *
 * @param path DOS-style executable path.
 * @param name Optional visible process name.
 * @param args Optional launch argument string.
 * @return Async request id on success, or a negative kernel status code.
 */
long call_sys_spawn_async(const char* path, const char* name, const char* args) {
    return syscall(SYS_SPAWN_ASYNC, (unsigned long)path, (unsigned long)name, (unsigned long)args);
}

/*
 * Poll one finished async launch result for the current process.
 *
 * @param result Receives the next available completion record.
 * @return Zero on success, `StatusBusy` when no result is ready, or another negative status.
 */
long call_sys_spawn_async_result(UserAsyncSpawnResult* result) {
    return syscall(SYS_SPAWN_ASYNC_RESULT, (unsigned long)result);
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

long call_sys_path_info(const char* path, UserPathInfo* info) {
    return syscall(SYS_PATH_INFO, (unsigned long)path, (unsigned long)info);
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

long call_sys_task_modules(long pid, UserTaskModuleInfo* modules, unsigned long capacity, unsigned long* count_out) {
    return syscall(SYS_TASK_MODULES, (unsigned long)pid, (unsigned long)modules, capacity, (unsigned long)count_out);
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

long call_sys_create_thread(unsigned long entry_point, unsigned long argument, const char* name) {
    return syscall(SYS_CREATE_THREAD, entry_point, argument, (unsigned long)name);
}

long call_sys_exit_thread(void) {
    return syscall(SYS_EXIT_THREAD);
}

/*
 * Queue one async process launch and route the completion to success or failure callbacks.
 *
 * The helper returns as soon as the kernel accepted the request. A dedicated
 * user thread polls the completion syscall and invokes the caller
 * callback once the kernel loader worker either produced a PID or failed. A
 * bounded timeout also synthesizes one failure callback so UI callers are not
 * left with a silent no-op when a completion is delayed or lost.
 *
 * @param path DOS-style executable path.
 * @param name Optional visible process name.
 * @param args Optional launch argument string.
 * @param success_callback Success callback invoked from the dispatcher thread.
 * @param error_callback Failure callback invoked from the dispatcher thread.
 * @param context Opaque caller-owned cookie forwarded to the callback.
 * @return Async request id on success, or a negative status code on failure.
 */
long SpawnTaskAsyncCallbacks(
    const char* path,
    const char* name,
    const char* args,
    SpawnTaskAsyncSuccessCallback success_callback,
    SpawnTaskAsyncErrorCallback error_callback,
    void* context) {
    UserLaunchPendingRequest* request = NULL;
    UserAsyncSpawnResult deferred_result;
    long request_id;
    int has_deferred_result = 0;

    if (path == NULL) {
        return -4L;
    }

    if ((success_callback == NULL) && (error_callback == NULL)) {
        return call_sys_spawn_async(path, name, args);
    }

    request = (UserLaunchPendingRequest*)malloc(sizeof(*request));
    if (request == NULL) {
        return -6L;
    }

    memset(request, 0, sizeof(*request));
    request->success_callback = success_callback;
    request->error_callback = error_callback;
    request->context = context;
    request->path = user_launch_duplicate_text(path);
    request->name = user_launch_duplicate_text(name);
    request->args = user_launch_duplicate_text(args);
    request->deadline_msec = getUptimeMs() + USER_LAUNCH_CALLBACK_TIMEOUT_MSEC;
    if (request->path == NULL) {
        user_launch_free_pending_request(request);
        return -6L;
    }

    request_id = call_sys_spawn_async(path, name, args);
    if (request_id < 0L) {
        user_launch_free_pending_request(request);
        return request_id;
    }

    request->request_id = (unsigned long)request_id;

    user_launch_lock();
    user_launch_append_pending_request(request);
    has_deferred_result = user_launch_take_deferred_completion(request->request_id, &deferred_result);
    if (has_deferred_result) {
        request = user_launch_take_pending_request(request->request_id);
    }
    user_launch_unlock();

    if (has_deferred_result && request != NULL) {
        user_launch_dispatch_result(request, &deferred_result);
        return request_id;
    }

    request_id = user_launch_ensure_dispatcher_thread() < 0L ? -6L : request_id;
    if (request_id < 0L) {
        user_launch_lock();
        request = user_launch_take_pending_request((unsigned long)request->request_id);
        user_launch_unlock();
        user_launch_free_pending_request(request);
        return request_id;
    }

    return request_id;
}

/*
 * Preserve the older one-callback async spawn helper on top of the split
 * success/error callback API.
 *
 * @param path DOS-style executable path.
 * @param name Optional visible process name.
 * @param args Optional launch argument string.
 * @param callback Completion callback invoked for both success and failure.
 * @param context Opaque caller-owned cookie forwarded to the callback.
 * @return Async request id on success, or a negative status code on failure.
 */
long SpawnTaskAsyncCallback(
    const char* path,
    const char* name,
    const char* args,
    SpawnTaskAsyncCompletionCallback callback,
    void* context) {
    return SpawnTaskAsyncCallbacks(path, name, args, callback, callback, context);
}

/*
 * Preserve the older generic async launch helper on top of the spawn-specific
 * callback API so existing callers keep working while new code can discover the
 * spawn-oriented entrypoint directly from the runtime surface.
 *
 * @param path DOS-style executable path.
 * @param name Optional visible process name.
 * @param args Optional launch argument string.
 * @param callback Completion callback invoked from the dispatcher thread.
 * @param context Opaque caller-owned cookie forwarded to the callback.
 * @return Async request id on success, or a negative status code on failure.
 */
long UserLaunchProcess(
    const char* path,
    const char* name,
    const char* args,
    UserLaunchProcessCallback callback,
    void* context) {
    return SpawnTaskAsyncCallbacks(path, name, args, callback, callback, context);
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
    return user_shared_heap_malloc(size);
}

void free(void* ptr) {
    user_shared_heap_free(ptr);
}

void* realloc(void* ptr, size_t size) {
    return user_shared_heap_realloc(ptr, size);
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