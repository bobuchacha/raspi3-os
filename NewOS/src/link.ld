SECTIONS
{
    /* . = 0xffff000000080000; */
	. = 0x80000;
	.text.boot : { *(.text.boot) }

	. = ALIGN(0x00001000);
	user_begin = .;
    .text.user : { build/user* (.text) }
    .rodata.user : { build/user* (.rodata) }
    .data.user : { build/user* (.data) }
    .bss.user : { build/user* (.bss) }
    user_end = .;


	.text : { *(.text .text.* .gnu.linkonce.t*) }
	.rodata : { *(.rodata) }
	. = ALIGN(4096);
	.data : { *(.data) }

	bss_begin = .;
	.bss (NOLOAD) : {
            . = ALIGN(0x8);
            bss_begin = .;
            *(.bss .bss.*)
            *(COMMON)
            bss_end = .;
        }
	bss_end = .;
	. = ALIGN(0x00001000);
	pg_dir = .;
/*	 reserve 3 page frame (4K each) for each table */
	.data.pgd : { . += ((1 << 12)); }
	.data.pud : { . += ((1 << 12)); }
	.data.pmd : { . += ((1 << 12)); }

	_end = .;
	/DISCARD/ : { *(.comment) *(.gnu*) *(.note*) *(.eh_frame*) }
}

bss_size = (bss_end - bss_begin)>>3;
