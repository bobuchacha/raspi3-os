.section ".text.user"

.set SYS_WRITE_NUMBER, 0
.set SYS_MALLOC_NUMBER, 1
.set SYS_FREE_NUMBER, 2
.set SYS_FORK_NUMBER, 3
.set SYS_EXIT_NUMBER, 4
.set SYS_GET_PARAM, 5
.set SYS_SLEEP_NUMBER, 6
.set SYS_EXEC_NUMBER, 7
.set SYS_SHLIB_OPEN_NUMBER, 8
.set SYS_TASK_NAME_NUMBER, 9
.set SYS_SPAWN_NUMBER, 10
.set SYS_SHLIB_LOCAL_NUMBER, 11
.set SYS_CONSOLE_READ_NUMBER, 12
.set SYS_READ_FILE_NUMBER, 13
.set SYS_DIR_ENTRY_NUMBER, 14
.set SYS_MKDIR_NUMBER, 15
.set SYS_TASK_INFO_NUMBER, 16
.set SYS_MEM_INFO_NUMBER, 17
.set SYS_KILL_NUMBER, 18
.set SYS_REBOOT_NUMBER, 19
.set SYS_DEBUG_SHELL_NUMBER, 20
.set SYS_EXT_INVOKE_NUMBER, 21
.set SYS_TASK_ARGS_NUMBER, 22
.set SYS_MODULE_INVOKE_NUMBER, 23
.set SYS_SHLIB_EXPORT_NUMBER, 24

// Generic variadic wrapper that reshuffles AArch64 argument registers and traps into the kernel.
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

// Busy-wait helper for the rare code path that truly needs a raw spin delay.
.globl user_spin_delay
user_spin_delay:
	subs x0, x0, #1
	bne user_spin_delay
	ret

// Scheduler-friendly delay helper that sleeps in the kernel instead of burning CPU in user mode.
.globl user_delay
user_delay:
	mov w8, #SYS_SLEEP_NUMBER
	svc #0
	ret

// Write a user buffer to the kernel log and return immediately.
.globl call_sys_write
call_sys_write:
	mov w8, #SYS_WRITE_NUMBER
	svc #0
	ret

// Allocate page-backed user heap memory owned by the current task.
.globl call_sys_malloc
call_sys_malloc:
	mov w8, #SYS_MALLOC_NUMBER
	svc #0
	ret

// Terminate the current user task.
.globl call_sys_exit
call_sys_exit:
	mov w8, #SYS_EXIT_NUMBER
	svc #0
	ret

// Release a prior user heap allocation.
.globl call_sys_free
call_sys_free:
	mov w8, #SYS_FREE_NUMBER
	svc #0
	ret

// Clone the current process when the kernel grows real fork support.
.globl call_sys_fork
call_sys_fork:
	mov w8, #SYS_FORK_NUMBER
	svc #0
	ret

// Read a small kernel parameter value for diagnostics or experimentation.
.global call_sys_get_param
call_sys_get_param:
    mov w8, #SYS_GET_PARAM
    svc #0
    ret

// Yield the CPU until the requested sleep interval has elapsed.
.globl call_sys_sleep
call_sys_sleep:
	mov w8, #SYS_SLEEP_NUMBER
	svc #0
	ret

// Replace the current process image with a new ELF from the VFS.
.globl call_sys_exec
call_sys_exec:
	mov w8, #SYS_EXEC_NUMBER
	svc #0
	ret

// Map a packed shared library into the current task and return its entry address.
.globl call_sys_shlib_open
call_sys_shlib_open:
	mov w8, #SYS_SHLIB_OPEN_NUMBER
	svc #0
	ret

// Allocate or retrieve a per-task DLL-local storage block for one shared library.
.globl call_sys_shlib_local
call_sys_shlib_local:
	mov w8, #SYS_SHLIB_LOCAL_NUMBER
	svc #0
	ret

// Copy the current task's user-visible name into a user buffer.
.globl call_sys_task_name
call_sys_task_name:
	mov w8, #SYS_TASK_NAME_NUMBER
	svc #0
	ret

// Start a new user task from an executable path and assign it a small name string.
.globl call_sys_spawn
call_sys_spawn:
	mov w8, #SYS_SPAWN_NUMBER
	svc #0
	ret

// Copy the current task's launch-argument string into a user buffer.
.globl call_sys_task_args
call_sys_task_args:
	mov w8, #SYS_TASK_ARGS_NUMBER
	svc #0
	ret

// Read one raw console byte from the serial input stream.
.globl call_sys_console_read
call_sys_console_read:
	mov w8, #SYS_CONSOLE_READ_NUMBER
	svc #0
	ret

// Read file bytes directly from a VFS path.
.globl call_sys_read_file
call_sys_read_file:
	mov w8, #SYS_READ_FILE_NUMBER
	svc #0
	ret

// Fetch one directory entry by zero-based index.
.globl call_sys_dir_entry
call_sys_dir_entry:
	mov w8, #SYS_DIR_ENTRY_NUMBER
	svc #0
	ret

// Create one directory by absolute path.
.globl call_sys_mkdir
call_sys_mkdir:
	mov w8, #SYS_MKDIR_NUMBER
	svc #0
	ret

// Query one task slot by PID.
.globl call_sys_task_info
call_sys_task_info:
	mov w8, #SYS_TASK_INFO_NUMBER
	svc #0
	ret

// Query basic kernel memory statistics.
.globl call_sys_mem_info
call_sys_mem_info:
	mov w8, #SYS_MEM_INFO_NUMBER
	svc #0
	ret

// Terminate another task by PID.
.globl call_sys_kill
call_sys_kill:
	mov w8, #SYS_KILL_NUMBER
	svc #0
	ret

// Reboot the board immediately.
.globl call_sys_reboot
call_sys_reboot:
	mov w8, #SYS_REBOOT_NUMBER
	svc #0
	ret

// Forward one kernel debug shell command line for privileged inspection helpers.
.globl call_sys_debug_shell
call_sys_debug_shell:
	mov w8, #SYS_DEBUG_SHELL_NUMBER
	svc #0
	ret

// Invoke one registered system extension function by extension and symbol name.
.globl call_sys_ext_invoke
call_sys_ext_invoke:
	mov w8, #SYS_EXT_INVOKE_NUMBER
	svc #0
	ret

// Resolve the runtime address of a named export from a loaded shared library.
.globl call_sys_shlib_export
call_sys_shlib_export:
	mov w8, #SYS_SHLIB_EXPORT_NUMBER
	svc #0
	ret

// Invoke a named export on a loaded kernel module.
.globl call_sys_module_invoke
call_sys_module_invoke:
	mov w8, #SYS_MODULE_INVOKE_NUMBER
	svc #0
	ret

// Return the current stack pointer for low-level debugging.
.global user_get_sp
user_get_sp:
	mov x0, sp
	ret