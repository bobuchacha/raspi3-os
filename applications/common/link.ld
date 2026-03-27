ENTRY(_entrypoint)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x4000;

    .entrypoint : ALIGN(0x1000)
    {
        *(.entrypoint)
    } :text

    .text : ALIGN(16)
    {
        *(.text)
        *(.text.*)
    } :text

    .rodata : ALIGN(0x1000)
    {
        *(.rodata)
        *(.rodata.*)
    } :rodata

    .data : ALIGN(0x1000)
    {
        *(.data)
        *(.data.*)
    } :data

    .init_array : ALIGN(16)
    {
        __init_array_start = .;
        KEEP(*(.init_array))
        KEEP(*(.init_array.*))
        __init_array_end = .;
    } :data

    .bss (NOLOAD) : ALIGN(16)
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } :data

    . = ALIGN(0x1000);
}