load_start = 0x80000;
virt_start = 0xFFFF000000000000;
ENTRY(_start)

SECTIONS
{

    /***************** LOW CODE & DATA, which will be loaded into load_start ********************/
	. = load_start;
    .text.boot :{*(.text.boot)}
	.bss (NOLOAD) : {
        . = ALIGN(8);
        bss_begin = .;
        *(.bss .bss.*)
        *(COMMON)
        bss_end = .;
    }
    .rodata : { *(.rodata) }
    . = ALIGN(4096);
    .data : { *(.data) }


	/***************** PAGE DIRECTORIES ********************/
    . = ALIGN(0x00001000);
    pg_dir = .;
    /*	 reserve 3 page frame (4K each) for each table */
    .directory.pgd : { . += ((1 << 12)); }
    .directory.pud : { . += ((1 << 12)); }
    .directory.pmd : { . += ((1 << 12)); }
    . = . + virt_start;

    /***************** HIGH CODE 64, everything here will takes VA ********************/
	. = ALIGN(0x00001000);
    .text : AT(code_start - virt_start) {
        code_start = .;
        *(.text*)
    }
	/DISCARD/ : { *(.comment) *(.gnu*) *(.note*) *(.eh_frame*) }

	_end = . - virt_start;
}
kernel_size = _end - load_start;
bss_size = (bss_end - bss_begin)>>3;
