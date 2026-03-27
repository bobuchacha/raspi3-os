#include "app/dll_import.h"
#include "app/demo-shared.h"
#include "app/kernel.h"
#include "logger.h"
#include "stdio.h"

#define DEMO_SHARED_PATH "/lib/demo.dll"

DLL_IMPORT_DECL(long, demo_shared_add, (long lhs, long rhs), (lhs, rhs), DEMO_SHARED_PATH, "demo_shared_add")
DLL_IMPORT_DECL_VOID(demo_shared_print_shared_counter, (const char* program_name), (program_name), DEMO_SHARED_PATH, "demo_shared_print_shared_counter")
DLL_IMPORT_DECL_VOID(demo_shared_print_local_counter, (const char* program_name), (program_name), DEMO_SHARED_PATH, "demo_shared_print_local_counter")

/**
 * C demo application that opens the shared library and uses its exported functions.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   `0` on success, or `1` when the library cannot be opened or has the wrong ABI version.
 */
    long main(void) {
    char buffer[128];

    if (demo_shared_add(7, 35) != 42) {
        app_log_error("dllc", "failed to open /lib/demo.dll");
        return 1;
    }

    // Use one shared-library function for arithmetic and another for formatted output.
    snprintf(buffer, sizeof(buffer), "dllc: add(7, 35) = %ld\n", demo_shared_add(7, 35));
    user_kernel_write(buffer);
    demo_shared_print_shared_counter("dllc");
    demo_shared_print_local_counter("dllc");
    // app_log_error("API", "This is an error message from dllc");

    while (1) {
        user_kernel_sleep(5000);
        app_log_info("dllc", "tick");
        demo_shared_print_shared_counter("dllc");
        demo_shared_print_local_counter("dllc");
    }
    return 0;
}
