#include "user_runtime.h"

#define CORE_SUPERVISOR_POLL_MSEC 100UL
#define CORE_SPIN_BACKOFF_COUNT 50000UL
#define CORE_ENABLE_BOOT_DLLSMOKE 0
#define CORE_DLLSMOKE_PATH "/bin/dllsmoke.exe"
#define CORE_GWES_PATH "/bin/gwes.exe"
#define CORE_SHELL_PATH "/bin/shell.exe"
#define CORE_STATUS_NOT_FOUND (-2L)
#define CORE_STATUS_NOT_SUPPORTED (-4L)
#define CORE_WINDOW_SERVER_SHARED_STATE_NAME "ros.window-server.state"
#define CORE_WINDOW_SERVER_SHARED_STATE_VERSION 1UL

typedef struct CoreWindowServerSharedState {
    unsigned long version;
    long pid;
    char name[32];
} CoreWindowServerSharedState;

static CoreWindowServerSharedState* g_core_window_server_state;

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
static long core_wait_for_shell_exit(long shell_pid) {
    long exit_code = 0;
    long status;

    if (shell_pid < 0) {
        return 0;
    }

    status = waitPid(shell_pid, &exit_code);
    if (status == 0) {
        return 0;
    }

    if ((status == CORE_STATUS_NOT_SUPPORTED) || (status == CORE_STATUS_NOT_FOUND)) {
        while (core_task_is_alive(shell_pid)) {
            core_pause_supervisor();
        }
        return 0;
    }

    if (!core_task_is_alive(shell_pid)) {
        return 0;
    }

    return status;
}

static void core_supervisor_loop(long initial_shell_pid) {
    long shell_pid = -1;
    long last_launch_status = 0;
    long last_wait_status = 0;
    int gwes_started = 0;

    shell_pid = initial_shell_pid;

    (void)core_write_line("core.exe: entering supervisor loop");
    for (;;) {
        long wait_status;

        if (shell_pid < 0) {
            shell_pid = core_launch_shell(shell_pid, &last_launch_status);
            if (shell_pid < 0) {
                core_pause_supervisor();
                continue;
            }
        }

        core_maybe_launch_gwes(&gwes_started);

        wait_status = core_wait_for_shell_exit(shell_pid);
        if (wait_status < 0) {
            if (last_wait_status != wait_status) {
                (void)core_write_pid_event("core.exe: shell wait failed status=", wait_status);
                last_wait_status = wait_status;
            }
            core_pause_supervisor();
            continue;
        }

        last_wait_status = 0;
        last_launch_status = 0;
        (void)core_write_pid_event("core.exe: shell exited pid=", shell_pid);
        shell_pid = core_launch_shell(shell_pid, &last_launch_status);
    }
}

int AppMain(void) {
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
