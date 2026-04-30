#define ROS_APP_USE_WINDOW 1
#include "app/app.h"

#define CORE_SUPERVISOR_POLL_MSEC 100UL
#define CORE_SPIN_BACKOFF_COUNT 50000UL
#define CORE_ENABLE_BOOT_DLLSMOKE 0
#define CORE_DLLSMOKE_PATH "/bin/dllsmoke.exe"
#define CORE_EXPLORER_PATH "/bin/explorer.exe"
#define CORE_GWES_PATH "/bin/gwes.exe"
#define CORE_SHELL_PATH "/bin/shell.exe"
#define CORE_STATUS_NOT_FOUND (-2L)
#define CORE_STATUS_NOT_SUPPORTED (-4L)
#define CORE_EVENT_BATCH_CAPACITY 8U
#define CORE_GWES_STARTUP_GRACE_MSEC 500UL
#define CORE_EXPLORER_RETRY_MSEC 5000UL
#define CORE_EXPLORER_WATCHDOG_MSEC 5000UL
#define CORE_WINDOW_SERVER_SHARED_STATE_NAME "ros.window-server.state"
#define CORE_WINDOW_SERVER_SHARED_STATE_VERSION 1UL

typedef struct CoreWindowServerSharedState {
    unsigned long version;
    long pid;
    char name[32];
} CoreWindowServerSharedState;

static CoreWindowServerSharedState* g_core_window_server_state;

static CoreWindowServerSharedState* core_window_server_shared_state(void);

static char* core_append_long(char* destination, long value) {
    if (value < 0) {
        *destination++ = '-';
        return appendUnsignedLong(destination, (unsigned long)(-value));
    }

    return appendUnsignedLong(destination, (unsigned long)value);
}

static long core_write_line(const char* text) {
    return writeLine(text);
}

static long core_write_key_value(const char* label, const char* value) {
    char line[192];
    char* cursor = line;

    cursor = appendText(cursor, label);
    cursor = appendText(cursor, value && value[0] != '\0' ? value : "<none>");
    *cursor = '\0';
    return core_write_line(line);
}

static long core_write_pid_event(const char* prefix, long pid) {
    char line[192];
    char* cursor = line;

    cursor = appendText(cursor, prefix);
    cursor = core_append_long(cursor, pid);
    *cursor = '\0';
    return core_write_line(line);
}

/*
 * Report whether one process-exit event represents a fatal user fault.
 *
 * The kernel terminates user tasks that hit an unrecoverable lower-EL abort
 * with `StatusFault`. Core uses that stable exit code to distinguish crashes
 * from ordinary process shutdown while keeping the event ABI small.
 *
 * @param record Kernel event record being inspected.
 * @return Non-zero when the process exit was fault-driven.
 */
static int core_process_exit_is_fault(const KernelEventRecord* record) {
    if (record == 0) {
        return 0;
    }

    return ((KernelEventType)record->type == KernelEventTypeProcessExited)
        && (record->payload.process.exit_code == (I64)StatusFault);
}

/*
 * Show one userspace-visible crash report for a faulted process.
 *
 * Keeping the dialog in core avoids trapping the whole kernel in the serial
 * debugger for normal app faults while still giving the desktop session a
 * human-readable error surface when GWES is alive.
 *
 * @param record Faulted process exit event.
 * @return Nothing.
 */
static void core_report_process_fault(const KernelEventRecord* record) {
    CoreWindowServerSharedState* window_server_state;
    char message[320];
    char log_line[192];
    char* cursor;

    if (!core_process_exit_is_fault(record)) {
        return;
    }

    cursor = log_line;
    cursor = appendText(cursor, "core.exe: detected faulted process pid=");
    cursor = appendUnsignedLong(cursor, (unsigned long)record->source_process_id);
    cursor = appendText(cursor, " name=");
    cursor = appendText(cursor, record->name[0] != '\0' ? record->name : "<unknown>");
    *cursor = '\0';
    (void)core_write_line(log_line);

    window_server_state = core_window_server_shared_state();
    if ((window_server_state == 0) || (window_server_state->pid < 0L)) {
        return;
    }
    if ((long)record->source_process_id == window_server_state->pid) {
        (void)core_write_line("core.exe: skipped fault dialog because gwes faulted");
        return;
    }

    cursor = message;
    cursor = appendText(cursor, record->name[0] != '\0' ? record->name : "<unknown>");
    cursor = appendText(cursor, " (pid=");
    cursor = appendUnsignedLong(cursor, (unsigned long)record->source_process_id);
    cursor = appendText(cursor, ") hit a fatal user-mode fault and was terminated.\n");
    cursor = appendText(cursor, "The kernel stayed alive. Check the serial log for ESR/FAR/ELR details.");
    *cursor = '\0';

    if (MessageBoxError("Application Fault", message) < 0L) {
        (void)core_write_line("core.exe: failed to show fault dialog");
    }
}

static unsigned long core_uptime_msec(void) {
    return getUptimeMs();
}

/*
 * core_timer_expired
 *
 * Explorer restart supervision uses one coarse periodic timer so core wakes up
 * even when the kernel event broker has nothing queued. That prevents a child
 * shell regression from leaving the session without a desktop behind one
 * indefinite `waitKernelEvents()` call.
 *
 * @param deadline_msec In-out absolute deadline.
 * @param period_msec Period to arm after each expiration.
 * @return Non-zero when the timer expired on this call.
 */
static int core_timer_expired(unsigned long* deadline_msec, unsigned long period_msec) {
    unsigned long now;

    if ((deadline_msec == 0) || (period_msec == 0UL)) {
        return 0;
    }

    now = core_uptime_msec();
    if ((*deadline_msec != 0UL) && (now < *deadline_msec)) {
        return 0;
    }

    *deadline_msec = now + period_msec;
    return 1;
}

/*
 * core_zero_memory
 *
 * Core only needs a tiny deterministic clear helper for fixed-size launcher
 * structs, so keeping it local avoids depending on any hosted libc routine.
 *
 * @param destination Buffer to clear.
 * @param size Number of bytes to clear.
 * @return Nothing.
 */
static void core_zero_memory(void* destination, unsigned long size) {
    unsigned char* bytes = (unsigned char*)destination;
    unsigned long index;

    if (bytes == 0) {
        return;
    }

    for (index = 0UL; index < size; ++index) {
        bytes[index] = 0U;
    }
}

/*
 * core_window_server_shared_state
 *
 * The shell and client DLLs now depend on one explicit GWES PID publication
 * path rather than best-effort task enumeration. Core owns that publication
 * because it already receives the authoritative PID from `spawnTask`.
 *
 * @return Shared-state mapping, or NULL when creation/open failed.
 */
static void core_copy_text(char* destination, unsigned long capacity, const char* source) {
    unsigned long index = 0UL;

    if (!destination || capacity == 0UL) {
        return;
    }

    if (!source) {
        destination[0] = '\0';
        return;
    }

    while (source[index] != '\0' && (index + 1UL) < capacity) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

/*
 * core_window_server_shared_state
 *
 * The shell and client DLLs now depend on one explicit GWES PID publication
 * path rather than best-effort task enumeration. Core owns that publication
 * because it already receives the authoritative PID from `spawnTask`.
 *
 * @return Shared-state mapping, or NULL when creation/open failed.
 */
static CoreWindowServerSharedState* core_window_server_shared_state(void) {
    void* address = 0;

    if (g_core_window_server_state != 0) {
        return g_core_window_server_state;
    }

    if (acquireSharedMemoryRegion(CORE_WINDOW_SERVER_SHARED_STATE_NAME, sizeof(CoreWindowServerSharedState), &address) < 0) {
        return 0;
    }

    g_core_window_server_state = (CoreWindowServerSharedState*)address;
    if (g_core_window_server_state == 0) {
        return 0;
    }

    if (g_core_window_server_state->version != CORE_WINDOW_SERVER_SHARED_STATE_VERSION) {
        g_core_window_server_state->version = CORE_WINDOW_SERVER_SHARED_STATE_VERSION;
        g_core_window_server_state->pid = CORE_STATUS_NOT_FOUND;
        core_copy_text(g_core_window_server_state->name, sizeof(g_core_window_server_state->name), "gwes");
    }

    return g_core_window_server_state;
}

/*
 * core_publish_gwes_pid
 *
 * Publishing the spawned PID into shared memory gives every GUI DLL one stable
 * contract even when task-table lookups lag behind process creation.
 *
 * @param pid PID to publish, or a negative status when GWES is unavailable.
 * @return Nothing.
 */
static void core_publish_gwes_pid(long pid) {
    CoreWindowServerSharedState* state = core_window_server_shared_state();

    if (state == 0) {
        return;
    }

    state->pid = pid;
}

#if CORE_ENABLE_BOOT_DLLSMOKE
/*
 * core_launch_boot_smoke
 *
 * Launch the one-shot DLL smoke consumer before the shell supervisor loop
 * begins so every clean boot exercises the new user DLL load path.
 *
 * @return Spawned PID on success, or the negative kernel status code.
 */
static long core_launch_boot_smoke(void) {
    long pid = spawnTask(CORE_DLLSMOKE_PATH, "dllsmoke", "");

    if (pid >= 0) {
        (void)core_write_pid_event("core.exe: launched dll smoke pid=", pid);
        return pid;
    }

    (void)core_write_pid_event("core.exe: dll smoke launch failed status=", pid);
    return pid;
}
#endif

/*
 * core_launch_gwes
 *
 * Launch the early userspace window server before the shell starts so GUI test
 * clients can immediately discover their broker peer.
 *
 * @return Spawned PID on success, or the negative kernel status code.
 */
static long core_launch_gwes(void) {
    long pid = spawnTask(CORE_GWES_PATH, "gwes", "");

    if (pid >= 0) {
        core_publish_gwes_pid(pid);
        (void)core_write_pid_event("core.exe: launched gwes pid=", pid);
        return pid;
    }

    core_publish_gwes_pid(CORE_STATUS_NOT_FOUND);
    (void)core_write_pid_event("core.exe: gwes launch failed status=", pid);
    return pid;
}

/*
 * core_maybe_launch_gwes
 *
 * On `virt`, launching the shell first is the safer scheduler handoff order.
 * This helper defers GWES startup until at least one shell is alive and keeps
 * retrying if the first window-server launch fails.
 *
 * @param gwes_started Sticky flag owned by the supervisor loop.
 * @return Nothing.
 */
static void core_maybe_launch_gwes(int* gwes_started) {
    if ((gwes_started == 0) || *gwes_started) {
        return;
    }

    if (core_launch_gwes() >= 0) {
        *gwes_started = 1;
    }
}

static int core_task_is_alive(long pid) {
    UserTaskInfo info;
    long status;

    if (pid < 0) {
        return 0;
    }

    status = getTaskInfo(pid, &info);
    if (status < 0) {
        return 0;
    }

    return info.main_thread_state != USER_TASK_STATE_TERMINATED;
}

/*
 * core_task_is_gone
 *
 * The userspace supervisor sometimes needs the inverse predicate when it is
 * reconciling polling fallback state or deciding whether a dependent process
 * should be relaunched after a crash.
 *
 * @param pid PID to inspect.
 * @return Non-zero when the process no longer appears alive.
 */
static int core_task_is_gone(long pid) {
    return !core_task_is_alive(pid);
}

static long core_launch_shell(long previous_pid, long* last_launch_status) {
    long pid;

    if (previous_pid >= 0) {
        (void)core_write_pid_event("core.exe: restarting shell after pid=", previous_pid);
    }

    pid = spawnTask(CORE_SHELL_PATH, "shell", "");
    if (pid >= 0) {
        if (last_launch_status) {
            *last_launch_status = 0;
        }
        (void)core_write_pid_event("core.exe: launched shell pid=", pid);
        return pid;
    }

    if ((last_launch_status == NULL) || (*last_launch_status != pid)) {
        (void)core_write_pid_event("core.exe: shell launch failed status=", pid);
        if (last_launch_status) {
            *last_launch_status = pid;
        }
    }

    return -1;
}

/*
 * core_launch_explorer
 *
 * Explorer is supervised directly by core so the shell UI returns even when
 * the taskbar process exits unexpectedly. Keeping the launch helper separate
 * makes the restart log lines explicit and symmetric with shell supervision.
 *
 * @param previous_pid Previously supervised explorer PID, or a negative value
 * when this is the first launch attempt.
 * @param last_launch_status Sticky launch-failure code used to avoid logging
 * the same failure endlessly.
 * @return Spawned PID on success, or -1 when launch failed.
 */
static long core_launch_explorer(long previous_pid, long* last_launch_status, unsigned long* retry_after_msec, int* launch_suppressed) {
    long pid;
    unsigned long now = core_uptime_msec();

    if (previous_pid >= 0) {
        (void)core_write_pid_event("core.exe: restarting explorer after pid=", previous_pid);
    }

    pid = spawnTask(CORE_EXPLORER_PATH, "explorer", "");
    if (pid >= 0) {
        if (last_launch_status) {
            *last_launch_status = 0;
        }
        if (launch_suppressed != NULL) {
            *launch_suppressed = 0;
        }
        if (retry_after_msec) {
            *retry_after_msec = 0UL;
        }
        (void)core_write_pid_event("core.exe: launched explorer pid=", pid);
        return pid;
    }

    if ((last_launch_status == NULL) || (*last_launch_status != pid)) {
        (void)core_write_pid_event("core.exe: explorer launch failed status=", pid);
        if (last_launch_status) {
            *last_launch_status = pid;
        }
    }
    if (pid == StatusNotFound && launch_suppressed != NULL) {
        *launch_suppressed = 1;
        (void)core_write_line("core.exe: suppressing explorer relaunch after deterministic load failure");
    }
    if (retry_after_msec) {
        *retry_after_msec = now + CORE_EXPLORER_RETRY_MSEC;
    }

    return -1;
}

/*
 * core_reap_process_exit
 *
 * The event broker tells core that a process exited, but `waitPid` still needs
 * to run so the exit record is consumed and the status is visible in logs.
 *
 * @param pid Exited PID to reap.
 * @param name Short process label used in diagnostics.
 * @return Nothing.
 */
static void core_reap_process_exit(long pid, const char* name) {
    long exit_code = 0;
    long status;
    char line[192];
    char* cursor = line;

    if (pid < 0) {
        return;
    }

    status = waitPid(pid, &exit_code);
    cursor = appendText(cursor, "core.exe: ");
    cursor = appendText(cursor, name ? name : "process");
    if (status == 0) {
        cursor = appendText(cursor, " exit pid=");
        cursor = core_append_long(cursor, pid);
        cursor = appendText(cursor, " code=");
        cursor = core_append_long(cursor, exit_code);
    }
    else {
        cursor = appendText(cursor, " reap failed pid=");
        cursor = core_append_long(cursor, pid);
        cursor = appendText(cursor, " status=");
        cursor = core_append_long(cursor, status);
    }
    *cursor = '\0';
    (void)core_write_line(line);
}

/*
 * core_force_explorer_shutdown
 *
 * Explorer depends on a live GWES instance for its window connection. When the
 * window server dies, proactively terminating explorer keeps the shell stack in
 * a known order and lets core relaunch explorer after GWES returns.
 *
 * @param explorer_pid Pointer to the tracked explorer PID.
 * @return Nothing.
 */
static void core_force_explorer_shutdown(long* explorer_pid) {
    if (explorer_pid == NULL || *explorer_pid < 0) {
        return;
    }

    if (!core_task_is_gone(*explorer_pid)) {
        (void)core_write_pid_event("core.exe: stopping explorer because gwes exited pid=", *explorer_pid);
        (void)killTask(*explorer_pid);
    }
}

/*
 * core_handle_shell_exit
 *
 * Shell exits are independent from GWES and explorer, so core only needs to
 * reap the old process and clear the tracked PID for relaunch.
 *
 * @param shell_pid Pointer to the tracked shell PID.
 * @return Nothing.
 */
static void core_handle_shell_exit(long* shell_pid) {
    long exited_pid;

    if (shell_pid == NULL || *shell_pid < 0) {
        return;
    }

    exited_pid = *shell_pid;
    *shell_pid = -1;
    core_reap_process_exit(exited_pid, "shell");
}

/*
 * core_handle_gwes_exit
 *
 * GWES owns the GUI broker contract, so losing it invalidates the published PID
 * and any explorer taskbar window attached to the old server.
 *
 * @param gwes_pid Pointer to the tracked GWES PID.
 * @param gwes_started Sticky launch flag owned by the supervisor loop.
 * @param explorer_pid Pointer to the tracked explorer PID.
 * @return Nothing.
 */
static void core_handle_gwes_exit(long* gwes_pid, int* gwes_started, long* explorer_pid, unsigned long* explorer_retry_after_msec) {
    long exited_pid;

    if (gwes_pid == NULL || *gwes_pid < 0) {
        return;
    }

    exited_pid = *gwes_pid;
    *gwes_pid = -1;
    if (gwes_started) {
        *gwes_started = 0;
    }
    if (explorer_retry_after_msec != NULL) {
        *explorer_retry_after_msec = 0UL;
    }
    core_publish_gwes_pid(CORE_STATUS_NOT_FOUND);
    core_force_explorer_shutdown(explorer_pid);
    core_reap_process_exit(exited_pid, "gwes");
}

/*
 * core_handle_explorer_exit
 *
 * Explorer is supervised directly by core, so its exit only needs to clear the
 * tracked PID and consume the cached exit record.
 *
 * @param explorer_pid Pointer to the tracked explorer PID.
 * @return Nothing.
 */
static void core_handle_explorer_exit(
    long* explorer_pid,
    unsigned long* explorer_retry_after_msec,
    unsigned long next_retry_after_msec) {
    long exited_pid;

    if (explorer_pid == NULL || *explorer_pid < 0) {
        return;
    }

    exited_pid = *explorer_pid;
    *explorer_pid = -1;
    if (explorer_retry_after_msec != NULL) {
        *explorer_retry_after_msec = next_retry_after_msec;
    }
    core_reap_process_exit(exited_pid, "explorer");
}

/*
 * core_run_explorer_watchdog
 *
 * The normal event path should catch explorer exits first, but the desktop must
 * still come back when an exit event is missed or delayed. This watchdog runs
 * every five seconds and converts a dead tracked explorer PID into an immediate
 * relaunch opportunity.
 *
 * @param shell_pid Current shell PID.
 * @param gwes_pid Current GWES PID.
 * @param explorer_pid Pointer to the tracked explorer PID.
 * @param explorer_retry_after_msec Pointer to the next allowed launch time.
 * @return Nothing.
 */
static void core_run_explorer_watchdog(
    long shell_pid,
    long gwes_pid,
    long* explorer_pid,
    unsigned long* explorer_retry_after_msec) {
    unsigned long now;

    if ((explorer_pid == NULL) || (shell_pid < 0) || (gwes_pid < 0)) {
        return;
    }
    if (*explorer_pid < 0) {
        return;
    }
    if (core_task_is_alive(*explorer_pid)) {
        return;
    }

    now = core_uptime_msec();
    (void)core_write_pid_event("core.exe: explorer watchdog detected missing pid=", *explorer_pid);
    core_handle_explorer_exit(explorer_pid, explorer_retry_after_msec, now);
}

/*
 * core_handle_process_exit
 *
 * The process-event broker reports exits for the whole system, so the core
 * supervisor filters those events down to the PIDs it currently owns.
 *
 * @param pid Exited PID reported by the event record.
 * @param shell_pid Pointer to tracked shell PID.
 * @param gwes_pid Pointer to tracked GWES PID.
 * @param gwes_started Pointer to the GWES sticky launch flag.
 * @param explorer_pid Pointer to tracked explorer PID.
 * @return Nothing.
 */
static void core_handle_process_exit(
    long pid,
    long* shell_pid,
    long* gwes_pid,
    int* gwes_started,
    long* explorer_pid,
    unsigned long* explorer_retry_after_msec) {
    if (shell_pid != NULL && pid == *shell_pid) {
        core_handle_shell_exit(shell_pid);
        return;
    }
    if (gwes_pid != NULL && pid == *gwes_pid) {
        core_handle_gwes_exit(gwes_pid, gwes_started, explorer_pid, explorer_retry_after_msec);
        return;
    }
    if (explorer_pid != NULL && pid == *explorer_pid) {
        core_handle_explorer_exit(
            explorer_pid,
            explorer_retry_after_msec,
            core_uptime_msec() + CORE_EXPLORER_RETRY_MSEC);
    }
}

/*
 * core_poll_supervised_processes
 *
 * Event subscriptions are the preferred supervision path, but a polling pass
 * keeps core resilient if the event broker is unavailable or temporarily busy.
 *
 * @param shell_pid Pointer to tracked shell PID.
 * @param gwes_pid Pointer to tracked GWES PID.
 * @param gwes_started Pointer to the GWES sticky launch flag.
 * @param explorer_pid Pointer to tracked explorer PID.
 * @return Nothing.
 */
static void core_poll_supervised_processes(
    long* shell_pid,
    long* gwes_pid,
    int* gwes_started,
    long* explorer_pid,
    unsigned long* explorer_retry_after_msec) {
    if (shell_pid != NULL && *shell_pid >= 0 && core_task_is_gone(*shell_pid)) {
        core_handle_shell_exit(shell_pid);
    }
    if (gwes_pid != NULL && *gwes_pid >= 0 && core_task_is_gone(*gwes_pid)) {
        core_handle_gwes_exit(gwes_pid, gwes_started, explorer_pid, explorer_retry_after_msec);
    }
    if (explorer_pid != NULL && *explorer_pid >= 0 && core_task_is_gone(*explorer_pid)) {
        core_handle_explorer_exit(
            explorer_pid,
            explorer_retry_after_msec,
            core_uptime_msec() + CORE_EXPLORER_RETRY_MSEC);
    }
}

/*
 * core_subscribe_process_events
 *
 * Using the kernel event broker lets core supervise shell, GWES, and explorer
 * concurrently without adding userspace thread support just for the launcher.
 *
 * @param subscription_id Receives the created subscription on success.
 * @return Zero on success, or a negative status code on failure.
 */
static long core_subscribe_process_events(KernelEventSubscriptionId* subscription_id) {
    KernelEventSubscriptionRequest request;

    if (subscription_id == NULL) {
        return -1L;
    }

    *subscription_id = 0ULL;
    core_zero_memory(&request, sizeof(request));
    request.version = KERNEL_EVENT_ABI_VERSION;
    request.family_mask = KERNEL_EVENT_FAMILY_PROCESS;
    return subscribeKernelEvents(&request, subscription_id);
}

/*
 * core_maybe_launch_explorer
 *
 * Explorer depends on both the shell-first boot ordering and a live GWES PID,
 * so core only launches it once those prerequisites are satisfied.
 *
 * @param shell_pid Current shell PID.
 * @param gwes_pid Current GWES PID.
 * @param explorer_pid Pointer to tracked explorer PID.
 * @param last_launch_status Sticky launch-failure code used to avoid log spam.
 * @return Nothing.
 */
static void core_maybe_launch_explorer(
    long shell_pid,
    long gwes_pid,
    unsigned long gwes_ready_after_msec,
    long* explorer_pid,
    long* last_launch_status,
    unsigned long* retry_after_msec,
    int* launch_suppressed) {
    const unsigned long now = core_uptime_msec();

    if (explorer_pid == NULL || *explorer_pid >= 0) {
        return;
    }
    if (shell_pid < 0 || gwes_pid < 0) {
        return;
    }
    if (launch_suppressed != NULL && *launch_suppressed) {
        return;
    }
    if (now < gwes_ready_after_msec) {
        return;
    }
    if (retry_after_msec != NULL && *retry_after_msec != 0UL && now < *retry_after_msec) {
        return;
    }

    *explorer_pid = core_launch_explorer(-1, last_launch_status, retry_after_msec, launch_suppressed);
}

/*
 * core_wait_for_next_event
 *
 * Once every supervised process is alive, core can block on the event broker
 * instead of polling constantly. Busy results fall back to the shared backoff.
 *
 * @param subscription_id Active process-event subscription.
 * @return Zero on success, or a negative status code when waiting failed.
 */
static long core_wait_for_next_event(KernelEventSubscriptionId subscription_id, unsigned long deadline_msec) {
    KernelEventSubscriptionInfo info;
    long status;

    if (subscription_id == 0ULL) {
        return CORE_STATUS_NOT_SUPPORTED;
    }

    for (;;) {
        core_zero_memory(&info, sizeof(info));
        info.version = KERNEL_EVENT_ABI_VERSION;
        status = queryKernelEvents(subscription_id, &info);
        if (status < 0L) {
            return status;
        }
        if (info.queued_count != 0U) {
            return 0L;
        }
        if (core_uptime_msec() >= deadline_msec) {
            return StatusBusy;
        }

        if (sleepMs(CORE_SUPERVISOR_POLL_MSEC) < 0) {
            for (volatile unsigned long spin = 0; spin < CORE_SPIN_BACKOFF_COUNT; ++spin) {
                asm volatile("" ::: "memory");
            }
        }
    }
}

/*
 * core_pause_supervisor
 *
 * The userspace supervisor still needs one bounded backoff path for launch or
 * wait failures. Sleeping first keeps the loop cooperative, while the spin
 * fallback preserves progress if the sleep syscall is temporarily unavailable.
 *
 * @return Nothing.
 */
static void core_pause_supervisor(void) {
    if (sleepMs(CORE_SUPERVISOR_POLL_MSEC) < 0) {
        for (volatile unsigned long spin = 0; spin < CORE_SPIN_BACKOFF_COUNT; ++spin) {
            asm volatile("" ::: "memory");
        }
    }
}

/*
 * core_wait_for_shell_exit
 *
 * Waiting directly on the shell PID is more reliable than a timer-driven poll
 * loop on the current cooperative scheduler. If the kernel does not support
 * that wait path for a given build, fall back to task-info polling so the
 * supervisor still treats the launcher PID as reference-only metadata rather
 * than a hard parent/child dependency.
 *
 * @param shell_pid PID of the currently supervised shell process.
 * @return Zero when the shell is gone, or a negative kernel status when the
 * wait itself failed and the shell still appears alive.
 */
static void core_supervisor_loop(long initial_shell_pid) {
    KernelEventSubscriptionId subscription_id = 0ULL;
    KernelEventRecord records[CORE_EVENT_BATCH_CAPACITY];
    long shell_pid = initial_shell_pid;
    long gwes_pid = -1;
    long explorer_pid = -1;
    long shell_launch_status = 0;
    long explorer_launch_status = 0;
    long event_status;
    int gwes_started = 0;
    int explorer_launch_suppressed = 0;
    unsigned long gwes_ready_after_msec = 0UL;
    unsigned long explorer_retry_after_msec = 0UL;
    unsigned long explorer_watchdog_after_msec = core_uptime_msec() + CORE_EXPLORER_WATCHDOG_MSEC;

    event_status = core_subscribe_process_events(&subscription_id);
    if (event_status < 0L) {
        (void)core_write_pid_event("core.exe: process-event subscribe failed status=", event_status);
        subscription_id = 0ULL;
    }
    else {
        (void)core_write_line("core.exe: process-event subscription ready");
    }

    (void)core_write_line("core.exe: entering supervisor loop");
    for (;;) {
        U32 record_count = 0U;
        long read_status = CORE_STATUS_NOT_SUPPORTED;

        if (shell_pid < 0) {
            shell_pid = core_launch_shell(-1, &shell_launch_status);
        }

        if (shell_pid >= 0 && gwes_pid < 0) {
            long launched_pid = core_launch_gwes();

            if (launched_pid >= 0) {
                gwes_pid = launched_pid;
                gwes_started = 1;
                explorer_launch_suppressed = 0;
                gwes_ready_after_msec = core_uptime_msec() + CORE_GWES_STARTUP_GRACE_MSEC;
                if (explorer_retry_after_msec < gwes_ready_after_msec) {
                    explorer_retry_after_msec = gwes_ready_after_msec;
                }
            }
            else {
                gwes_started = 0;
                gwes_ready_after_msec = 0UL;
            }
        }

        core_maybe_launch_explorer(
            shell_pid,
            gwes_pid,
            gwes_ready_after_msec,
            &explorer_pid,
            &explorer_launch_status,
            &explorer_retry_after_msec,
            &explorer_launch_suppressed);

        if (core_timer_expired(&explorer_watchdog_after_msec, CORE_EXPLORER_WATCHDOG_MSEC)) {
            core_run_explorer_watchdog(
                shell_pid,
                gwes_pid,
                &explorer_pid,
                &explorer_retry_after_msec);

            core_maybe_launch_explorer(
                shell_pid,
                gwes_pid,
                gwes_ready_after_msec,
                &explorer_pid,
                &explorer_launch_status,
                &explorer_retry_after_msec,
                &explorer_launch_suppressed);
        }

        if (subscription_id != 0ULL) {
            read_status = readKernelEvents(subscription_id, records, CORE_EVENT_BATCH_CAPACITY, &record_count);
            if (read_status < 0L) {
                (void)core_write_pid_event("core.exe: process-event read failed status=", read_status);
            }
        }

        if (record_count == 0U) {
            core_poll_supervised_processes(
                &shell_pid,
                &gwes_pid,
                &gwes_started,
                &explorer_pid,
                &explorer_retry_after_msec);

            if (shell_pid >= 0 && gwes_pid >= 0 && explorer_pid >= 0 && subscription_id != 0ULL) {
                long wait_status = core_wait_for_next_event(subscription_id, explorer_watchdog_after_msec);

                if (wait_status < 0L && wait_status != StatusBusy) {
                    (void)core_write_pid_event("core.exe: process-event wait failed status=", wait_status);
                    core_pause_supervisor();
                }
                continue;
            }

            core_pause_supervisor();
            continue;
        }

        for (U32 index = 0U; index < record_count; ++index) {
            if ((KernelEventType)records[index].type != KernelEventTypeProcessExited) {
                continue;
            }

            core_report_process_fault(&records[index]);

            core_handle_process_exit(
                (long)records[index].source_process_id,
                &shell_pid,
                &gwes_pid,
                &gwes_started,
                &explorer_pid,
                &explorer_retry_after_msec);
        }
    }
}

int main(void) {
    char task_name[64] = { 0 };
    char task_args[128] = { 0 };
    long initial_shell_pid = -1;
    long initial_shell_launch_status = 0;

    if (getTaskName(task_name, sizeof(task_name)) < 0) {
        task_name[0] = '\0';
    }
    if (getTaskArgs(task_args, sizeof(task_args)) < 0) {
        task_args[0] = '\0';
    }

    (void)core_write_line("core.exe: starting shell supervisor");
    (void)core_write_key_value("core.exe: task=", task_name);
    (void)core_write_key_value("core.exe: args=", task_args);
    (void)core_write_line("core.exe: shell-first boot path");
    initial_shell_pid = core_launch_shell(-1, &initial_shell_launch_status);
#if CORE_ENABLE_BOOT_DLLSMOKE
    (void)core_launch_boot_smoke();
#else
    /*
     * Keep the GUI boot path stable while the shared-memory DLL smoke route is
     * being debugged separately. GWES and the shell still start normally, and
     * the smoke app can be launched manually once the allocator issue is fixed.
     */
    (void)core_write_line("core.exe: boot dll smoke disabled");
#endif

    core_supervisor_loop(initial_shell_pid);
    return 0;
}
