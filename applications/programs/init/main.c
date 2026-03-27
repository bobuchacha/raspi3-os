#include "app/kernel.h"
#include "logger.h"

/**
 * Initial user program that hands control to the next real application.
 *
 * This process exists as a simple launcher so the kernel only needs to know one
 * boot entry path. It replaces itself with the interactive shell.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   `0` when `exec` succeeds and never returns, or `1` if the exec request fails.
 */
long main(void)
{
    app_log_info("init", "starting /bin/shell.exe");
    if (user_kernel_exec("/bin/shell.exe") != 0)
    {
        app_log_error("init", "shell exec failed");
        return 1;
    }

    return 0;
}