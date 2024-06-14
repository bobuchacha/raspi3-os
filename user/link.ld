
ENTRY(_entrypoint)
SECTIONS
{
    . = 0x4000;
    .entrypoint : { *(.entrypoint) }
    .text : { *(.text) }
    .data : { *(.data) }
    .bss : { *(.bss) }

    .align(2048)
}