SECTIONS
{
    . = 0x80000;
	.text.boot : { *(.text.boot) }
	.text : { *(.text .text.* .gnu.linkonce.t*) }
	.rodata : { *(.rodata) }
	. = ALIGN(4096);
	.data : { *(.data) }
	bss_begin = .;
	.bss (NOLOAD) : {
            . = ALIGN(16);
            bss_begin = .;
            *(.bss .bss.*)
            *(COMMON)
            bss_end = .;
        }
	bss_end = .;
	. = ALIGN(0x8);
	_end = .;

	/DISCARD/ : { *(.comment) *(.gnu*) *(.note*) *(.eh_frame*) }
}
bss_size = (bss_end - bss_begin)>>3;
