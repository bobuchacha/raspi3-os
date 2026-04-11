ENTRY(_start)

REAL_LOAD = 0x80000;
VIRT_ADDR = 0xFFFF000000000000;

PHDRS {
        boot_rx   PT_LOAD FLAGS(5);
        boot_rw   PT_LOAD FLAGS(6);
        kernel_rx PT_LOAD FLAGS(5);
        kernel_rw PT_LOAD FLAGS(6);
}

SECTIONS {
        . = REAL_LOAD;
        .text.boot : {KEEP(*(.text.boot)) *(.text.boot.c)} :boot_rx
        .data.low : {KEEP(*(.data.low))} :boot_rw
        boot_code_end = .;

        . = ALIGN(0x1000);

        PROVIDE(ttbr0 = .);
        PROVIDE(ttbr0_pgd = .);
        ttbr0_pgd (NOLOAD) : { . += ((1 << 12)); } :boot_rw
        PROVIDE(ttbr0_pud = .);
        ttbr0_pud (NOLOAD) : { . += ((1 << 12)); } :boot_rw
        PROVIDE(ttbr0_pmd = .);
        ttbr0_pmd (NOLOAD) : { . += ((1 << 12)); } :boot_rw

        PROVIDE(ttbr1 = .);
        PROVIDE(ttbr1_pgd = .);
        ttbr1_pgd (NOLOAD) : { . += ((1 << 12)); } :boot_rw
        PROVIDE(ttbr1_pud = .);
        ttbr1_pud (NOLOAD) : { . += ((1 << 12)); } :boot_rw
        PROVIDE(ttbr1_pmd = .);
        ttbr1_pmd (NOLOAD) : { . += ((1 << 12)); } :boot_rw

        . = . + VIRT_ADDR;

        . = ALIGN(0x1000);
        .text   : AT (ADDR (.text) - VIRT_ADDR) { *(.text) } :kernel_rx
        kernel_code_end = .;

        .rodata : AT (ADDR (.rodata) - VIRT_ADDR) { *(.rodata) } :kernel_rx

        PROVIDE(_data = .);
        .data   : AT (ADDR (.data) - VIRT_ADDR) { *(.data) } :kernel_rw

        .bss (NOLOAD) : AT (ADDR (.bss) - VIRT_ADDR) {
                bss_begin = .;
                *(.bss COMMON)
                bss_end = .;
        } :kernel_rw

        _end = .;
        PROVIDE(HEAP_START = .);
}

bss_size = (bss_end - bss_begin) >> 3;
virt_addr = VIRT_ADDR;