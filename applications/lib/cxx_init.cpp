#include "app/kernel.h"

typedef void (*InitFunc)(void);

extern "C"
{

    extern InitFunc __init_array_start[];
    extern InitFunc __init_array_end[];

    /**
     * Invoke every constructor emitted into the user ELF `.init_array` section.
     *
     * Args:
     *   None.
     *
     * Returns:
     *   Nothing. Registered constructors run in link order.
     */
    void __user_run_init_array(void) {
        // Walk the linker-defined constructor range and run each non-null entry exactly once.
        for (InitFunc* func = __init_array_start; func < __init_array_end; ++func) {
            if (*func) {
                (*func)();
            }
        }
    }

}