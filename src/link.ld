ENTRY(_start)
REAL_LOAD = 0x80000;
VIRT_ADDR = 0xFFFF000000000000;

SECTIONS {
        /* LOW BOOT CODE */
        . = REAL_LOAD;
        .text.boot : {KEEP(*(.text.boot)) *(.text.boot.c)}
        .data.low : {KEEP(*(.data.low))}
        boot_code_end = .;

        /* RESERVE */
        . = ALIGN(0x1000);
	
	/* reserve 3 page frame (4K each) for each table */
        PROVIDE(ttbr0 = .);
        PROVIDE(ttbr0_pgd = .);
	ttbr0_pgd : { . += ((1 << 12)); }
        PROVIDE(ttbr0_pud = .);
	ttbr0_pud : { . += ((1 << 12)); }
        PROVIDE(ttbr0_pmd = .);
	ttbr0_pmd : { . += ((1 << 12)); }

        PROVIDE(ttbr1 = .);
        PROVIDE(ttbr1_pgd = .);
        ttbr1_pgd : { . += ((1 << 12)); }
        PROVIDE(ttbr1_pud = .);
	ttbr1_pud : { . += ((1 << 12)); }
        PROVIDE(ttbr1_pmd = .);
	ttbr1_pmd : { . += ((1 << 12)); }

        /* HIGH KERNEL C CODES */        
        . = . + VIRT_ADDR;
        /* . = ALIGN(0x1000); */
        .text   : AT (ADDR (.text) - VIRT_ADDR) { *(.text) }
        kernel_code_end = .;

        .rodata : AT (ADDR (.rodata) - VIRT_ADDR) { *(.rodata) }

        /* . = ALIGN(4096); */
        PROVIDE(_data = .);
        .data   : AT (ADDR (.data) - VIRT_ADDR) { *(.data) }
        .bss	: AT (ADDR (.bss) - VIRT_ADDR) {
                bss_begin = .;
                *(.bss COMMON)
                bss_end = .;
        }
        _end = .;
        PROVIDE(HEAP_START = .);
}

bss_size = (bss_end - bss_begin) >> 3;