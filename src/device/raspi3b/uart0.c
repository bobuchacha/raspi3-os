//
// Created by Thang Cao on 5/18/24.
//

#include "device/raspi3b.h"
#include "utils.h"
#define mbox mailbox_buffer

void uart0_init(){
    register unsigned int r;

    /* initialize UART */
    *UART0_CR = 0;         // turn off UART0

    /* set up clock for consistent divisor values */
    mbox[0] = 9*4;          // message size
    mbox[1] = MBOX_REQUEST;
    mbox[2] = MBOX_TAG_SETCLKRATE; // set clock rate
    mbox[3] = 12;
    mbox[4] = 8;
    mbox[5] = 2;           // UART clock
    mbox[6] = 4000000;     // 4Mhz
    mbox[7] = 0;           // clear turbo
    mbox[8] = MBOX_TAG_LAST;

    mailbox_call(mbox, MBOX_CH_PROP);

    /* map UART0 to GPIO pins */
    r=get32(GPFSEL1);
    r&=~((7<<12)|(7<<15)); // gpio14, gpio15
    r|=(4<<12)|(4<<15);    // alt0
    put32(GPFSEL1,r);
    put32(GPPUD, 0);            // enable pins 14 and 15
    r=150; while(r--) { asm volatile("nop"); }
    put32(GPPUDCLK0, (1<<14)|(1<<15));
    r=150; while(r--) { asm volatile("nop"); }
    put32(GPPUDCLK0, 0);        // flush GPIO setup

    // enable UART0
    *(int*)UART0_ICR = 0x7FF;    // clear interrupts
    *(int*)UART0_IBRD = 2;       // 115200 baud
    *(int*)UART0_FBRD = 0xB;
    *(int*)UART0_LCRH = 0x7<<4;  // 8n1, enable FIFOs
    *(int*)UART0_CR = 0x301;     // enable Tx, Rx, UART
}

/**
 * Send a character
 */
void uart0_send(unsigned int c) {
    /* wait until we can send */
    do{asm volatile("nop");}while(*UART0_FR&0x20);
    /* write the character to the buffer */
    *UART0_DR=c;
}

void uart0_putc(void *p, char c){
    uart0_send((int)c);
}
/**
 * Receive a character
 */
char uart0_getc() {
    char r;
    /* wait until something is in the buffer */
    do{asm volatile("nop");}while(*UART0_FR&0x10);
    /* read it and return */
    r=(char)(*UART0_DR);
    /* convert carrige return to newline */
    return r=='\r'?'\n':r;
}


/**
 * Display a string
 */
void uart0_puts(char *s) {
    while(*s) {
        /* convert newline to carrige return + newline */
        if(*s=='\n')
            uart0_send('\r');
        uart0_send(*s++);
    }
}

void uart0_hex(unsigned int d) {
    unsigned int n;
    int c;
    for(c=28;c>=0;c-=4) {
        // get highest tetrad
        n=(d>>c)&0xF;
        // 0-9 => '0'-'9', 10-15 => 'A'-'F'
        n+=n>9?0x37:0x30;
        uart0_send(n);
    }
}

void uart0_lhex(unsigned long d) {
    unsigned int n;
    int c;
    for(c=60;c>=0;c-=4) {
        // get highest tetrad
        n=(d>>c)&0xF;
        // 0-9 => '0'-'9', 10-15 => 'A'-'F'
        n+=n>9?0x37:0x30;
        uart0_send(n);
    }
}
