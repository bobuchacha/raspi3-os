#include "arch.h"
#include "platform.h"
#include "scheduler.h"

extern "C" void aarch64_handle_irq(void) {
    // The active kernel still uses the architected timer as the only always-on IRQ source.
    // Poll board-local input before the scheduler tick so fresh keyboard and pointer packets
    // reach GWES with the lowest latency this polling design can provide.
    Arch::rearm_periodic_timer();
    board::Platform::poll_devices();
    Scheduler::timer_tick();
}