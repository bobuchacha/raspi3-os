#include "arch/cortex-a53/boot/bootcode.h"

#define VIRT_UART1_BASE 0x09000000UL

#define VIRT_UART1_DR ((volatile unsigned int*)(VIRT_UART1_BASE + 0x000U))
#define VIRT_UART1_FR ((volatile unsigned int*)(VIRT_UART1_BASE + 0x018U))
#define VIRT_UART1_IBRD ((volatile unsigned int*)(VIRT_UART1_BASE + 0x024U))
#define VIRT_UART1_FBRD ((volatile unsigned int*)(VIRT_UART1_BASE + 0x028U))
#define VIRT_UART1_LCRH ((volatile unsigned int*)(VIRT_UART1_BASE + 0x02CU))
#define VIRT_UART1_CR ((volatile unsigned int*)(VIRT_UART1_BASE + 0x030U))
#define VIRT_UART1_IMSC ((volatile unsigned int*)(VIRT_UART1_BASE + 0x038U))
#define VIRT_UART1_ICR ((volatile unsigned int*)(VIRT_UART1_BASE + 0x044U))

BOOTFUNC
void uart1_init(void) {
    *VIRT_UART1_CR = 0U;
    *VIRT_UART1_ICR = 0x7FFU;
    *VIRT_UART1_IBRD = 13U;
    *VIRT_UART1_FBRD = 1U;
    *VIRT_UART1_LCRH = 0x70U;
    *VIRT_UART1_IMSC = 0U;
    *VIRT_UART1_CR = 0x301U;
}

BOOTFUNC
void uart1_send(unsigned int c) {
    do {
        asm volatile("nop");
    } while ((*VIRT_UART1_FR & 0x20U) != 0U);
    *VIRT_UART1_DR = c;
}

BOOTFUNC
char uart1_getc(void) {
    char value;

    do {
        asm volatile("nop");
    } while ((*VIRT_UART1_FR & 0x10U) != 0U);

    value = (char)(*VIRT_UART1_DR);
    return value == '\r' ? '\n' : value;
}

BOOTFUNC
void uart1_puts(char* s) {
    while (*s) {
        if (*s == '\n') {
            uart1_send('\r');
        }
        uart1_send((unsigned int)*s++);
    }
}

BOOTFUNC
void uart1_hex(unsigned int d) {
    unsigned int n;
    int c;

    for (c = 28; c >= 0; c -= 4) {
        n = (d >> c) & 0xFU;
        n += n > 9U ? 0x37U : 0x30U;
        uart1_send(n);
    }
}