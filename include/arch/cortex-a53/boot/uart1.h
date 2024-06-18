#ifndef _BOOT_UART1_H_
#define _BOOT_UART1_H_

void uart1_init();
void uart1_send(unsigned int);
char uart1_getc();
void uart1_puts(char *);
void uart1_hex(unsigned int);

#endif