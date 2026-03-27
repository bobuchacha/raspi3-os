#include "printf.h"
#include "task.h"
extern void dump_registers();

/**
 * Top-level timer interrupt hook used by the generic kernel layer.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   Nothing. The call is forwarded to the scheduler tick handler.
 */
void timer_tick()
{
    // Keep the generic timer entry tiny and let the scheduler own time-slice bookkeeping.
    schedler_timer_tick();
}