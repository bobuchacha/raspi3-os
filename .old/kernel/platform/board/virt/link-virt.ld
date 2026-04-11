ENTRY(kernel_entry)

REAL_LOAD = 0x40400000;
VIRT_ADDR = 0xFFFF000000000000;

PHDRS {
	kernel_rx PT_LOAD FLAGS(5);
	kernel_rw PT_LOAD FLAGS(6);
}

SECTIONS {
        . = REAL_LOAD + VIRT_ADDR;

        .text : AT (ADDR(.text) - VIRT_ADDR) {
                *(.text)
        } :kernel_rx

        .rodata : AT (ADDR(.rodata) - VIRT_ADDR) {
                *(.rodata)
        } :kernel_rx

	. = ALIGN(0x1000);

        PROVIDE(_data = .);
        .data : AT (ADDR(.data) - VIRT_ADDR) {
                *(.data)
        } :kernel_rw

        .bss (NOLOAD) : AT (ADDR(.bss) - VIRT_ADDR) {
                bss_begin = .;
                *(.bss COMMON)
                bss_end = .;
        } :kernel_rw

        _end = .;
        PROVIDE(HEAP_START = .);
}

bss_size = (bss_end - bss_begin) >> 3;
real_load = REAL_LOAD;
virt_addr = VIRT_ADDR;