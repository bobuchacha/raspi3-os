/*
 * kernel.c
 *
 * Primary kernel bootstrap and steady-state scheduler loop.
 *
 * This file brings up core subsystems in dependency order, performs the first
 * userspace handoff, and then stays in the idle dispatch loop for the life of
 * the system.
 */
#include "device.h"
#if defined(ROS_BOARD_VIRT)
#include "device/virt.h"
#endif
#include "filesystem/filesystem.h"
#include "filesystem/vfs/vfs.h"
#include "graphics.h"
#include "gui.h"
#include "hal/hal.h"
#include "input.h"
#include "irq.h" // some code in device irq or arch irq
#include "lib/lib.h"
#include "kernel_module_tests.h"
#include "log.h"
#include "memory.h"
#include "module.h"
#include "percpu.h"
#include "platform/cpu/debug_uart.h"
#include "printf.h"
#include "task.h"
#include "timer.h"
#include "touch.h"
#include "ipc_kernel.h"
#include "user-exe.h"
#include "utils.h"
#include "secondary.h"

 /* Legacy file helpers provided by the older filesystem layer. */
void file_close(PFileDesc fileDesc);
Buffer file_read(PFileDesc fileDesc, int position, int length);
PFileDesc file_open(char* path, int mode);

extern int file_last_status;
void kernel_shell_main(Pointer arg);

typedef struct __attribute__((packed)) KernelBootPsf2HeaderStruct {
    unsigned int magic;
    unsigned int version;
    unsigned int header_size;
    unsigned int flags;
    unsigned int glyph_count;
    unsigned int char_size;
    unsigned int height;
    unsigned int width;
} KernelBootPsf2Header;

extern const unsigned char _binary_build_screenfont_font_psf_start[];

static const KernelBootPsf2Header* kernel_boot_font_header(void) {
    return (const KernelBootPsf2Header*)_binary_build_screenfont_font_psf_start;
}

static const unsigned char* kernel_boot_font_glyph(unsigned int ch) {
    const KernelBootPsf2Header* header = kernel_boot_font_header();
    const unsigned char* glyphs = _binary_build_screenfont_font_psf_start + header->header_size;

    if (ch >= header->glyph_count) {
        ch = '?';
    }
    return glyphs + ((ULong)ch * header->char_size);
}

static void kernel_boot_draw_char(unsigned int x, unsigned int y, unsigned char ch, unsigned int fg, unsigned int bg) {
    const KernelBootPsf2Header* header = kernel_boot_font_header();
    const unsigned char* glyph = kernel_boot_font_glyph(ch);

    for (unsigned int row = 0; row < header->height; row++) {
        unsigned char bits = glyph[row];
        for (unsigned int col = 0; col < header->width; col++) {
            unsigned int color = (bits & (1U << (7U - col))) ? fg : bg;
            graphics_draw_pixel(x + col, y + row, color);
        }
    }
}

static void kernel_boot_draw_text(unsigned int x, unsigned int y, const char* text, unsigned int fg, unsigned int bg) {
    const KernelBootPsf2Header* header = kernel_boot_font_header();

    while (text && *text != '\0') {
        kernel_boot_draw_char(x, y, (unsigned char)*text++, fg, bg);
        x += header->width;
    }
}

/*
 * kernel_start_initial_userspace
 *
 * Finish the one-time boot transition from the kernel-only startup phase into
 * the normal mixed kernel/userspace runtime.
 *
 * Responsibilities:
 * - load any boot-time compatibility modules exactly once,
 * - spawn the initial userspace program (`/bin/core.exe`) while PID 1 still
 *   belongs to that first real process,
 * - start the schedproc housekeeper only after the initial userspace spawn
 *   decision has been made, so the reaper thread does not steal PID 1,
 * - start the kernel-side module smoke runner only after the same PID 1
 *   decision, so test threads also stay out of the first userspace slot,
 * - and fall back to the kernel shell when the initial userspace program
 *   cannot be launched.
 *
 * Keeping this logic in a dedicated helper instead of the idle loop makes the
 * scheduler path easier to reason about: the loop below is now only for
 * housekeeping and dispatch, not for one-shot boot sequencing.
 */
static void kernel_start_initial_userspace(void) {
    int corePid;

    module_load_boot_modules();
    log_info("Boot modules loaded; spawning /bin/core.exe");

    /*
     * Spawn the first userspace process before starting the background
     * housekeeper so `/bin/core.exe` remains the first visible userspace PID.
     */
    corePid = spawn_user_program("/bin/core.exe", "core", "");

    /*
     * The reaper thread owns deferred cleanup for exited tasks. Start it once
     * the initial spawn decision is complete so later process exits have a
     * safe kernel-thread landing point.
     */
    schedproc_start_housekeeper();

    /*
     * Run module smoke suites on a separate kernel thread so the tests execute
     * against the live freestanding kernel without perturbing the boot path.
     */
     // kernel_module_tests_start();

    if (corePid < 0) {
        log_warning("Unable to spawn /bin/core.exe; falling back to kernel shell");
        kernel_shell_main(0);
    }
}

static void kernel_start_virt_console(void) {
    log_info("virt board: skipping Pi storage/userspace boot; entering kernel shell");
    kernel_shell_main(0);
}

/* Paint a simple framebuffer splash so graphics bring-up is visible immediately. */
static void kernel_draw_boot_graphics(void) {
    graphics_clear(0x00101820);
    graphics_fill_rect(48, 48, 320, 120, 0x00D97706);
    graphics_fill_rect(72, 80, 272, 16, 0x00F8FAFC);
    graphics_fill_rect(72, 112, 220, 16, 0x0038BDF8);
    graphics_fill_rect(400, 48, 96, 192, 0x00F97316);
    graphics_fill_rect(520, 48, 96, 192, 0x00A855F7);
    graphics_fill_rect(640, 48, 96, 192, 0x0022C55E);
    kernel_boot_draw_text(72U, 148U, "Welcome to raspi3-os", 0x00F8FAFCU, 0x00101820U);

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
    init_schedler();
    current_task->mm.pgd = get_pgd();

    log_info("Initializing module subsystem...\n");
    module_subsystem_init();
    input_init();

    // Bring up the shared in-kernel IPC context before any subsystem or test uses it.
    (void)ipc_kernel_init();

    // init file system

    // Mount storage and VFS support before any executable or directory operations run.
    log_info("Initializing filesystem...\n");
#if defined(ROS_BOARD_VIRT)
    if (virtio_blk_is_ready()) {
        filesystem_init();
    }
    else {
        log_info("virt board: filesystem bring-up skipped because no block device is present\n");
    }
#else
    filesystem_init();
#endif

    // Initialize the framebuffer and paint a small test pattern when graphics are available.
    log_info("Initializing graphics...\n");
    if (device_init_graphics() == 0 && graphics_is_ready()) {
        kernel_draw_boot_graphics();
        gui_reset();
    }

    // Ask the firmware for the official touchscreen shared buffer when present.
    log_info("Initializing touch input...\n");
    device_init_touch();

    // Wake parked secondaries only when the compile-time SMP flag is enabled.
    if (percpu_multi_cpu_enabled()) {
        log_info("USE_MULTI_CPU enabled; waking secondary CPUs...\n");
        wake_secondary_cores();
    }
    else {
        log_info("USE_MULTI_CPU disabled; keeping the kernel on CPU0 only...\n");
    }

    /*
     * Complete the one-time userspace boot handoff before dropping into the
     * perpetual idle/scheduler loop.
     */
     // #if defined(ROS_BOARD_VIRT)
     //     kernel_start_virt_console();
     // #else
    kernel_start_initial_userspace();
    // kernel_start_virt_console();
    // #endif

    // init scheduler
    init_schedler();

    log_info("Entering main scheduler loop...\n");
    while (1) {
        //  kinfo("Printing from thread %s. Yeilding...", current_task->name);

        // Poll touch firmware state and refresh the simple marker overlay when input changes.
        device_poll_touch();
        input_poll();

        // Reclaim resources from tasks that exited on a previous schedule cycle.
        cleanup_zombie_processes();
        module_run_idle_loops();

        // Yield to the scheduler so runnable kernel or user tasks can execute.
        schedler_schedule();

        // delay(1000000000);
    }
}
