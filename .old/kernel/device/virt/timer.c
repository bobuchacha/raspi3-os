#include "timer.h"

static unsigned long timer_counter_frequency(void) {
    unsigned long frequency;

    asm volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    return frequency;
}

void timer_init(void) {
}

void handle_timer_irq(void) {
}

void wait_cycles(unsigned int n) {
    while (n-- > 0U) {
        asm volatile("nop");
    }
}

void wait_msec(unsigned int n) {
    unsigned long start;
    unsigned long current;
    unsigned long delta;
    unsigned long frequency = timer_counter_frequency();
    unsigned long ticks = (frequency * (unsigned long)n) / 1000UL;

    asm volatile("mrs %0, cntpct_el0" : "=r"(start));
    do {
        asm volatile("mrs %0, cntpct_el0" : "=r"(current));
        delta = current - start;
    } while (delta < ticks);
}

unsigned long get_system_timer(void) {
    unsigned long counter;
    unsigned long frequency = timer_counter_frequency();

    asm volatile("mrs %0, cntpct_el0" : "=r"(counter));
    return frequency ? (counter * 1000000UL) / frequency : 0UL;
}

void wait_msec_st(unsigned int n) {
    unsigned long start = get_system_timer();

    while ((get_system_timer() - start) < (unsigned long)n) {
    }
}