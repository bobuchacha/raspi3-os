#ifndef ROS_LOADER_VIRT_UART0_H
#define ROS_LOADER_VIRT_UART0_H

#include "base.h"

#define UART0_DR ((volatile unsigned int*)(PBASE + 0x01000000U))
#define UART0_FR ((volatile unsigned int*)(PBASE + 0x01000018U))
#define UART0_IBRD ((volatile unsigned int*)(PBASE + 0x01000024U))
#define UART0_FBRD ((volatile unsigned int*)(PBASE + 0x01000028U))
#define UART0_LCRH ((volatile unsigned int*)(PBASE + 0x0100002CU))
#define UART0_CR ((volatile unsigned int*)(PBASE + 0x01000030U))
#define UART0_IMSC ((volatile unsigned int*)(PBASE + 0x01000038U))
#define UART0_ICR ((volatile unsigned int*)(PBASE + 0x01000044U))

void uart0_init(void);
void uart0_send(unsigned int c);
char uart0_getc(void);
void uart0_puts(char* s);
void uart0_putc(void* p, char c);
void uart0_lhex(unsigned long d);

#endif