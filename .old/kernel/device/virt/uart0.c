#include "device/virt.h"
#include "arch/cortex-a53/dbg.h"

#define VIRT_UART0_DR ((volatile unsigned int*)(VA_START + VIRT_MMIO_BASE + 0x000U))
#define VIRT_UART0_FR ((volatile unsigned int*)(VA_START + VIRT_MMIO_BASE + 0x018U))
#define VIRT_UART0_IBRD ((volatile unsigned int*)(VA_START + VIRT_MMIO_BASE + 0x024U))
#define VIRT_UART0_FBRD ((volatile unsigned int*)(VA_START + VIRT_MMIO_BASE + 0x028U))
#define VIRT_UART0_LCRH ((volatile unsigned int*)(VA_START + VIRT_MMIO_BASE + 0x02CU))
#define VIRT_UART0_CR ((volatile unsigned int*)(VA_START + VIRT_MMIO_BASE + 0x030U))
#define VIRT_UART0_IMSC ((volatile unsigned int*)(VA_START + VIRT_MMIO_BASE + 0x038U))
#define VIRT_UART0_ICR ((volatile unsigned int*)(VA_START + VIRT_MMIO_BASE + 0x044U))

void uart0_init(void) {
    *VIRT_UART0_CR = 0U;
    *VIRT_UART0_ICR = 0x7FFU;
    *VIRT_UART0_IBRD = 13U;
    *VIRT_UART0_FBRD = 1U;
    *VIRT_UART0_LCRH = 0x70U;
    *VIRT_UART0_IMSC = 0U;
    *VIRT_UART0_CR = 0x301U;
}

void uart0_send(unsigned int c) {
    do {
        asm volatile("nop");
    } while ((*VIRT_UART0_FR & 0x20U) != 0U);
    *VIRT_UART0_DR = c;
}

void uart0_putc(void* p, char c) {
    (void)p;
    uart0_send((unsigned int)c);
}

char uart0_getc(void) {
    char value;

    do {
        dbg_wait_if_paused();
        asm volatile("nop");
    } while ((*VIRT_UART0_FR & 0x10U) != 0U);

    value = (char)(*VIRT_UART0_DR);
    return value == '\r' ? '\n' : value;
}

int uart0_try_getc(char* out_char) {
    char value;

    if (out_char == 0 || (*VIRT_UART0_FR & 0x10U) != 0U) {
        return -1;
    }

    value = (char)(*VIRT_UART0_DR);
    *out_char = value == '\r' ? '\n' : value;
    return 0;
}

void uart0_puts(char* s) {
    while (s && *s) {
        if (*s == '\n') {
            uart0_send('\r');
        }
        uart0_send((unsigned int)*s++);
    }
}

void uart0_lhex(unsigned long d) {
    unsigned int n;
    int c;

    for (c = 60; c >= 0; c -= 4) {
        n = (unsigned int)((d >> c) & 0xFU);
        n += n > 9U ? 0x57U : 0x30U;
        uart0_send(n);
    }
}