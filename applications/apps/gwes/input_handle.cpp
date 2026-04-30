#include "gwes_workers.h"

#include "app/app.h"

/*
 * Drain one shared-input batch through the dedicated input-handling file.
 *
 * GWES still owns the real shared-input state machine inside `gwes.cpp`, but
 * routing the call through this file starts the source split the redesign asks
 * for and gives the future input thread one stable entry point to own.
 *
 * @return Count of shared-input events consumed during this drain.
 */
unsigned long gwes_input_handle_drain_pending(void) {
    return gwes_consume_shared_input();
}

/*
 * Run the dedicated shared-input worker loop.
 *
 * The kernel currently exposes the shared-input ring as a memory-mapped queue
 * without a matching wait primitive, so the worker uses a short idle sleep only
 * when a drain pass found no new records. All stateful pointer and keyboard
 * handling still runs later on the window/render thread after the events are
 * copied into the explicit queue.
 *
 * @param argument Unused worker-thread argument.
 * @return Nothing.
 */
extern "C" void gwes_input_thread_entry(unsigned long argument) {
    (void)argument;

    for (;;) {
        if (gwes_input_handle_drain_pending() != 0UL) {
            continue;
        }

        (void)sleepMs(1UL);
    }
}
