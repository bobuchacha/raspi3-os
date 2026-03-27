.section ".data.low"
.global secondary_entry_table
.balign 8
secondary_entry_table:
    .xword 0, 0, 0, 0

.global secondary_boot_stacks
.balign 16
secondary_boot_stacks:
    .space 4096 * 4

.section ".text"
.global secondary_program_boot_entry
secondary_program_boot_entry:
    ldr x1, =secondary_entry_table
    lsl x3, x0, #3
    add x1, x1, x3
    ldr x2, =secondary_boot
    str x2, [x1]

    sub x3, x0, #1
    mov x4, #0xE0
    add x4, x4, x3, lsl #3
    str x2, [x4]
    dc cvac, x4
    ret

.global secondary_publish_boot_entries
secondary_publish_boot_entries:
    dsb sy
    sev
    ret