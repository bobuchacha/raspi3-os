#include "macros.S"
#include "memory.h"

FUNCTION kernel_entry
    ldr     x0, =VA_START
    add     sp, x0, #LOW_MEMORY_CEILING
    bl      disable_irq
    bl      irq_vector_init
    bl      kernel_main
1:
    wfe
    b       1b
ENDFUNC
