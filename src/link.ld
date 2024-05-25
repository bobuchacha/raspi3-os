SECTIONS
{
    . = 0x80000;
	.text : { KEEP(*(.text.boot)) *(.text .text.* .gnu.linkonce.t*) }
	.rodata : { *(.rodata .rodata.* .gnu.linkonce.r*) }

	. = ALIGN(4096);
	PROVIDE(_data = .);
	.data : { *(.data .data.* .gnu.linkonce.d*) }

	.bss (NOLOAD) : {
        . = ALIGN(16);
        bss_begin = .;
        *(.bss .bss.*)
        *(COMMON)
        bss_end = .;
    }

	. = ALIGN(4096);
	_end = .;

	/DISCARD/ : { *(.comment) *(.gnu*) *(.note*) *(.eh_frame*) }
}
bss_size = (bss_end - bss_begin)>>3;
