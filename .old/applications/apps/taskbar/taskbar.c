#include "user_runtime.h"
#include "app/gui.h"
#include "app/core_log.h"

#define TASKBAR_TICK_MSEC 1000UL

int AppMain(void) {
    unsigned long gui_base;

    debugInfo("taskbar.exe: starting");

    gui_base = openSharedLibrary(ROS_GUI_PATH);
    if (gui_base == 0UL) {
        writeLine("taskbar.exe: failed to load gui.dll");
        exitProcess(1);
    }

    if (guiInit() != 0) {
        writeLine("taskbar.exe: gui init failed");
        exitProcess(1);
    }

    for (;;) {
        (void)guiShowTaskbar(1UL);
        (void)guiTaskbarTick();
        (void)sleepMs(TASKBAR_TICK_MSEC);
    }
}