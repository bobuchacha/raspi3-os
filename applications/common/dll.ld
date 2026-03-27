ENTRY(_shared_library_entry)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    /* Simple shared libraries live in a fixed high user region so apps can call them without a full relocator. */
    . = 0x200000;

    /* The first executable bytes hold the library entry trampoline returned by the kernel open syscall. */
    .entrypoint : ALIGN(0x1000)
    {
        *(.entrypoint)
    } :text

    /* Library code follows the entry stub and stays read-only/executable. */
    .text : ALIGN(16)
    {
        *(.text)
        *(.text.*)
    } :text

    /* Export tables and constant data live in a separate read-only segment. */
    .rodata : ALIGN(0x1000)
    {
        *(.rodata)
        *(.rodata.*)
    } :rodata

    /* Keep constructor bounds available so the shared user runtime links even for simple C-only libraries. */
    .init_array : ALIGN(16)
    {
        __init_array_start = .;
        KEEP(*(.init_array))
        KEEP(*(.init_array.*))
        __init_array_end = .;
    } :rodata

    /* Writable shared-library pages hold state that is shared across every process using the DLL. */
    .data : ALIGN(0x1000)
    {
        *(.data)
        *(.data.*)
    } :data

    .bss (NOLOAD) : ALIGN(16)
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } :data

    . = ALIGN(0x1000);
}
