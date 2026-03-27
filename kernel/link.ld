ENTRY(kernel_entry)

REAL_LOAD = 0x400000;
VIRT_ADDR = 0xFFFF000000000000;

SECTIONS {
        . = REAL_LOAD + VIRT_ADDR;

        .text : AT (ADDR(.text) - VIRT_ADDR) {
                *(.text)
        }

        .rodata : AT (ADDR(.rodata) - VIRT_ADDR) {
                *(.rodata)
        }

        PROVIDE(_data = .);
        .data : AT (ADDR(.data) - VIRT_ADDR) {
                *(.data)
        }

        .bss : AT (ADDR(.bss) - VIRT_ADDR) {
                bss_begin = .;
                *(.bss COMMON)
                bss_end = .;
        }

        _end = .;
        PROVIDE(HEAP_START = .);
}

bss_size = (bss_end - bss_begin) >> 3;
virt_addr = VIRT_ADDR;
