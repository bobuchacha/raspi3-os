#ifndef RASPI3_OS_INPUT_H
#define RASPI3_OS_INPUT_H

#include "ros.h"

#define INPUT_QUEUE_CAPACITY 128U

void input_init(void);
void input_poll(void);
int input_enqueue_key(unsigned long key);
int input_try_dequeue_key(char* out_char);
long input_read_key(void);

#endif