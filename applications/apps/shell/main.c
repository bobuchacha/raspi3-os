#include "ros_user_runtime.h"

int AppMain(void) {
    ros_write_line("shell.exe: simple user shell (stub)");
    ros_write_line("shell.exe: type commands via kernel shell (not implemented)");

    for (;;) {
        ros_write("user-shell> ");
        ros_sleep(1000);
    }

    return 0;
}
