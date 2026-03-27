#include "app/dll_import.h"
#include "app/demo-shared.h"
#include "app/kernel.hpp"
#include "logger.h"
#include "stdio.h"

#define DEMO_SHARED_PATH "/lib/demo.dll"

DLL_IMPORT_DECL(long, demo_shared_add, (long lhs, long rhs), (lhs, rhs), DEMO_SHARED_PATH, "demo_shared_add")
DLL_IMPORT_DECL_VOID(demo_shared_print_shared_counter, (const char* program_name), (program_name), DEMO_SHARED_PATH, "demo_shared_print_shared_counter")
DLL_IMPORT_DECL_VOID(demo_shared_print_local_counter, (const char* program_name), (program_name), DEMO_SHARED_PATH, "demo_shared_print_local_counter")

#define SAYHELLO_DELAY_MSEC 1000UL
#define SAYHELLO_MAX_MESSAGES 3UL

/**
 * Small demo class that owns the hello-world counter loop.
 *
 * The class keeps the example stateful so the userspace C++ runtime, object
 * construction, and method dispatch all get exercised during boot testing.
 */
    class Greeter {
    public:
        Greeter(const char* name, unsigned long start)
            : name(name), counter(start) {
        }

        /**
         * Print the greeting message repeatedly with a kernel-backed sleep between iterations.
         *
         * Args:
         *   None.
         *
         * Returns:
         *   Nothing. The loop stops after the current debug limit is reached.
         */
        void run() {
            // Emit a bounded number of messages so runtime testing finishes deterministically.
            while (counter < SAYHELLO_MAX_MESSAGES) {
                // Format the next greeting line into a stack buffer owned by this iteration.
                char buffer[128];

                // Expand the counter into printable text before crossing into the kernel log path.
                snprintf(buffer, sizeof(buffer), "[%s]: Hello [%lu]\n", name, counter++);

                // Write the fully formatted message through the syscall wrapper.
                user::kernel::write(buffer);

                // Ask the shared library to print its own shared counter value.
                demo_shared_print_shared_counter(name);
                demo_shared_print_local_counter(name);

                // Sleep in the kernel so the task yields instead of burning CPU in a busy loop.
                user::kernel::sleep(SAYHELLO_DELAY_MSEC);
            }
        }

    private:
        const char* name;
        unsigned long counter;
};

/**
 * Entry point for the `sayhello` user program.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   `0` after the demo loop completes.
 */
long main(void) {
    char name[32];
    char launch_args[128];

    if (user::kernel::get_name(name, sizeof(name)) < 0) {
        name[0] = 'u';
        name[1] = 's';
        name[2] = 'e';
        name[3] = 'r';
        name[4] = '\0';
    }

    if (user::kernel::get_args(launch_args, sizeof(launch_args)) > 0) {
        app_log_info("sayhello", "launch args: %s", launch_args);
    }

    if (demo_shared_add(40, 2) != 42) {
        app_log_warn("sayhello", "shared library unavailable");
    }

    // Construct the example object with the assigned task name and initial counter of zero.
    Greeter greeter(name, 0);

    // Run the greeting loop until the current debug bound is reached.
    greeter.run();
    return 0;
}