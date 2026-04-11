#include "device/raspi3b.h"

#define mbox mailbox_buffer

void uart0_init(void) {
    unsigned int r;

    *UART0_CR = 0;

    mbox[0] = 9 * 4;
    mbox[1] = MBOX_REQUEST;
    mbox[2] = MBOX_TAG_SETCLKRATE;
    mbox[3] = 12;
    mbox[4] = 8;
    mbox[5] = 2;
    mbox[6] = 4000000;
    mbox[7] = 0;
    mbox[8] = MBOX_TAG_LAST;

    mailbox_call((UInt*)mbox, MBOX_CH_PROP);

    r = *GPFSEL1;
    r &= ~((7 << 12) | (7 << 15));
    r |= (4 << 12) | (4 << 15);
    *GPFSEL1 = r;
    *GPPUD = 0;
    r = 150;
    while (r--) {
        asm volatile("nop");
    }
    *GPPUDCLK0 = (1 << 14) | (1 << 15);
    r = 150;
    while (r--) {
        asm volatile("nop");
    }
    *GPPUDCLK0 = 0;

    *UART0_ICR = 0x7FF;
    *UART0_IBRD = 2;
    *UART0_FBRD = 0xB;
    *UART0_LCRH = 0x7 << 4;
    *UART0_CR = 0x301;
}

void uart0_send(unsigned int c) {
    do {
        asm volatile("nop");
    } while (*UART0_FR & 0x20);
    *UART0_DR = c;
}

void uart0_putc(void* p, char c) {
    (void)p;
    uart0_send((unsigned int)c);
}

char uart0_getc(void) {
    char r;

    do {
        asm volatile("nop");
    } while (*UART0_FR & 0x10);
    r = (char)(*UART0_DR);
    return r == '\r' ? '\n' : r;
}

void uart0_puts(char* s) {
    while (*s) {
        if (*s == '\n') {
            uart0_send('\r');
        }
        uart0_send(*s++);
    }
}

void uart0_lhex(unsigned long d) {
    unsigned int n;
    int c;

    for (c = 60; c >= 0; c -= 4) {
        n = (unsigned int)((d >> c) & 0xF);
        n += n > 9 ? 0x57 : 0x30;
        uart0_send(n);
    }
}
