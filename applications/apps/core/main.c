#include "ros_user_runtime.h"
#include "sample_abi.h"

static void init_write_key_value(const char* label, const char* text) {
    char line[192];
    char* cursor = line;

    cursor = ros_append_text(cursor, label);
    cursor = ros_append_text(cursor, text ? text : "<none>");
    *cursor = '\0';
    ros_write_line(line);
}

static void init_write_cycle(unsigned long cycle, long total, unsigned long status, unsigned long pulse) {
    char line[192];
    char* cursor = line;

    cursor = ros_append_text(cursor, "core.exe: cycle ");
    cursor = ros_append_ulong(cursor, cycle);
    cursor = ros_append_text(cursor, " total=");
    cursor = ros_append_ulong(cursor, (unsigned long)total);
    cursor = ros_append_text(cursor, " status=");
    cursor = ros_append_ulong(cursor, status);
    cursor = ros_append_text(cursor, " pulse=");
    cursor = ros_append_ulong(cursor, pulse);
    *cursor = '\0';
    ros_write_line(line);
}

int AppMain(void) {
    char task_name[64];
    char task_args[128];
    sample_lib_calculate_total_fn calculate_total;
    sample_lib_profile_fn profile;
    sample_driver_dispatch_fn dispatch;

    ros_task_name(task_name, sizeof(task_name));
    ros_task_args(task_args, sizeof(task_args));
    ros_write_line("core.exe: starting user-mode storefront demo");
    init_write_key_value("core.exe: task=", task_name);
    init_write_key_value("core.exe: args=", task_args[0] ? task_args : "<none>");

    if (ros_shlib_open(SAMPLE_LIB_PATH) == 0 || ros_shlib_open(SAMPLE_DRIVER_PATH) == 0) {
        ros_write_line("core.exe: failed to map sample modules");
        return 1;
    }

    calculate_total = (sample_lib_calculate_total_fn)ros_shlib_export(SAMPLE_LIB_PATH, "sample_lib_calculate_total");
    profile = (sample_lib_profile_fn)ros_shlib_export(SAMPLE_LIB_PATH, "sample_lib_profile");
    dispatch = (sample_driver_dispatch_fn)ros_shlib_export(SAMPLE_DRIVER_PATH, "sample_driver_dispatch");
    if (!calculate_total || !profile || !dispatch) {
        ros_write_line("core.exe: failed to resolve sample exports");
        return 1;
    }

    init_write_key_value("core.exe: pricing-profile=", profile());
    for (unsigned long cycle = 1; cycle <= 3; cycle++) {
        long total = calculate_total(120 + (long)(cycle * 15), 9, 4);
        unsigned long status = dispatch(SAMPLE_DRIVER_OP_STATUS, cycle);
        unsigned long pulse = dispatch(SAMPLE_DRIVER_OP_PULSE, (unsigned long)total);

        init_write_cycle(cycle, total, status, pulse);
        ros_sleep(750);
    }

    ros_write_line("core.exe: demo complete; launching shell.exe");
    (void)ros_spawn("/bin/shell.exe", "shell", "");

    return 0;
}
