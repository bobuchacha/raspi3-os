#include "gwes_workers.h"

#include "render.h"

namespace {

    inline constexpr unsigned long GwesWindowThreadMaxIdleSleepMsec = 4UL;

} // namespace

/*
 * Submit any pending compositor damage through the dedicated paint-scheduler file.
 *
 * Keeping the present call in its own file makes the split requested by the
 * architecture document explicit: the window thread owns state mutation, while
 * the paint scheduler owns the policy for when accumulated damage is flushed.
 *
 * @return Nothing.
 */
void gwes_window_thread_present_if_needed(void) {
    gwes_render_present_if_needed();
}

/*
 * Compute one bounded idle sleep for the window/render owner thread.
 *
 * The current EL0 runtime does not yet expose a wait object that compositor
 * damage can signal, so the scheduler cannot safely sleep until the next timer
 * fire. Instead it sleeps only for the smaller of the next timer deadline and a
 * short latency cap, while still returning immediately when damage is waiting.
 *
 * @param now Current uptime in milliseconds.
 * @return Milliseconds the caller may sleep before polling again.
 */
unsigned long gwes_window_thread_idle_wait_budget(unsigned long now) {
    unsigned long timer_wait_msec;

    if (gwes_render_has_pending_damage()) {
        return 0UL;
    }

    timer_wait_msec = gwes_window_thread_timer_wait_budget(now);
    if (timer_wait_msec == 0UL) {
        return 0UL;
    }
    if (timer_wait_msec > GwesWindowThreadMaxIdleSleepMsec) {
        return GwesWindowThreadMaxIdleSleepMsec;
    }

    return timer_wait_msec;
}