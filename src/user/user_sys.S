.section ".text.user"

.set SYS_WRITE_NUMBER, 0 		// syscal numbers 
.set SYS_MALLOC_NUMBER, 1
.set SYS_FORK_NUMBER, 2 	
.set SYS_EXIT_NUMBER, 3 	
.set SYS_GET_PARAM, 4


.globl syscall
syscall:
	mov 	x8, x0
	mov 	x0, x1
	mov 	x1, x2
	mov 	x2, x3
	mov 	x3, x4
	mov 	x4, x5
	mov 	x5, x6
	mov 	x6, x7
	svc 	#0
	ret

.globl user_delay
user_delay:
	subs x0, x0, #1
	bne user_delay
	ret

.globl call_sys_write
call_sys_write:
	mov w8, #SYS_WRITE_NUMBER	
	svc #0
	ret

.globl call_sys_malloc
call_sys_malloc:
	mov w8, #SYS_MALLOC_NUMBER	
	svc #0
	ret

.globl call_sys_exit
call_sys_exit:
	mov w8, #SYS_EXIT_NUMBER	
	svc #0
	ret

.globl call_sys_fork
call_sys_fork:
	mov w8, #SYS_FORK_NUMBER	
	svc #0
	ret

.global call_sys_get_param
call_sys_get_param:
    mov w8, #SYS_GET_PARAM
    svc #0
    ret

.global user_get_sp
user_get_sp:
	mov x0, sp
	ret
