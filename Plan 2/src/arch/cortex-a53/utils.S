.globl get_el
get_el:
	mrs x0, CurrentEL
	lsr x0, x0, #2
	ret

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
