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
    // Program the in-kernel low-memory spin table used by the local boot loop.
    ldr x1, =secondary_entry_table
    lsl x3, x0, #3
    add x1, x1, x3
    ldr x2, =secondary_boot
    str x2, [x1]

    // Program the Raspberry Pi firmware armstub release slot.
    // Core 1 uses 0xE0, core 2 uses 0xE8, and core 3 uses 0xF0.
    sub x3, x0, #1
    mov x4, #0xE0
    add x4, x4, x3, lsl #3
    str x2, [x4]

    // Clean the release slot so parked secondaries without our MMU context
    // can observe the new target address in memory.
    dc cvac, x4
    ret

.global secondary_publish_boot_entries
secondary_publish_boot_entries:
    // Ensure the spin-table and firmware release-slot writes are visible
    // before waking the secondary cores.
    dsb sy
    sev
    ret
