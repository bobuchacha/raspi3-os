//
// Created by Thang Cao on 6/15/24.
//
#include <ros.h>
#include "../include/arch/cortex-a53/boot/uart1.h"
#include "../include/printf.h"
#include "../include/device.h"
#include "../include/irq.h"            // some code in device irq or arch irq
#include "../include/log.h"
#include "../include/task.h"
#include "../include/timer.h"
#include "../include/memory.h"
#include "../include/utils.h"

void do_test(){

    log_test("Testing memcpy...");

    Buffer src = "Hello Thang Cao";
    Address dst = kmalloc(100);
    memcpy(dst, (Address)src, 16);
    printf("This is the final: %s\n", dst);
}