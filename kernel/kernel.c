//
// Created by Thang Cao on 5/23/24.
//
#include "device.h"
#include "filesystem/filesystem.h"
#include "filesystem/vfs/vfs.h"
#include "graphics.h"
#include "gui.h"
#include "hal/hal.h"
#include "irq.h" // some code in device irq or arch irq
#include "log.h"
#include "memory.h"
#include "module.h"
#include "percpu.h"
#include "platform/cpu/debug_uart.h"
#include "printf.h"
#include "task.h"
#include "timer.h"
#include "touch.h"
#include "user-exe.h"
#include "utils.h"
#include "secondary.h"

// file.c
void file_close(PFileDesc f);
Buffer file_read(PFileDesc f, int position, int length);
PFileDesc file_open(char* path, int mode);

extern int file_last_status;
void kernel_shell_main(Pointer arg);

/**
 * Kernel thread entry that loads the default core executable into the current task.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Nothing. On failure the current task is terminated.
 */
void kernel_load_user_program() {
    // Replace this kernel thread's task image with the configured init executable.
    module_load_boot_modules();
    log_info("Boot modules loaded; executing /bin/core.exe");

    if (spawn_user_program("/bin/core.exe", "core", "") < 0) {
        log_warning("Unable to spawn /bin/core.exe; falling back to kernel shell");
        kernel_shell_main(0);
        exit_current_process(-1);
    }

    log_info("/bin/core.exe spawn requested; returning to scheduler");
}

static void kernel_draw_boot_graphics(void) {
    graphics_clear(0x00101820);
    graphics_fill_rect(48, 48, 320, 120, 0x00D97706);
    graphics_fill_rect(72, 80, 272, 16, 0x00F8FAFC);
    graphics_fill_rect(72, 112, 220, 16, 0x0038BDF8);
    graphics_fill_rect(400, 48, 96, 192, 0x00F97316);
    graphics_fill_rect(520, 48, 96, 192, 0x00A855F7);
    graphics_fill_rect(640, 48, 96, 192, 0x0022C55E);
}

static void kernel_draw_touch_marker(unsigned int x, unsigned int y) {
    unsigned int clamped_x = x;
    unsigned int clamped_y = y;

    if (graphics_width() == 0 || graphics_height() == 0) {
        return;
    }

    if (clamped_x >= graphics_width()) {
        clamped_x = graphics_width() - 1;
    }
    if (clamped_y >= graphics_height()) {
        clamped_y = graphics_height() - 1;
    }

    graphics_fill_rect(clamped_x > 10 ? clamped_x - 10 : 0, clamped_y, 21, 3, 0x00FFFFFF);
    graphics_fill_rect(clamped_x, clamped_y > 10 ? clamped_y - 10 : 0, 3, 21, 0x00FFFFFF);
}

static void kernel_update_touch_demo(void) {
    static Bool last_pressed = false;
    static UInt last_x = 0;
    static UInt last_y = 0;
    const TouchState* touch_state;
    long result = 0;

    if (!graphics_is_ready()) {
        return;
    }

    touch_state = touch_get_state();
    if (touch_state == NULL) {
        return;
    }

    if (touch_state->pressed == last_pressed && (!touch_state->pressed || (touch_state->x == last_x && touch_state->y == last_y))) {
        return;
    }

    if (touch_state->pressed) {
        if (module_invoke("usbmouse", "inject_pointer", touch_state->x, touch_state->y, &result) != 0) {
            module_invoke("fbgui", "push_pointer", touch_state->x, touch_state->y, &result);
        }
        last_x = touch_state->x;
        last_y = touch_state->y;
    }
    else {
        if (module_invoke("usbmouse", "inject_pointer", GUI_POINTER_HIDDEN, GUI_POINTER_HIDDEN, &result) != 0) {
            module_invoke("fbgui", "push_pointer", GUI_POINTER_HIDDEN, GUI_POINTER_HIDDEN, &result);
        }
    }

    last_pressed = touch_state->pressed;
}
/**
 * Main kernel entry point after low-level platform setup completes.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Never returns. The kernel stays inside the scheduler loop indefinitely.
 */
void kernel_main() {
    // Bring up the HAL abstraction before touching board devices or memory services.
    hal_init();

    // CPU0 needs a valid idle context once PID 1 starts yielding in userspace.
    percpu_init_idle_task(0);

    // Initialize console-capable devices so later logging becomes visible.
    device_init();
    // init memory management

    // Build page allocation and heap state before higher-level subsystems allocate memory.
    log_info("Enabling memory management...\n");
    init_memory_management();
    current_task->mm.pgd = get_pgd();

    log_info("Initializing module subsystem...\n");
    module_subsystem_init();

    // init file system

    // Mount storage and VFS support before any executable or directory operations run.
    log_info("Initializing filesystem...\n");
    filesystem_init();

    // Initialize the framebuffer and paint a small test pattern when graphics are available.
    log_info("Initializing graphics...\n");
    if (device_init_graphics() == 0 && graphics_is_ready()) {
        kernel_draw_boot_graphics();
    }

    // Ask the firmware for the official touchscreen shared buffer when present.
    log_info("Initializing touch input...\n");
    device_init_touch();

    // Keep secondaries parked until the bootloader-owned secondary trampoline is stable.
    log_info("Skipping secondary CPU wake while SMP handoff is unstable...\n");

    /* userspace launcher will be scheduled from the main loop after boot ticks */

    // create another thread

    // Spawn extra loader threads for the current multi-process boot experiment.
    // process_copy_thread(PF_KTHREAD, (Address)&kernel_load_user_program_2, 0);
    // process_copy_thread(PF_KTHREAD, (Address)&kernel_load_user_program_2, 0);

    //    extern void *user_begin, *user_end;
    //    create_user_process((Address)(&user_begin), &user_end-&user_begin);

    // Keep reclaiming zombies and handing CPU time to runnable tasks forever.

    log_info("Entering main scheduler loop...\n");
    int boot_spawn_counter = 3; /* scheduler iterations before auto-spawn */
    Bool boot_spawned = false;
    while (1) {
        //  kinfo("Printing from thread %s. Yeilding...", current_task->name);

        // Poll touch firmware state and refresh the simple marker overlay when input changes.
        device_poll_touch();
        kernel_update_touch_demo();

        // Reclaim resources from tasks that exited on a previous schedule cycle.
        cleanup_zombie_processes();
        module_run_idle_loops();

        // Yield to the scheduler so runnable kernel or user tasks can execute.
        schedler_schedule();

        if (!boot_spawned) {
            if (--boot_spawn_counter <= 0) {
                log_info("Boot delay elapsed; spawning /bin/core.exe now...");
                if (spawn_user_program("/bin/core.exe", "core", "") < 0) {
                    log_warning("Unable to spawn /bin/core.exe from scheduler loop; falling back to kernel shell");
                    kernel_shell_main(0);
                }
                boot_spawned = true;
            }
        }

        // delay(1000000000);
    }
}
