//
// Created by Thang Cao on 5/18/24.
//

#ifndef RASPI3_OS_UART0_H
#define RASPI3_OS_UART0_H
#include "peripherals/base.h"

/* PL011 UART registers */
#define UART0_DR        ((volatile unsigned int*)(PBASE+0x00201000))
#define UART0_FR        ((volatile unsigned int*)(PBASE+0x00201018))
#define UART0_IBRD      ((volatile unsigned int*)(PBASE+0x00201024))
#define UART0_FBRD      ((volatile unsigned int*)(PBASE+0x00201028))
#define UART0_LCRH      ((volatile unsigned int*)(PBASE+0x0020102C))
#define UART0_CR        ((volatile unsigned int*)(PBASE+0x00201030))
#define UART0_IMSC      ((volatile unsigned int*)(PBASE+0x00201038))
#define UART0_ICR       ((volatile unsigned int*)(PBASE+0x00201044))


void uart0_init();
void uart0_send(unsigned int c);
char uart0_getc();
void uart0_puts(char *s);
void uart0_putc(void *p, char c);

#endif //RASPI3_OS_UART0_H
