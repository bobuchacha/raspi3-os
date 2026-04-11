#include "console.h"

#if defined(BOARD_VIRT)


#define UART0_BASE 0x09000000UL                                     // UART BASE
#define UART0_DR ((volatile U32*)(UART0_BASE + 0x000U))             
#define UART0_FR ((volatile U32*)(UART0_BASE + 0x018U))
#define UART0_IBRD ((volatile U32*)(UART0_BASE + 0x024U))
#define UART0_FBRD ((volatile U32*)(UART0_BASE + 0x028U))
#define UART0_LCRH ((volatile U32*)(UART0_BASE + 0x02CU))
#define UART0_CR ((volatile U32*)(UART0_BASE + 0x030U))
#define UART0_IMSC ((volatile U32*)(UART0_BASE + 0x038U))
#define UART0_ICR ((volatile U32*)(UART0_BASE + 0x044U))

Status boot_console_init(void) {
    *UART0_CR = 0U;
    *UART0_ICR = 0x7FFU;
    *UART0_IBRD = 13U;
    *UART0_FBRD = 1U;
    *UART0_LCRH = 0x70U;
    *UART0_IMSC = 0U;
    *UART0_CR = 0x301U;
    return StatusOK;
}

void boot_console_putc(char ch) {
    if (ch == '\n') {
        boot_console_putc('\r');
    }

    while ((*UART0_FR & 0x20U) != 0U) {
        __asm__ volatile("nop");
    }

    *UART0_DR = (U32)(U8)ch;
}

void boot_console_puts(const char* text) {
    while ((text != NULL) && (*text != '\0')) {
        boot_console_putc(*text++);
    }
}

char boot_console_getc(void) {
    U8 value;

    while ((*UART0_FR & 0x10U) != 0U) {
        __asm__ volatile("nop");
    }

    value = (U8)(*UART0_DR & 0xffU);
    return (value == '\r') ? '\n' : (char)value;
}

#else

Status boot_console_init(void) {
    return StatusNotSupported;
}

void boot_console_putc(char ch) {
    (void)ch;
}

void boot_console_puts(const char* text) {
    (void)text;
}

char boot_console_getc(void) {
    return '\0';
}

#endif