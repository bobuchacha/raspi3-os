#include "utils.h"
#include "printf.h"
#include "device/raspi3b.h"

const unsigned int _timer0_interval = 500000;
static unsigned long int _timer0_current_val = 0;

void timer_init ( void )
{
	_timer0_current_val = get32(TIMER_CLO);
	_timer0_current_val += _timer0_interval;
	put32(TIMER_C1, _timer0_current_val);
}

void handle_timer_irq( void ) 
{
	_timer0_current_val += _timer0_interval;
	put32(TIMER_C1, _timer0_current_val);
	put32(TIMER_CS, TIMER_CS_M1);
    timer_tick();

}


/**
 * Wait N CPU cycles (ARM CPU only)
 */
void wait_cycles(unsigned int n)
{
    if(n) while(n--) { asm volatile("nop"); }
}

/**
 * Wait N microsec (ARM CPU only)
 */
void wait_msec(unsigned int n)
{
    register unsigned long f, t, r;
    // get the current counter frequency
    asm volatile ("mrs %0, cntfrq_el0" : "=r"(f));
    // read the current counter
    asm volatile ("mrs %0, cntpct_el0" : "=r"(t));
    // calculate required count increase
    unsigned long i=((f/1000)*n)/1000;
    // loop while counter increase is less than i
    do{asm volatile ("mrs %0, cntpct_el0" : "=r"(r));}while(r-t<i);
}

/**
 * Get System Timer's counter
 */
unsigned long get_system_timer()
{
    register unsigned int h = -1, l;
    // we must read MMIO area as two separate 32 bit reads
    h = get32(TIMER_CHI);
    l = get32(TIMER_CLO);
    // we have to repeat it if high word changed during read
    if(h!= get32(TIMER_CHI)) {
        h=get32(TIMER_CHI);
        l=get32(TIMER_CLO);
    }
    // compose long int value
    return ((unsigned long) h << 32) | l;
}

/**
 * Wait N microsec (with BCM System Timer)
 */
void wait_msec_st(unsigned int n)
{
    unsigned long t=get_system_timer();
    // we must check if it's non-zero, because qemu does not emulate
    // system timer, and returning constant zero would mean infinite loop
    if(t) while(get_system_timer()-t < n);
}
