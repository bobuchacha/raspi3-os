#include "arch/cortex-a53/boot/bootcode.h"

#define MMIO_BASE 0x3F000000

#define GPFSEL1 ((volatile unsigned int*)(MMIO_BASE + 0x00200004))
#define GPPUD ((volatile unsigned int*)(MMIO_BASE + 0x00200094))
#define GPPUDCLK0 ((volatile unsigned int*)(MMIO_BASE + 0x00200098))

#define AUX_ENABLE ((volatile unsigned int*)(MMIO_BASE + 0x00215004))
#define AUX_MU_IO ((volatile unsigned int*)(MMIO_BASE + 0x00215040))
#define AUX_MU_IER ((volatile unsigned int*)(MMIO_BASE + 0x00215044))
#define AUX_MU_IIR ((volatile unsigned int*)(MMIO_BASE + 0x00215048))
#define AUX_MU_LCR ((volatile unsigned int*)(MMIO_BASE + 0x0021504C))
#define AUX_MU_MCR ((volatile unsigned int*)(MMIO_BASE + 0x00215050))
#define AUX_MU_LSR ((volatile unsigned int*)(MMIO_BASE + 0x00215054))
#define AUX_MU_CNTL ((volatile unsigned int*)(MMIO_BASE + 0x00215060))
#define AUX_MU_BAUD ((volatile unsigned int*)(MMIO_BASE + 0x00215068))

BOOTFUNC
void uart1_init(void) {
    unsigned int r;

    *AUX_ENABLE |= 1;
    *AUX_MU_CNTL = 0;
    *AUX_MU_LCR = 3;
    *AUX_MU_MCR = 0;
    *AUX_MU_IER = 0;
    *AUX_MU_IIR = 0xc6;
    *AUX_MU_BAUD = 270;
    r = *GPFSEL1;
    r &= ~((7 << 12) | (7 << 15));
    r |= (2 << 12) | (2 << 15);
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
    *AUX_MU_CNTL = 3;
}

BOOTFUNC
void uart1_send(unsigned int c) {
    do {
        asm volatile("nop");
    } while (!(*AUX_MU_LSR & 0x20));
    *AUX_MU_IO = c;
}

BOOTFUNC
char uart1_getc(void) {
    char r;

    do {
        asm volatile("nop");
    } while (!(*AUX_MU_LSR & 0x01));
    r = (char)(*AUX_MU_IO);
    return r == '\r' ? '\n' : r;
}

BOOTFUNC
void uart1_puts(char* s) {
    while (*s) {
        if (*s == '\n') {
            uart1_send('\r');
        }
        uart1_send(*s++);
    }
}

BOOTFUNC
void uart1_hex(unsigned int d) {
    unsigned int n;
    int c;

    for (c = 28; c >= 0; c -= 4) {
        n = (d >> c) & 0xF;
        n += n > 9 ? 0x37 : 0x30;
        uart1_send(n);
    }
}
