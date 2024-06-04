
//
// Created by Thang Cao on 5/23/24.
//
#include "arch/cortex-a53/boot/uart1.h"
#include "printf.h"
#include "device.h"
#include "irq.h"            // some code in device irq or arch irq
#include "log.h"
#include "task.h"
#include "timer.h"

extern long get_sp();
extern void pushl(long);
extern long popl();
extern void push_pair(long, long);
extern long pop_pair();

void test_push_pop(){
    kdebug("Current SP is 0x%lX", get_sp());
    pushl(0x1234000056780000);
    kdebug("SP after pushl 0x1234000056780000 is 0x%lX", get_sp());
    pushl(0xDEADCABD12345678);
    kdebug("SP after pushl 0xDEADCABD12345678 is 0x%lX", get_sp());
    long i = popl();
    kdebug("SP after popl is 0x%lX, value of the pop is 0x%lX", get_sp(), i);
    i = popl();
    kdebug("SP after popl is 0x%lX, value of the pop is 0x%lX", get_sp(), i);

    printf("-------- PAIR --------- \n");
    push_pair(0x1234123412341234, 0x5678567856785678);
    kdebug("SP after push_pair is 0x%lX", get_sp());
    i = pop_pair();
    long x1;
    asm volatile ("mov %0, x1" : "=r"(x1) ::);
    kdebug("SP after pop_pair is 0x%lX, value of the pop is 0x%lX and 0x%lX", get_sp(), i, x1);

    asm volatile("brk #0");

    printf("OK");
}