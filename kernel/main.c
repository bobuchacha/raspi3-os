

#include "ros.h"
#include "hardware/uart/uart.h"
#include "hardware/mailbox/mbox.h"
#include "hardware/timer/delays.h"
#include "hardware/power/power.h"
#include "hardware/framebuffer/lfb.h"
#include "hardware/sdcard/sd.h"
#include "../not used code/fat32.h"
#include "random/rand.h"
#include "debugger/dbg.h"
#include "lib/string.h"
#include "memory/paging.h"
#include "terminal/terminal.h"
#include "console.h"
#include "filesystem/filesystem.h"
//#include "filesystem/FAT/fat32.h"
//#include "../src/fat32_console.h"

// #include "process/process.h"


// get the end of bss segment from linker
extern unsigned char _end;
extern void __asmcode();
extern int get_current_el();

#define KERNEL_UART0_DR        ((volatile unsigned int*)0xFFFFFFFFFFE00000)
#define KERNEL_UART0_FR        ((volatile unsigned int*)0xFFFFFFFFFFE00018)


struct test1 {
    int x;
    int y;
    int z;
};

struct test2 {
    int x: 24;
    int y: 24;
    int z: 24;
};

void main(INT r0, INT r1, INT atags)
{
    char *s="Writing through MMIO mapped in higher half!\r\n";
    char c;
    unsigned int r;
    unsigned int cluster;
//    unsigned long el;
    char* author = (char*)0x80000+4+8+4;
    char* version = (char*)0x80000+4+8+4+32+64;
    char* copyright = (char*)0x80000+4+8+4+32;

    int a = strlen(s);

    char device[128];



    // set up serial console
    uart_init();
    terminal_init();
    mem_init_paging((atag_t*)atags);
    terminal_printf("Welcome\n%s, %d\n\n\n", copyright, a);

    // init file system
    fs_init();
    printf("\n\n\n");
    // display a pixmap
    lfb_showpicture();


   // setup FAT32 console
//   printf("Creating fat32 filesystem.\n");
//   master_fs = makeFilesystem("");
//   if(master_fs == NULL) {
//       printf("Failed to create fat32 filesystem. Disk may be corrupt.\n");
//       return;
//   }
//
//    unsigned int el = get_current_el();
//    printf ("current EL %d\n", el);
//
//    fat32_console(master_fs);

    // echo everything back
    printf("\nKernel function ended. Press any key to close and exit...\n");
    while(c=uart_getc()) {
//        // uart_send(uart_getc());
//         uart_puts(" 1 - power off\n 2 - reset\nChoose one: ");
       ;
//        uart_send(c);
//        uart_puts("\n\n");
       if(c) power_off();
//        if(c=='2') reset();

    }
}
