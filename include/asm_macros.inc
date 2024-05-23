//-----------------------------------
// MACRO create a padded string
//-----------------------------------
.macro padded_string string, max
    1:
        .ascii "\string"
    2:
        .iflt \max - (2b - 1b)
        .error "String too long"
        .endif

        .ifgt \max - (2b - 1b)
        .zero \max - (2b - 1b)
        .endif
.endm

//-----------------------------------
// MACRO create PGD entry
//-----------------------------------
.macro	create_pgd_entry, tbl, virt, tmp1, tmp2
        create_table_entry \tbl, \virt, PGD_SHIFT, \tmp1, \tmp2
        create_table_entry \tbl, \virt, PUD_SHIFT, \tmp1, \tmp2
.endm

//-----------------------------------
// MACRO create a table entry
//-----------------------------------
.macro	create_table_entry, tbl, virt, shift, tmp1, tmp2
        lsr	\tmp1, \virt, #\shift
        and	\tmp1, \tmp1, #ENTRY_PER_TABLE - 1			// table index
        add	\tmp2, \tbl, #PAGE_SIZE
        orr	\tmp2, \tmp2, #MM_TYPE_PAGE_TABLE
        str	\tmp2, [\tbl, \tmp1, lsl #3]
        add	\tbl, \tbl, #PAGE_SIZE					// next level table page
.endm

//-----------------------------------
// MACRO create a block map
//-----------------------------------
.macro	create_block_map, tbl, phys, start, end, flags, tmp1
        lsr	\start, \start, #SECTION_SHIFT
        and	\start, \start, #ENTRY_PER_TABLE - 1			// table index
        lsr	\end, \end, #SECTION_SHIFT
        and	\end, \end, #ENTRY_PER_TABLE - 1				// table end index
        lsr	\phys, \phys, #SECTION_SHIFT
        mov	\tmp1, #\flags
        orr	\phys, \tmp1, \phys, lsl #SECTION_SHIFT			// table entry
    9999:
        str	\phys, [\tbl, \start, lsl #3]				// store the entry
        add	\start, \start, #1					// next entry
        add	\phys, \phys, #SECTION_SIZE				// next block
        cmp	\start, \end
        b.ls	9999b
.endm

//-----------------------------------
// MACRO implement push and pop sp
//-----------------------------------
.macro PUSH, reg
    str   \reg, [sp, #-16]!         // push {register}
.endm
.macro POP, reg
    ldr   \reg, [sp], #16           // pop {register}
.endm
