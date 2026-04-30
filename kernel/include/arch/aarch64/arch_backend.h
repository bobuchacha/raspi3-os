#ifndef KERNEL_INCLUDE_ARCH_AARCH64_ARCH_BACKEND_H
#define KERNEL_INCLUDE_ARCH_AARCH64_ARCH_BACKEND_H

extern "C" void aarch64_exception_vectors(void);

namespace arch {

    class Arch final {
    public:
        static Status early_init(const ArchBootInfo* boot_info) {
            (void)boot_info;

            // Install a real EL1 vector table before any subsystem enables interrupts so timer preemption
            // and future faults always land in kernel-owned code instead of the old infinite branch stub.
            disable_interrupts();
            __asm__ volatile(
                "msr vbar_el1, %0\n\t"
                "isb"
                :
            : "r"(reinterpret_cast<Uptr>(&aarch64_exception_vectors))
                : "memory");
            return StatusOK;
        }

        static void enable_interrupts(void) {
            __asm__ volatile("msr daifclr, #2" ::: "memory");
        }

        static void disable_interrupts(void) {
            __asm__ volatile("msr daifset, #2" ::: "memory");
        }

        static bool interrupts_enabled(void) {
            U64 daif;

            __asm__ volatile("mrs %0, daif" : "=r"(daif));
            return (daif & (1ULL << 7)) == 0ULL;
        }

        static bool save_and_disable_interrupts(void) {
            bool enabled = interrupts_enabled();

            disable_interrupts();
            return enabled;
        }

        static void restore_interrupts(bool enabled) {
            if (enabled) {
                enable_interrupts();
            }
        }

        static U64 counter_frequency(void) {
            U64 frequency;

            __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
            return frequency;
        }

        static U64 counter_value(void) {
            U64 counter;

#if defined(BOARD_VIRT)
            // The `virt` board currently drives the periodic IRQ from the
            // virtual EL1 timer registers, so deadline accounting must read the
            // matching virtual counter instead of the physical counter. Mixing
            // `cntpct` with `cntv_cval` can arm the first interrupt close
            // enough to fire, then push later compares onto the wrong timeline.
            __asm__ volatile("mrs %0, cntvct_el0" : "=r"(counter));
#else
            __asm__ volatile("mrs %0, cntpct_el0" : "=r"(counter));
#endif
            return counter;
        }

        static Status init_periodic_timer(U64 interval_microseconds) {
            U64 frequency;
            U64 ticks;

            if (interval_microseconds == 0ULL) {
                return StatusInvalidArgument;
            }
            if (timer_enabled_) {
                return StatusAlreadyExists;
            }

            frequency = counter_frequency();
            if (frequency == 0ULL) {
                return StatusFault;
            }

            ticks = (frequency * interval_microseconds) / 1000000ULL;
            if (ticks == 0ULL) {
                ticks = 1ULL;
            }

            timer_interval_ticks_ = ticks;
            // Keep the software deadline even while arming the architected timer IRQ source. The
            // active `virt` tree still needs the poll path as a fallback until the board-level IRQ
            // route is fully wired, so both views of time must stay in sync.
            timer_deadline_ticks_ = counter_value() + ticks;
            timer_enabled_ = true;
            rearm_periodic_timer();
            return StatusOK;
        }

        static void rearm_periodic_timer(void) {
            if (!timer_enabled_ || (timer_interval_ticks_ == 0ULL)) {
                return;
            }

            timer_deadline_ticks_ = counter_value() + timer_interval_ticks_;
            program_periodic_timer_compare(timer_deadline_ticks_);
        }

        static U32 poll_periodic_timer(void) {
            U64 elapsed_ticks;
            U64 now;

            if (!timer_enabled_ || (timer_interval_ticks_ == 0ULL)) {
                return 0U;
            }

            now = counter_value();
            if (now < timer_deadline_ticks_) {
                return 0U;
            }

            // Advance by whole quanta so long UART waits or idle spins keep scheduler time monotonic.
            elapsed_ticks = 1ULL + ((now - timer_deadline_ticks_) / timer_interval_ticks_);
            timer_deadline_ticks_ += elapsed_ticks * timer_interval_ticks_;
            // Re-arm the architected timer from the advanced software deadline too, otherwise a
            // missing board IRQ route would leave the hardware timer permanently expired after the
            // first missed interrupt and later IRQ bring-up would resume with a stale level.
            program_periodic_timer_compare(timer_deadline_ticks_);
            if (elapsed_ticks > static_cast<U64>(static_cast<U32>(-1))) {
                return static_cast<U32>(-1);
            }

            return static_cast<U32>(elapsed_ticks);
        }

        static void disable_periodic_timer(void) {
            disable_periodic_timer_compare();
            timer_enabled_ = false;
            timer_interval_ticks_ = 0ULL;
            timer_deadline_ticks_ = 0ULL;
        }

        [[noreturn]] static void idle(void) {
            for (;;) {
                __asm__ volatile("wfi");
            }
        }

        [[noreturn]] static void halt(void) {
            for (;;) {
                __asm__ volatile("wfi");
            }
        }

    private:
        /**
         * Program the EL1 physical timer compare register so the CPU can take a real timer IRQ
         * when the board interrupt route exists.
         *
         * @param compare_value Absolute architected-counter value for the next timer firing.
         * @return Nothing.
         */
        static void program_periodic_timer_compare(U64 compare_value) {
            const U64 control = 1ULL;

            // Use the absolute compare register rather than TVAL so the software deadline and the
            // hardware IRQ source share the same target tick even if execution was delayed.
#if defined(BOARD_VIRT)
            // QEMU `virt` runs this kernel in non-secure EL1 under EL2 bring-up, and the virtual
            // timer PPI is the reliable architected route exposed to that guest-style context.
            __asm__ volatile(
                "msr cntv_cval_el0, %0\n\t"
                "msr cntv_ctl_el0, %1\n\t"
                "isb"
                :
            : "r"(compare_value), "r"(control)
                : "memory");
#else
            __asm__ volatile(
                "msr cntp_cval_el0, %0\n\t"
                "msr cntp_ctl_el0, %1\n\t"
                "isb"
                :
            : "r"(compare_value), "r"(control)
                : "memory");
#endif
        }

        /**
         * Disable the EL1 physical timer compare register while tearing the timer device down.
         *
         * @return Nothing.
         */
        static void disable_periodic_timer_compare(void) {
            const U64 control = 0ULL;

#if defined(BOARD_VIRT)
            __asm__ volatile(
                "msr cntv_ctl_el0, %0\n\t"
                "isb"
                :
            : "r"(control)
                : "memory");
#else
            __asm__ volatile(
                "msr cntp_ctl_el0, %0\n\t"
                "isb"
                :
            : "r"(control)
                : "memory");
#endif
        }

        inline static U64 timer_interval_ticks_ = 0ULL;
        inline static U64 timer_deadline_ticks_ = 0ULL;
        inline static bool timer_enabled_ = false;
    };

} // namespace arch

using Arch = arch::Arch;

#endif // KERNEL_INCLUDE_ARCH_AARCH64_ARCH_BACKEND_H