#include "arch.h"
#include "arch/aarch64/exception_frame.h"
#include "debug-message.h"
#include "platform.h"
#include "scheduler.h"

namespace {

    /**
     * Emit one serial proof that the raw lower-EL IRQ entry path is live.
     *
     * This trace deliberately fires before any scheduler decision so it can
     * distinguish "the timer IRQ reached EL1" from "that IRQ later forced a
     * reschedule". Logging only once keeps the entry path narrow enough for
     * runtime proof without turning every tick into scheduler noise.
     *
     * @param frame Saved architectural exception frame captured by the vector.
     * @return Nothing.
     */
    void trace_lower_el_irq_entry_window(const AArch64ExceptionFrame* frame, U32 interrupt_id) {
        static U32 emitted_count = 0U;

        if ((frame == NULL) || (emitted_count >= 16U)) {
            return;
        }

        ++emitted_count;
        KRETAIL(
            "[sched-irq] irq entry count=%u vector=%u id=%u elr=0x%llx spsr=0x%llx esr=0x%llx\n",
            emitted_count,
            static_cast<unsigned int>(frame->vector_id),
            static_cast<unsigned int>(interrupt_id),
            static_cast<unsigned long long>(frame->elr),
            static_cast<unsigned long long>(frame->spsr),
            static_cast<unsigned long long>(frame->esr));
    }

} // namespace

extern "C" U64 aarch64_handle_irq(AArch64ExceptionFrame* frame) {
    U32 acknowledge_value;
    U32 interrupt_id;

    if (!board::Platform::begin_irq(&interrupt_id, &acknowledge_value)) {
        return static_cast<U64>(SchedulerIrqAction::ResumeCurrent);
    }

    // The active kernel still uses the architected timer as the only always-on IRQ source.
    // Poll board-local input before the scheduler tick so fresh keyboard and pointer packets
    // reach GWES with the lowest latency this polling design can provide.
    trace_lower_el_irq_entry_window(frame, interrupt_id);
    Arch::rearm_periodic_timer();
    board::Platform::poll_devices();

    if (board::Platform::is_timer_irq(interrupt_id)) {
        U64 action;

        // IRQ-time preemption can switch away from this kernel call chain and
        // never return to the old stack frame. Complete the timer interrupt
        // after rearming its compare value but before handing control to the
        // scheduler, otherwise the first preemptive switch can leave the PPI
        // permanently active and block every later tick.
        board::Platform::end_irq(acknowledge_value);
        if ((frame != NULL) && (frame->vector_id == AArch64ExceptionVectorLowerElA64Irq)) {
            action = static_cast<U64>(Scheduler::handle_lower_el_timer_irq(frame));
        }
        else {
            action = static_cast<U64>(Scheduler::handle_current_el_timer_irq());
        }
        return action;
    }

    board::Platform::end_irq(acknowledge_value);
    return static_cast<U64>(SchedulerIrqAction::ResumeCurrent);
}