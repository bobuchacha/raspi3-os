ENTRY(_start)

/* Physical load address used by the early boot code before the MMU switches into the higher half. */
REAL_LOAD = 0x80000;

/* Kernel virtual base once execution moves into the higher-half address space. */
VIRT_ADDR = 0xFFFF000000000000;

SECTIONS {
        /* Identity-mapped boot code and data that must execute before higher-half relocation. */
        . = REAL_LOAD;
        .text.boot : {KEEP(*(.text.boot)) *(.text.boot.c)}
        .data.low : {KEEP(*(.data.low))}
        boot_code_end = .;

        /* Reserve page-aligned storage for the early translation tables built during bring-up. */
        . = ALIGN(0x1000);
	
	/* Reserve three 4 KiB pages for each translation-table level used by TTBR0 and TTBR1. */
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


        /* Switch the linker location counter into the higher-half kernel virtual address space. */
        . = . + VIRT_ADDR;

        /* Embed statically linked user objects in the image; they are linked for higher-half addresses. */
        user_begin = .;
        .entry.user : AT (ADDR (.entry.user) - VIRT_ADDR) { build/objs/user* (.entrypoint) }
        .text.user : AT (ADDR (.text.user) - VIRT_ADDR) { build/objs/user* (.text) }
        .rodata.user : AT (ADDR (.rodata.user) - VIRT_ADDR) { build/objs/user* (.rodata) }
        .data.user : AT (ADDR (.data.user) - VIRT_ADDR) { build/objs/user* (.data) }
        .bss.user : AT (ADDR (.bss.user) - VIRT_ADDR) { build/objs/user* (.bss) }
        user_end = .;

        /* Align the main kernel text to a page boundary before placing executable code. */
         . = ALIGN(0x1000);
        .text   : AT (ADDR (.text) - VIRT_ADDR) { *(.text) }
        kernel_code_end = .;

        /* Read-only kernel data follows text while preserving a physical image contiguous with the file. */
        .rodata : AT (ADDR (.rodata) - VIRT_ADDR) { *(.rodata) }

        /* Writable kernel sections start here; `_data` is exported for runtime initialization helpers. */
        PROVIDE(_data = .);
        .data   : AT (ADDR (.data) - VIRT_ADDR) { *(.data) }

        /* Zero-initialized storage is part of the virtual image but does not need preinitialized contents. */
        .bss	: AT (ADDR (.bss) - VIRT_ADDR) {
                bss_begin = .;
                *(.bss COMMON)
                bss_end = .;
        }

        /* `_end` and `HEAP_START` mark the first free byte after the linked kernel image. */
        _end = .;
        PROVIDE(HEAP_START = .);
}

/* Export the BSS size in 64-bit words for the early clear loop in boot code. */
bss_size = (bss_end - bss_begin) >> 3;

/* Expose the higher-half base as a symbol for runtime code that needs the linker constant. */
virt_addr = VIRT_ADDR;