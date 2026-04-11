#ifndef ROS_LOADER_UART1_H
#define ROS_LOADER_UART1_H

void uart1_init(void);
void uart1_send(unsigned int c);
char uart1_getc(void);
void uart1_puts(char* s);
void uart1_hex(unsigned int d);

#endif
