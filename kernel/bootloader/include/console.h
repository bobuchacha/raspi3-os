#ifndef KERNEL_BOOTLOADER_INCLUDE_CONSOLE_H
#define KERNEL_BOOTLOADER_INCLUDE_CONSOLE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

    Status boot_console_init(void);
    void boot_console_putc(char ch);
    void boot_console_puts(const char* text);
    char boot_console_getc(void);

#ifdef __cplusplus
}
#endif

#endif