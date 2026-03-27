#include "app/demo-shared.h"
#include "app/kernel.h"
#include "logger.h"
#include "stdio.h"

#define SHARED_LIBRARY_ENTRY __attribute__((section(".entrypoint")))

/**
 * Add two integers inside the shared library so multiple applications can reuse the same code page.
 *
 * Args:
 *   lhs: Left-hand operand.
 *   rhs: Right-hand operand.
 *
 * Returns:
 *   Sum of `lhs` and `rhs`.
 */
long demo_shared_add(long lhs, long rhs) {
    return lhs + rhs;
}

static unsigned long demo_shared_counter;

typedef struct {
    unsigned long local_counter;
} DemoSharedLocalState;

/**
 * Resolve the current task's private storage block owned by this DLL.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Pointer to the zero-initialized per-task local state, or `0` on allocation failure.
 */
static DemoSharedLocalState* demo_shared_get_local_state(void) {
    return (DemoSharedLocalState*)user_kernel_shared_library_local(DEMO_SHARED_PATH, sizeof(DemoSharedLocalState));
}

/**
 * Format and print the DLL-owned shared counter through the kernel log.
 *
 * Args:
 *   program_name: Name of the caller for logging.
 *
 * Returns:
 *   Nothing. The formatted string is written through the syscall wrapper.
 */
void demo_shared_print_shared_counter(const char* program_name) {
    char buffer[128];
    const char* name = program_name ? program_name : "shared";

    // Increment the shared DLL counter so all clients observe one combined sequence.
    snprintf(buffer, sizeof(buffer), "[%s][dll]: Counter [%lu]\n", name, demo_shared_counter++);
    user_kernel_write(buffer);
}

/**
 * Format and print the task-private DLL-local counter through the kernel log.
 *
 * Args:
 *   program_name: Name of the caller for logging.
 *
 * Returns:
 *   Nothing. Allocation failures are logged through the kernel console.
 */
void demo_shared_print_local_counter(const char* program_name) {
    DemoSharedLocalState* local_state = demo_shared_get_local_state();
    char buffer[128];
    const char* name = program_name ? program_name : "shared";

    if (!local_state) {
        app_log_error("demo.dll", "local-state allocation failed");
        return;
    }

    snprintf(buffer, sizeof(buffer), "[%s][dll-local]: Counter [%lu]\n", name, local_state->local_counter++);
    user_kernel_write(buffer);
}

static const DemoSharedApi demo_shared_api = {
    DEMO_SHARED_ABI_VERSION,
    demo_shared_add,
    demo_shared_print_shared_counter,
    demo_shared_print_local_counter,
};

/**
 * Entry point returned by `user_kernel_open_shared_library`.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Pointer to the exported API table implemented by this shared library.
 */
SHARED_LIBRARY_ENTRY const DemoSharedApi* _shared_library_entry(void) {
    return &demo_shared_api;
}
