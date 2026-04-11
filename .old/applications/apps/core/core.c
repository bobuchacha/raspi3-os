#include "user_runtime.h"
#include "app/kernel_gui.h"
#include "sample_abi.h"

#define CORE_LOOP_SLEEP_MSEC 100UL
#define CORE_LOG_DRAIN_MAX 16U

#define CORE_SHELL_PATH "/bin/shell.exe"
#define CORE_GWES_PATH "/bin/gwes.exe"

typedef struct CoreSharedInputObserverStruct {
    RosKernelGuiSharedInputRegion* region;
    uint32_t consumerIndex;
    unsigned long observedCount;
    unsigned long pointerCount;
    unsigned long keyCount;
    int attached;
} CoreSharedInputObserver;

static CoreSharedInputObserver g_core_shared_input;

static void core_shared_input_barrier(void) {
    asm volatile("dmb ishld" ::: "memory");
}

static int core_shared_input_acquire(void) {
    RosKernelGuiSharedInputView view;

    if (controlGui(ROS_KERNEL_GUI_CONTROL_SHARED_INPUT_ACQUIRE, (unsigned long)&view) != 0) {
        return -1;
    }
    if (view.version != ROS_KERNEL_GUI_SHARED_INPUT_VERSION ||
        view.view_address == 0ULL ||
        view.consumer_index >= ROS_KERNEL_GUI_SHARED_INPUT_MAX_CONSUMERS ||
        view.view_size < sizeof(RosKernelGuiSharedInputRegion)) {
        return -1;
    }

    g_core_shared_input.region = (RosKernelGuiSharedInputRegion*)(uintptr_t)view.view_address;
    g_core_shared_input.consumerIndex = view.consumer_index;
    g_core_shared_input.attached = 1;
    return 0;
}

static void core_shared_input_observe(void) {
    volatile RosKernelGuiSharedInputRegion* region;
    volatile RosKernelGuiSharedInputConsumer* consumer;

    if (!g_core_shared_input.attached || !g_core_shared_input.region) {
        return;
    }

    region = (volatile RosKernelGuiSharedInputRegion*)g_core_shared_input.region;
    consumer = &region->consumers[g_core_shared_input.consumerIndex];
    core_shared_input_barrier();
    for (;;) {
        uint64_t tail = region->tail_sequence;
        uint64_t oldest = tail > ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY ? (tail - ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY) : 0ULL;
        uint64_t head = consumer->head_sequence;

        if (head < oldest) {
            consumer->drop_count += oldest - head;
            consumer->head_sequence = oldest;
            head = oldest;
        }
        if (head >= tail) {
            consumer->last_seen_msec = (uint32_t)getUptimeMs();
            return;
        }

        {
            volatile RosKernelGuiSharedInputRecord* record = &region->records[head % ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY];

            if (record->sequence != head) {
                consumer->last_seen_msec = (uint32_t)getUptimeMs();
                return;
            }

            g_core_shared_input.observedCount++;
            if (record->event.type == ROS_KERNEL_GUI_INPUT_EVENT_KEY_DOWN || record->event.type == ROS_KERNEL_GUI_INPUT_EVENT_KEY_UP) {
                g_core_shared_input.keyCount++;
            }
            else if (record->event.type != ROS_KERNEL_GUI_INPUT_EVENT_NONE) {
                g_core_shared_input.pointerCount++;
            }

            consumer->head_sequence = head + 1ULL;
            consumer->last_seen_msec = (uint32_t)getUptimeMs();
        }

        core_shared_input_barrier();
    }
}

static void init_write_key_value(const char* label, const char* text) {
    char line[192];
    char* cursor = line;

    cursor = appendText(cursor, label);
    cursor = appendText(cursor, text ? text : "<none>");
    *cursor = '\0';
    writeLine(line);
}

static void init_write_cycle(unsigned long cycle, long total, unsigned long status, unsigned long pulse) {
    char line[192];
    char* cursor = line;

    cursor = appendText(cursor, "core.exe: cycle ");
    cursor = appendUnsignedLong(cursor, cycle);
    cursor = appendText(cursor, " total=");
    cursor = appendUnsignedLong(cursor, (unsigned long)total);
    cursor = appendText(cursor, " status=");
    cursor = appendUnsignedLong(cursor, status);
    cursor = appendText(cursor, " pulse=");
    cursor = appendUnsignedLong(cursor, pulse);
    *cursor = '\0';
    writeLine(line);
}

static void core_write_pid_message(const char* label, long pid) {
    char line[128];
    char* cursor = line;

    cursor = appendText(cursor, label);
    if (pid >= 0) {
        cursor = appendUnsignedLong(cursor, (unsigned long)pid);
    }
    else {
        cursor = appendText(cursor, "<none>");
    }
    *cursor = '\0';
    writeLine(line);
}

static int core_task_is_alive(long pid) {
    UserTaskInfo info;

    if (pid < 0) {
        return 0;
    }
    if (getTaskInfo(pid, &info) <= 0) {
        return 0;
    }

    return info.state == 1 || info.state == 2 || info.state == 3 || info.state == 4;
}

static void core_drain_log_queue(void) {
    char line[512];
    unsigned long drained = 0;

    while (drained < CORE_LOG_DRAIN_MAX) {
        long status = receiveLog(line, sizeof(line));

        if (status <= 0) {
            return;
        }

        writeText(line);
        drained++;
    }
}

static long core_spawn_program_if_needed(long pid, const char* path, const char* name, const char* label) {
    char line[160];
    char* cursor = line;

    if (core_task_is_alive(pid)) {
        return pid;
    }

    if (pid >= 0) {
        cursor = appendText(cursor, "core.exe: restarting ");
        cursor = appendText(cursor, label);
        cursor = appendText(cursor, " pid=");
        cursor = appendUnsignedLong(cursor, (unsigned long)pid);
        *cursor = '\0';
        writeLine(line);
    }

    pid = spawnTask(path, name, "");
    if (pid >= 0) {
        cursor = line;
        cursor = appendText(cursor, "core.exe: launched ");
        cursor = appendText(cursor, label);
        cursor = appendText(cursor, " pid=");
        cursor = appendUnsignedLong(cursor, (unsigned long)pid);
        *cursor = '\0';
        writeLine(line);
    }
    else {
        cursor = line;
        cursor = appendText(cursor, "core.exe: failed to launch ");
        cursor = appendText(cursor, label);
        *cursor = '\0';
        writeLine(line);
    }

    return pid;
}

/*
 * Spawn the interactive shell only when the supervisor is not already tracking
 * a live shell child.
 */
static void core_supervisor_loop(void) {
    long shell_pid = -1;
    long gwes_pid = -1;

    writeLine("core.exe: entering supervisor loop");
    for (;;) {
        core_drain_log_queue();
        core_shared_input_observe();
        shell_pid = core_spawn_program_if_needed(shell_pid, CORE_SHELL_PATH, "shell", "shell");
        gwes_pid = core_spawn_program_if_needed(gwes_pid, CORE_GWES_PATH, "gwes", "gwes");

        core_shared_input_observe();
        core_drain_log_queue();
        (void)sleepMs(CORE_LOOP_SLEEP_MSEC);
    }
}

int AppMain(void) {
    char task_name[64];
    char task_args[128];
    unsigned long sample_lib_base;
    unsigned long sample_driver_base;
    sample_lib_calculate_total_fn calculate_total;
    sample_lib_profile_fn profile;
    sample_driver_dispatch_fn dispatch;

    getTaskName(task_name, sizeof(task_name));
    getTaskArgs(task_args, sizeof(task_args));
    writeLine("core.exe: starting user-mode storefront demo");
    init_write_key_value("core.exe: task=", task_name);
    init_write_key_value("core.exe: args=", task_args[0] ? task_args : "<none>");

    (void)core_shared_input_acquire();

    sample_lib_base = openSharedLibrary(SAMPLE_LIB_PATH);
    sample_driver_base = openSharedLibrary(SAMPLE_DRIVER_PATH);
    if (sample_lib_base == 0 || sample_driver_base == 0) {
        writeLine("core.exe: failed to map sample modules");
        exitProcess(1);
    }

    calculate_total = (sample_lib_calculate_total_fn)exportSharedLibrary(SAMPLE_LIB_PATH, "sample_lib_calculate_total");
    profile = (sample_lib_profile_fn)exportSharedLibrary(SAMPLE_LIB_PATH, "sample_lib_profile");
    dispatch = (sample_driver_dispatch_fn)exportSharedLibrary(SAMPLE_DRIVER_PATH, "sample_driver_dispatch");
    if (!calculate_total || !profile || !dispatch) {
        writeLine("core.exe: failed to resolve sample exports");
        exitProcess(1);
    }

    init_write_key_value("core.exe: pricing-profile=", profile());
    for (unsigned long cycle = 1; cycle <= 3; cycle++) {
        long total = calculate_total(120 + (long)(cycle * 15), 9, 4);
        unsigned long status = dispatch(SAMPLE_DRIVER_OP_STATUS, cycle);
        unsigned long pulse = dispatch(SAMPLE_DRIVER_OP_PULSE, (unsigned long)total);

        init_write_cycle(cycle, total, status, pulse);
    }

    writeLine("core.exe: demo complete; entering core loop");
    core_supervisor_loop();
    exitProcess(0);
}
