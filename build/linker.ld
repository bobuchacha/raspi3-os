SECTIONS
{
	.text.boot : { *(.text.boot) }
	.text : { *(.text) }
	.rodata : { *(.rodata) }
	.data : { *(.data) }
	. = ALIGN(0x8);
	__bss_start = .;
    bss_begin = .;
	.bss : { *(.bss*) } 
	__bss_end = .;
    bss_end = .;
}
