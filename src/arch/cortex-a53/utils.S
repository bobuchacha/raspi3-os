#include "macros.S"

FUNCTION get_el
	mrs x0, CurrentEL
	lsr x0, x0, #2
	ret
ENDFUNC

.globl get_sp
get_sp:
	mov x0, sp
	ret

.globl popl
popl:
    POP x0
    ret

.globl pushl
pushl:
    PUSH x0
    ret

FUNCTION pop_pair
    POPP x0, x1
    ret
ENDFUNC

FUNCTION push_pair
    PUSHP x0, x1
    ret
ENDFUNC


.globl put32
.globl put_int
put32:
put_int:
	str w1,[x0]
	ret

.globl get32
.globl get_int
get32:
get_int:
	ldr w0,[x0]
	ret

.globl get_short
.globl get16
get16:
get_short:
	ldr w0,[x0, #16]
	ret

.global put_short
.globl put16
put16:
put_short:
	str w1,[x0, #16]
	ret

.globl get_char
.globl get8
get8:
get_char:
	ldr w0,[x0, #8]
	ret

.global put_char
.globl put8
put8:
put_char:
	str w1,[x0], #8
	ret


.globl delay
delay:
	subs x0, x0, #1
	bne delay
	ret

//--------------------------------------------------------------
// func: clear memory
//--------------------------------------------------------------
.globl memzero
memzero:
    str     xzr, [x0], #8
    subs    x1, x1, #8
    b.gt    memzero
    ret

//--------------------------------------------------------------
// func: copy memory
//--------------------------------------------------------------
.globl memcpy
memcpy:
    ldr     x3, [x1], #8
    str     x3, [x0], #8
    subs    x2, x2, #8
    b.gt    memcpy
    ret

.globl get_sp_el0
get_sp_el0:
	mrs x0, sp_el0
	ret