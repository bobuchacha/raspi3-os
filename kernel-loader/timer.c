#include "timer.h"

void wait_cycles(unsigned int n) {
    while (n-- > 0) {
        asm volatile("nop");
    }
}

void wait_msec(unsigned int n) {
    unsigned long frequency;
    unsigned long start;
    unsigned long current;
    unsigned long delta;

    asm volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    asm volatile("mrs %0, cntpct_el0" : "=r"(start));

    delta = ((frequency / 1000UL) * n) / 1000UL;
    do {
        asm volatile("mrs %0, cntpct_el0" : "=r"(current));
    } while (current - start < delta);
}
