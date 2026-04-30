#include "app/app.h"

#include "stdio.h"

#define KEVENTMON_BATCH_CAPACITY 16U
#define KEVENTMON_IDLE_POLL_MILLISECONDS 100U
#define KEVENTMON_LIMIT_UNBOUNDED 0UL
#define KEVENTMON_DEFAULT_LIMIT KEVENTMON_LIMIT_UNBOUNDED

/*
 * keventmon_copy_text
 *
 * The tiny userspace formatter is stable when it walks stack-backed strings, so
 * the event decoder copies readable labels into caller-owned buffers before they
 * are passed through any %s formatting path.
 */
static void keventmon_copy_text(char* text, unsigned long size, const char* value) {
    unsigned long index = 0UL;

    if ((text == NULL) || (size == 0UL)) {
        return;
    }

    if (value == NULL) {
        text[0] = '\0';
        return;
    }

    while ((index + 1UL < size) && (value[index] != '\0')) {
        text[index] = value[index];
        index++;
    }
    text[index] = '\0';
}

/*
 * keventmon_parse_limit
 *
 * Background monitors are most useful when they stay attached until an
 * operator explicitly kills them, so the no-argument path now means
 * "unbounded" instead of "capture the next 32 events and disappear". A
 * positive numeric argument still provides bounded capture for scripted runs.
 *
 * @param text Optional shell argument string containing a numeric event limit.
 * @return Zero for unbounded monitoring, otherwise the requested event count.
 */
static unsigned long keventmon_parse_limit(const char* text) {
    unsigned long value = 0UL;

    if ((text == NULL) || (text[0] == '\0')) {
        return KEVENTMON_DEFAULT_LIMIT;
    }

    while (*text == ' ' || *text == '\t') {
        text++;
    }
    if ((*text < '0') || (*text > '9')) {
        return KEVENTMON_DEFAULT_LIMIT;
    }

    while ((*text >= '0') && (*text <= '9')) {
        value = (value * 10UL) + (unsigned long)(*text - '0');
        text++;
    }

    return value;
}

/*
 * keventmon_status_name
 *
 * Event records carry raw status codes, but readable names make failures much
 * easier to scan when the monitor is running in a busy boot log.
 */
static void keventmon_status_name(long status, char* text, unsigned long size) {
    if ((Status)status == StatusOK) {
        keventmon_copy_text(text, size, "ok");
        return;
    }
    if ((Status)status == StatusInvalidArgument) {
        keventmon_copy_text(text, size, "invalid-argument");
        return;
    }
    if ((Status)status == StatusNotFound) {
        keventmon_copy_text(text, size, "not-found");
        return;
    }
    if ((Status)status == StatusAlreadyExists) {
        keventmon_copy_text(text, size, "already-exists");
        return;
    }
    if ((Status)status == StatusNotSupported) {
        keventmon_copy_text(text, size, "not-supported");
        return;
    }
    if ((Status)status == StatusNoMemory) {
        keventmon_copy_text(text, size, "no-memory");
        return;
    }
    if ((Status)status == StatusNoSpace) {
        keventmon_copy_text(text, size, "no-space");
        return;
    }
    if ((Status)status == StatusIoError) {
        keventmon_copy_text(text, size, "io-error");
        return;
    }
    if ((Status)status == StatusBusy) {
        keventmon_copy_text(text, size, "busy");
        return;
    }
    if ((Status)status == StatusFault) {
        keventmon_copy_text(text, size, "fault");
        return;
    }

    keventmon_copy_text(text, size, "unknown");
}

/*
 * keventmon_event_type_name
 *
 * Type ids are stable ABI values, but the monitor is meant for humans. Decode
 * them once here so every log line names the subsystem action directly.
 */
static void keventmon_event_type_name(U32 type, char* text, unsigned long size) {
    if ((KernelEventType)type == KernelEventTypeProcessCreated) {
        keventmon_copy_text(text, size, "process.created");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeProcessExited) {
        keventmon_copy_text(text, size, "process.exited");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeThreadCreated) {
        keventmon_copy_text(text, size, "thread.created");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeThreadExited) {
        keventmon_copy_text(text, size, "thread.exited");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeThreadReady) {
        keventmon_copy_text(text, size, "thread.ready");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeThreadRunning) {
        keventmon_copy_text(text, size, "thread.running");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeFilesystemCreated) {
        keventmon_copy_text(text, size, "filesystem.created");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeFilesystemRemoved) {
        keventmon_copy_text(text, size, "filesystem.removed");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeDeviceRegistered) {
        keventmon_copy_text(text, size, "device.registered");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeDriverRegistered) {
        keventmon_copy_text(text, size, "driver.registered");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeFilesystemDriverRegistered) {
        keventmon_copy_text(text, size, "filesystem-driver.registered");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeModuleLoaded) {
        keventmon_copy_text(text, size, "module.loaded");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeModuleUnloaded) {
        keventmon_copy_text(text, size, "module.unloaded");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeModuleInvoked) {
        keventmon_copy_text(text, size, "module.invoked");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeSharedLibraryOpened) {
        keventmon_copy_text(text, size, "shlib.opened");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeSharedLibraryClosed) {
        keventmon_copy_text(text, size, "shlib.closed");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeSharedLibraryExported) {
        keventmon_copy_text(text, size, "shlib.exported");
        return;
    }
    if ((KernelEventType)type == KernelEventTypeDriverUnloaded) {
        keventmon_copy_text(text, size, "driver.unloaded");
        return;
    }

    keventmon_copy_text(text, size, "event");
}

/*
 * keventmon_process_state_name
 *
 * Process states are exported as ABI integers rather than the kernel's C++ enum
 * names, so mirror the known slots locally for readable logs.
 */
static void keventmon_process_state_name(U32 state, char* text, unsigned long size) {
    if (state == 0U) {
        keventmon_copy_text(text, size, "created");
        return;
    }
    if (state == 1U) {
        keventmon_copy_text(text, size, "running");
        return;
    }
    if (state == 2U) {
        keventmon_copy_text(text, size, "exiting");
        return;
    }
    if (state == 3U) {
        keventmon_copy_text(text, size, "terminated");
        return;
    }

    keventmon_copy_text(text, size, "unknown");
}

/*
 * keventmon_thread_state_name
 *
 * Thread events become much easier to interpret when the scheduler transition is
 * printed with names instead of raw numeric slots.
 */
static void keventmon_thread_state_name(U32 state, char* text, unsigned long size) {
    if (state == 0U) {
        keventmon_copy_text(text, size, "initialized");
        return;
    }
    if (state == 1U) {
        keventmon_copy_text(text, size, "ready");
        return;
    }
    if (state == 2U) {
        keventmon_copy_text(text, size, "running");
        return;
    }
    if (state == 3U) {
        keventmon_copy_text(text, size, "waiting");
        return;
    }
    if (state == 4U) {
        keventmon_copy_text(text, size, "suspended");
        return;
    }
    if (state == 5U) {
        keventmon_copy_text(text, size, "terminated");
        return;
    }

    keventmon_copy_text(text, size, "unknown");
}

/*
 * keventmon_node_type_name
 *
 * Filesystem events only expose the compact VFS node-type id, so decode that
 * payload into terms users expect to read in the shell.
 */
static void keventmon_node_type_name(U32 node_type, char* text, unsigned long size) {
    if (node_type == 1U) {
        keventmon_copy_text(text, size, "dir");
        return;
    }
    if (node_type == 2U) {
        keventmon_copy_text(text, size, "file");
        return;
    }
    if (node_type == 3U) {
        keventmon_copy_text(text, size, "device");
        return;
    }

    keventmon_copy_text(text, size, "unknown");
}

/*
 * keventmon_registration_kind_name
 *
 * Registration events share one payload shape, so print the specific kind to
 * distinguish device announcements from driver announcements.
 */
static void keventmon_registration_kind_name(U32 kind, char* text, unsigned long size) {
    if ((KernelEventRegistrationKind)kind == KernelEventRegistrationKindDevice) {
        keventmon_copy_text(text, size, "device");
        return;
    }
    if ((KernelEventRegistrationKind)kind == KernelEventRegistrationKindDriver) {
        keventmon_copy_text(text, size, "driver");
        return;
    }
    if ((KernelEventRegistrationKind)kind == KernelEventRegistrationKindFilesystemDriver) {
        keventmon_copy_text(text, size, "filesystem-driver");
        return;
    }

    keventmon_copy_text(text, size, "none");
}

/*
 * keventmon_format_target
 *
 * Most events optionally carry a name, a path, or both. Formatting that common
 * identity once keeps the per-event formatter compact and consistent.
 */
static void keventmon_format_target(const KernelEventRecord* record, char* text, unsigned long size) {
    if ((text == NULL) || (size == 0UL)) {
        return;
    }

    text[0] = '\0';
    if ((record == NULL) || ((record->name[0] == '\0') && (record->path[0] == '\0'))) {
        return;
    }

    if ((record->name[0] != '\0') && (record->path[0] != '\0')) {
        (void)snprintf(text, size, " name=%s path=%s", record->name, record->path);
        return;
    }
    if (record->name[0] != '\0') {
        (void)snprintf(text, size, " name=%s", record->name);
        return;
    }

    (void)snprintf(text, size, " path=%s", record->path);
}

/*
 * keventmon_format_detail
 *
 * Decode each event family into a readable suffix so the monitor explains what
 * changed instead of echoing ABI field numbers back to the user.
 */
static void __attribute__((noinline)) keventmon_format_detail(const KernelEventRecord* record, char* text, unsigned long size) {
    char target[240];
    char process_state[32];
    char previous_state[32];
    char current_state[32];
    char node_type[32];
    char registration_kind[32];

    if ((text == NULL) || (size == 0UL)) {
        return;
    }

    text[0] = '\0';
    if (record == NULL) {
        return;
    }

    keventmon_format_target(record, target, sizeof(target));
    if (((KernelEventType)record->type == KernelEventTypeProcessCreated)
        || ((KernelEventType)record->type == KernelEventTypeProcessExited)) {
        keventmon_process_state_name(record->payload.process.process_state, process_state, sizeof(process_state));
        (void)snprintf(
            text,
            size,
            "pid=%llu parent=%llu state=%s exit=%lld flags=0x%lx%s",
            (unsigned long long)record->source_process_id,
            (unsigned long long)record->payload.process.parent_process_id,
            process_state,
            (long long)record->payload.process.exit_code,
            (unsigned long)record->payload.process.process_flags,
            target);
        return;
    }

    if (((KernelEventType)record->type == KernelEventTypeThreadCreated)
        || ((KernelEventType)record->type == KernelEventTypeThreadExited)
        || ((KernelEventType)record->type == KernelEventTypeThreadReady)
        || ((KernelEventType)record->type == KernelEventTypeThreadRunning)) {
        keventmon_thread_state_name(record->payload.thread.previous_state, previous_state, sizeof(previous_state));
        keventmon_thread_state_name(record->payload.thread.current_state, current_state, sizeof(current_state));
        (void)snprintf(
            text,
            size,
            "pid=%llu tid=%llu %s->%s pri=%lu%s",
            (unsigned long long)record->source_process_id,
            (unsigned long long)record->source_thread_id,
            previous_state,
            current_state,
            (unsigned long)record->payload.thread.priority,
            target);
        return;
    }

    if (((KernelEventType)record->type == KernelEventTypeFilesystemCreated)
        || ((KernelEventType)record->type == KernelEventTypeFilesystemRemoved)) {
        keventmon_node_type_name(record->payload.filesystem.node_type, node_type, sizeof(node_type));
        (void)snprintf(
            text,
            size,
            "pid=%llu type=%s%s",
            (unsigned long long)record->source_process_id,
            node_type,
            target);
        return;
    }

    if (((KernelEventType)record->type == KernelEventTypeDeviceRegistered)
        || ((KernelEventType)record->type == KernelEventTypeDriverRegistered)
        || ((KernelEventType)record->type == KernelEventTypeFilesystemDriverRegistered)) {
        keventmon_registration_kind_name(
            record->payload.registration.registration_kind,
            registration_kind,
            sizeof(registration_kind));
        (void)snprintf(
            text,
            size,
            "kind=%s pid=%llu%s",
            registration_kind,
            (unsigned long long)record->source_process_id,
            target);
        return;
    }

    if (((KernelEventType)record->type == KernelEventTypeModuleLoaded)
        || ((KernelEventType)record->type == KernelEventTypeModuleUnloaded)
        || ((KernelEventType)record->type == KernelEventTypeModuleInvoked)
        || ((KernelEventType)record->type == KernelEventTypeSharedLibraryOpened)
        || ((KernelEventType)record->type == KernelEventTypeSharedLibraryClosed)
        || ((KernelEventType)record->type == KernelEventTypeSharedLibraryExported)
        || ((KernelEventType)record->type == KernelEventTypeDriverUnloaded)) {
        (void)snprintf(
            text,
            size,
            "pid=%llu tid=%llu result=%lld value0=0x%llx value1=0x%llx%s",
            (unsigned long long)record->source_process_id,
            (unsigned long long)record->source_thread_id,
            (long long)record->payload.module.result_code,
            (unsigned long long)record->payload.module.value0,
            (unsigned long long)record->payload.module.value1,
            target);
        return;
    }

    (void)snprintf(
        text,
        size,
        "pid=%llu tid=%llu%s",
        (unsigned long long)record->source_process_id,
        (unsigned long long)record->source_thread_id,
        target);
}

/*
 * keventmon_print_record
 *
 * One concise, decoded line per event is much easier to follow than the raw
 * ABI dump that the first version emitted.
 */
static long __attribute__((noinline)) keventmon_print_record(const KernelEventRecord* record) {
    char line[512];
    char detail[320];
    char event_type[64];
    char status_name[32];
    char status_text[64];

    if (record == NULL) {
        return writeLine("keventmon: invalid record");
    }

    keventmon_format_detail(record, detail, sizeof(detail));
    keventmon_event_type_name(record->type, event_type, sizeof(event_type));
    if (record->status == StatusOK) {
        status_text[0] = '\0';
    }
    else {
        keventmon_status_name((long)record->status, status_name, sizeof(status_name));
        (void)snprintf(
            status_text,
            sizeof(status_text),
            " status=%s(%ld)",
            status_name,
            (long)record->status);
    }

    (void)snprintf(
        line,
        sizeof(line),
        "[kevent #%llu @%llums] %s%s%s%s",
        (unsigned long long)record->sequence,
        (unsigned long long)record->uptime_msec,
        event_type,
        (detail[0] != '\0') ? " " : "",
        detail,
        status_text);
    return writeLine(line);
}

/*
 * main
 *
 * Subscribe to the kernel event broker, drain records in batches, and print a
 * readable event stream for shell-driven debugging sessions. The default mode
 * stays alive until the task is killed so `start kevent` behaves like a real
 * background monitor instead of a short one-shot sampler.
 *
 * @return Process exit status for the loader runtime.
 */
int main(void) {
    KernelEventSubscriptionRequest request;
    KernelEventSubscriptionInfo info;
    KernelEventSubscriptionId subscription_id = 0ULL;
    KernelEventRecord records[KEVENTMON_BATCH_CAPACITY];
    char args[64] = { 0 };
    char line[160];
    char status_name[32];
    unsigned long delivered = 0UL;
    unsigned long limit;
    long status;

    if (getTaskArgs(args, sizeof(args)) < 0) {
        args[0] = '\0';
    }

    limit = keventmon_parse_limit(args);
    memzero(&request, sizeof(request));
    request.version = KERNEL_EVENT_ABI_VERSION;
    request.family_mask = KERNEL_EVENT_FAMILY_PROCESS | KERNEL_EVENT_FAMILY_FILESYSTEM | KERNEL_EVENT_FAMILY_MODULE | KERNEL_EVENT_FAMILY_REGISTRATION;

    status = subscribeKernelEvents(&request, &subscription_id);
    if (status < 0) {
        keventmon_status_name(status, status_name, sizeof(status_name));
        (void)snprintf(
            line,
            sizeof(line),
            "keventmon: subscribe failed status=%s(%ld)",
            status_name,
            status);
        (void)writeLine(line);
        return 1;
    }

    if (limit == KEVENTMON_LIMIT_UNBOUNDED) {
        (void)snprintf(
            line,
            sizeof(line),
            "keventmon: listening until killed (subscription=%llu)",
            (unsigned long long)subscription_id);
    }
    else {
        (void)snprintf(
            line,
            sizeof(line),
            "keventmon: listening for up to %lu event(s) (subscription=%llu)",
            limit,
            (unsigned long long)subscription_id);
    }
    (void)writeLine(line);

    status = queryKernelEvents(subscription_id, &info);
    if (status == 0) {
        (void)snprintf(
            line,
            sizeof(line),
            "keventmon: queue=%u/%u dropped=%llu next=%llu newest=%llu",
            info.queued_count,
            info.capacity,
            (unsigned long long)info.dropped_count,
            (unsigned long long)info.next_sequence,
            (unsigned long long)info.newest_sequence);
        (void)writeLine(line);
    }

    while ((limit == KEVENTMON_LIMIT_UNBOUNDED) || (delivered < limit)) {
        U32 count = 0U;

        status = readKernelEvents(subscription_id, records, KEVENTMON_BATCH_CAPACITY, &count);
        if (status < 0) {
            keventmon_status_name(status, status_name, sizeof(status_name));
            (void)snprintf(
                line,
                sizeof(line),
                "keventmon: read failed status=%s(%ld)",
                status_name,
                status);
            (void)writeLine(line);
            break;
        }
        if (count == 0U) {
            status = waitKernelEvents(subscription_id);
            if (status == StatusBusy) {
                /*
                 * The broker's blocking wait path is still permissive in some
                 * scheduler states and may report Busy instead of parking the
                 * caller. Falling back to a short sleep keeps `start kevent`
                 * alive and still lets the monitor drain events as they show
                 * up, which is better than aborting the background task.
                 */
                sleepMs(KEVENTMON_IDLE_POLL_MILLISECONDS);
                continue;
            }
            if (status < 0) {
                keventmon_status_name(status, status_name, sizeof(status_name));
                (void)snprintf(
                    line,
                    sizeof(line),
                    "keventmon: wait failed status=%s(%ld)",
                    status_name,
                    status);
                (void)writeLine(line);
                break;
            }
            continue;
        }

        for (U32 index = 0U;
            (index < count) && ((limit == KEVENTMON_LIMIT_UNBOUNDED) || (delivered < limit));
            ++index) {
            if (keventmon_print_record(&records[index]) < 0) {
                break;
            }
            delivered++;
        }
    }

    status = queryKernelEvents(subscription_id, &info);
    (void)unsubscribeKernelEvents(subscription_id);
    if (status == 0) {
        (void)snprintf(
            line,
            sizeof(line),
            "keventmon: delivered %lu event(s), queued=%u dropped=%llu newest=%llu",
            delivered,
            info.queued_count,
            (unsigned long long)info.dropped_count,
            (unsigned long long)info.newest_sequence);
    }
    else {
        (void)snprintf(line, sizeof(line), "keventmon: delivered %lu event(s)", delivered);
    }
    (void)writeLine(line);
    return 0;
}
