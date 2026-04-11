ENTRY(_start)

REAL_LOAD = 0x80000;
STACK_SIZE = 0x4000;

SECTIONS {
        . = REAL_LOAD;
        .text : {
                KEEP(*(.text.boot))
                *(.text .text.*)
        }

        . = ALIGN(16);
        .rodata : {
                *(.rodata .rodata.*)
        }

        PROVIDE(_data = .);
        .data : {
                *(.data .data.*)
        }

        .bss (NOLOAD) : {
                __bss_start = .;
                bss_begin = .;
                *(.bss .bss.* COMMON)
                bss_end = .;
                __bss_end = .;
        }

        . = ALIGN(16);
        __stack_bottom = .;
        . += STACK_SIZE;
        __stack_top = .;

        _end = .;
}

real_load = REAL_LOAD;