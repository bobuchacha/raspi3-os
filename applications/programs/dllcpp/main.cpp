#include "app/dll_import.h"
#include "app/demo-shared.h"
#include "app/kernel.hpp"
#include "logger.h"
#include "stdio.h"

#define DEMO_SHARED_PATH "/lib/demo.dll"

DLL_IMPORT_DECL(long, demo_shared_add, (long lhs, long rhs), (lhs, rhs), DEMO_SHARED_PATH, "demo_shared_add")
DLL_IMPORT_DECL_VOID(demo_shared_print_shared_counter, (const char* program_name), (program_name), DEMO_SHARED_PATH, "demo_shared_print_shared_counter")
DLL_IMPORT_DECL_VOID(demo_shared_print_local_counter, (const char* program_name), (program_name), DEMO_SHARED_PATH, "demo_shared_print_local_counter")

/**
 * Small C++ wrapper that consumes the C API exported by the shared library.
 */
    class SharedClient {
    public:
        SharedClient() {
        }

        /**
         * Exercise the shared library from C++ code.
         *
         * Args:
         *   None.
         *
         * Returns:
         *   Nothing. Results are logged through the kernel console.
         */
        void run() const {
            char buffer[128];

            // Call into the shared-library code path from a C++ object method.
            snprintf(buffer, sizeof(buffer), "dllcpp: add(100, 23) = %ld\n", demo_shared_add(100, 23));
            user::kernel::write(buffer);
            demo_shared_print_shared_counter("dllcpp");
            demo_shared_print_local_counter("dllcpp");
        }
};

/**
 * C++ demo application that opens the shared library and exercises its exported API.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   `0` on success, or `1` when the library cannot be opened or has the wrong ABI version.
 */
long main(void) {
    if (demo_shared_add(100, 23) != 123) {
        app_log_error("dllcpp", "failed to resolve demo exports");
        return 1;
    }

    SharedClient client;
    client.run();
    return 0;
}
