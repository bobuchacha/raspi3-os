//
// Created by Thang Cao on 6/15/24.
//

#ifndef RASPI3_OS_DEFS_H
#define RASPI3_OS_DEFS_H


// uart1.c
void uart1_init();
void uart1_send(unsigned int);
char uart1_getc();
void uart1_puts(char *);
void uart1_hex(unsigned int);


#endif //RASPI3_OS_DEFS_H
