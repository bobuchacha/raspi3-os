#include "app/kernel.h"

extern void __user_run_init_array(void);
extern long main(void);

#define ENTRYPOINT __attribute__((section(".entrypoint"))) void

/**
 * Common ELF entrypoint shared by all user programs.
 *
 * This routine runs global C/C++ constructors first, then transfers control to
 * `main`, and finally reports the program's exit code back to the kernel.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Never returns in the normal case because `call_sys_exit` terminates the task.
 */
ENTRYPOINT _entrypoint(void)
{
    // Run `.init_array` constructors before any user code touches global state.
    __user_run_init_array();

    // Exit through the kernel so the scheduler can reclaim the task cleanly.
    user_kernel_exit(main());
}