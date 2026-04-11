#ifndef KERNEL_INCLUDE_PLATFORM_BOARD_RASPI3_SERIAL_BACKEND_H
#define KERNEL_INCLUDE_PLATFORM_BOARD_RASPI3_SERIAL_BACKEND_H

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

        static char getc(void) {
            U8 value;

            while ((*uart0_fr() & 0x10U) != 0U) {
                // The early console is a busy wait, so it must also serve the cooperative timer path.
                Scheduler::poll();
                __asm__ volatile("nop");
            }

            value = static_cast<U8>(*uart0_dr() & 0xffU);
            return (value == '\r') ? '\n' : static_cast<char>(value);
        }

    private:
        static constexpr Uptr Uart0Base = 0x3F201000UL;

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

#endif // KERNEL_INCLUDE_PLATFORM_BOARD_RASPI3_SERIAL_BACKEND_H