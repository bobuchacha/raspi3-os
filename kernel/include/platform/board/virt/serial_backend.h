#ifndef KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_SERIAL_BACKEND_H
#define KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_SERIAL_BACKEND_H

#include "scheduler.h"

namespace board {

    class Serial final {
    public:
        static Status init(void) {
            *uart0_cr() = 0U;
            *uart0_icr() = 0x7FFU;
            *uart0_ibrd() = 13U;
            *uart0_fbrd() = 1U;
            *uart0_lcrh() = 0x70U;
            *uart0_imsc() = 0U;
            *uart0_cr() = 0x301U;
            return StatusOK;
        }

        /**
         * Emit one character without scheduler cooperation.
         *
         * Exception handling cannot safely re-enter the scheduler while the UART
         * FIFO is back-pressured, so the debugger uses this raw path instead of
         * the cooperative console helpers.
         *
         * @param ch Character to transmit.
         */
        static void putc_raw(char ch) {
            if (ch == '\n') {
                putc_raw('\r');
            }

            while ((*uart0_fr() & 0x20U) != 0U) {
                __asm__ volatile("nop");
            }

            *uart0_dr() = static_cast<U32>(static_cast<U8>(ch));
        }

        /**
         * Emit a string without scheduler cooperation.
         *
         * @param text Null-terminated string to transmit.
         */
        static void puts_raw(const char* text) {
            while ((text != NULL) && (*text != '\0')) {
                putc_raw(*text++);
            }
        }

        /**
         * Receive one character without scheduler cooperation.
         *
         * @return One decoded console character.
         */
        static char getc_raw(void) {
            U8 value;

            while ((*uart0_fr() & 0x10U) != 0U) {
                __asm__ volatile("nop");
            }

            value = static_cast<U8>(*uart0_dr() & 0xffU);
            return (value == '\r') ? '\n' : static_cast<char>(value);
        }

        static void putc(char ch) {
            if (ch == '\n') {
                putc('\r');
            }

            while ((*uart0_fr() & 0x20U) != 0U) {
                // Poll the scheduler clock while the UART is back-pressured so time slices still
                // expire when the console path is the only active workload.
                Scheduler::poll();
                __asm__ volatile("nop");
            }

            *uart0_dr() = static_cast<U32>(static_cast<U8>(ch));
        }

        static void puts(const char* text) {
            while ((text != NULL) && (*text != '\0')) {
                putc(*text++);
            }
        }

        /**
         * Inject one synthesized console key into the UART-backed console path.
         *
         * The `virt` keyboard backend now receives host input through
         * virtio-input rather than the PL011 UART. The shell still reads from
         * `Serial::getc()`, so keyboard events need a small software queue that
         * the console path checks before falling back to the real UART FIFO.
         *
         * @param ch ASCII or control character to enqueue.
         * @return `true` when the character was queued, or `false` when the
         * software queue is full.
         */
        static bool inject_key(char ch) {
            bool queued = false;

            if (ch == '\r') {
                ch = '\n';
            }

            lock(&InjectedQueueLockWord);
            if (InjectedQueueCount < InjectedQueueCapacity) {
                InjectedQueue[InjectedQueueHead] = ch;
                InjectedQueueHead = (InjectedQueueHead + 1U) % InjectedQueueCapacity;
                ++InjectedQueueCount;
                queued = true;
            }
            unlock(&InjectedQueueLockWord);
            return queued;
        }

        static char getc(void) {
            U8 value;
            char injected;

            for (;;) {
                if (try_dequeue_injected_key(&injected)) {
                    return injected;
                }
                if ((*uart0_fr() & 0x10U) == 0U) {
                    value = static_cast<U8>(*uart0_dr() & 0xffU);
                    return (value == '\r') ? '\n' : static_cast<char>(value);
                }

                // The early console is a busy wait, so it must also serve the cooperative timer path.
                Scheduler::poll();
                __asm__ volatile("nop");
            }
        }

    private:
        enum : U32 {
            InjectedQueueCapacity = 128U,
        };

        /**
         * Acquire the small synthesized-input queue lock.
         *
         * @param lock_word Shared lock word protecting the queue state.
         * @return Nothing.
         */
        static void lock(volatile U32* lock_word) {
            while (!try_lock(lock_word)) {
                __asm__ volatile("yield\n" ::: "memory");
            }
        }

        /**
         * Attempt to acquire the synthesized-input queue lock.
         *
         * @param lock_word Shared lock word protecting the queue state.
         * @return `true` when the caller acquired the lock.
         */
        static bool try_lock(volatile U32* lock_word) {
            U32 previous;
            U32 status;

            __asm__ volatile(
                "ldaxr %w0, [%2]\n"
                "cbnz %w0, 1f\n"
                "mov %w0, #1\n"
                "stxr %w1, %w0, [%2]\n"
                "cbnz %w1, 2f\n"
                "mov %w0, wzr\n"
                "b 3f\n"
                "1:\n"
                "mov %w1, wzr\n"
                "2:\n"
                "3:\n"
                : "=&r"(previous), "=&r"(status)
                : "r"(lock_word)
                : "memory");

            return (previous == 0U) && (status == 0U);
        }

        /**
         * Release the synthesized-input queue lock.
         *
         * @param lock_word Shared lock word protecting the queue state.
         * @return Nothing.
         */
        static void unlock(volatile U32* lock_word) {
            __asm__ volatile(
                "stlr %w1, [%0]\n"
                :
                : "r"(lock_word), "r"(0U)
                : "memory");
        }

        /**
         * Try to remove one synthesized key from the software queue.
         *
         * @param out_ch Destination for the dequeued character.
         * @return `true` when a character was available.
         */
        static bool try_dequeue_injected_key(char* out_ch) {
            bool dequeued = false;

            if (out_ch == NULL) {
                return false;
            }

            lock(&InjectedQueueLockWord);
            if (InjectedQueueCount != 0U) {
                *out_ch = InjectedQueue[InjectedQueueTail];
                InjectedQueueTail = (InjectedQueueTail + 1U) % InjectedQueueCapacity;
                --InjectedQueueCount;
                dequeued = true;
            }
            unlock(&InjectedQueueLockWord);
            return dequeued;
        }

        static constexpr Uptr Uart0Base = 0x09000000UL;
        inline static char InjectedQueue[InjectedQueueCapacity];
        inline static U32 InjectedQueueHead;
        inline static U32 InjectedQueueTail;
        inline static U32 InjectedQueueCount;
        inline static volatile U32 InjectedQueueLockWord;

        static volatile U32* register_at(Uptr offset) {
            return reinterpret_cast<volatile U32*>(Uart0Base + offset);
        }

        static volatile U32* uart0_dr(void) {
            return register_at(0x000U);
        }

        static volatile U32* uart0_fr(void) {
            return register_at(0x018U);
        }

        static volatile U32* uart0_ibrd(void) {
            return register_at(0x024U);
        }

        static volatile U32* uart0_fbrd(void) {
            return register_at(0x028U);
        }

        static volatile U32* uart0_lcrh(void) {
            return register_at(0x02CU);
        }

        static volatile U32* uart0_cr(void) {
            return register_at(0x030U);
        }

        static volatile U32* uart0_imsc(void) {
            return register_at(0x038U);
        }

        static volatile U32* uart0_icr(void) {
            return register_at(0x044U);
        }
    };

} // namespace board

#endif // KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_SERIAL_BACKEND_H