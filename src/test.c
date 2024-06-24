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

    log_test("Testing Unicode");
    UWord utf16[512];
    UByte *utf8 = "Cao Đức Thắng thiệt là tuyệt vời";
    utf8_to_utf16(utf8, 512, utf16, 512);
    kdump_size(utf16, 96);
    // convert back to utf8
    UByte utf8_2[512];
    utf16_to_utf8(utf16, 512, utf8_2, 512);
    kprint("        => Result after sound conversion: %s\n", utf8_2);
}