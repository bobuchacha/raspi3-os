ENTRY(kernel_entry)

REAL_LOAD = 0x40100000;
VIRT_ADDR = 0xFFFF000000000000;
STACK_SIZE = 0x4000;

PHDRS {
        kernel_rx PT_LOAD FLAGS(5);
        kernel_rw PT_LOAD FLAGS(6);
}

SECTIONS {
        . = REAL_LOAD + VIRT_ADDR;

        .text : AT (ADDR(.text) - VIRT_ADDR) {
                KEEP(*(.text.boot))
                *(.text .text.*)
        } :kernel_rx

        . = ALIGN(16);
        .rodata : AT (ADDR(.rodata) - VIRT_ADDR) {
                *(.rodata .rodata.*)
        } :kernel_rx

        . = ALIGN(16);

        PROVIDE(_data = .);
        .data : AT (ADDR(.data) - VIRT_ADDR) {
                *(.data .data.*)
        } :kernel_rw

        .bss (NOLOAD) : AT (ADDR(.bss) - VIRT_ADDR) {
                __bss_start = .;
                bss_begin = .;
                *(.bss .bss.* COMMON)
                bss_end = .;
                __bss_end = .;
        } :kernel_rw

        . = ALIGN(16);
        __stack_bottom = .;
        . += STACK_SIZE;
        __stack_top = .;

        _end = .;
}

real_load = REAL_LOAD;
virt_addr = VIRT_ADDR;