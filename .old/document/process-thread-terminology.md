# Kernel Process and Thread Terminology Cheat Sheet

This note defines the key terms for processes, threads, and related concepts in your kernel, inspired by modern OSes (Linux, Windows NT) and tailored to your codebase.

---

## 1. Process (User Process)
- **Definition:** An independent address space with its own memory, resources, and at least one thread. Represents a running program (user application).
- **Example:** A shell, user program, or any loaded executable.

## 2. Thread (Task, Kernel Thread)
- **Definition:** A schedulable unit of execution within a process. Shares the process’s address space but has its own CPU registers, stack, and scheduling state.
- **Example:** Main thread of a user process, or a kernel thread for background work.

## 3. Kernel Thread
- **Definition:** A thread running in kernel mode, not associated with a user process. Used for kernel background tasks (e.g., idle, housekeeping).
- **Example:** Idle thread, memory manager thread.

## 4. User Thread
- **Definition:** A thread running in user mode, belonging to a user process. Executes user code, but may enter kernel mode via syscalls.
- **Example:** Main thread of a user application.

## 5. Idle Thread (Idle Task)
- **Definition:** Special kernel thread scheduled when no other threads are runnable. Usually one per CPU core.
- **Example:** The thread that runs an infinite loop (e.g., `while(1) { wfi(); }`).

## 6. Zombie Process/Thread
- **Definition:** A process or thread that has finished execution but still exists for bookkeeping (e.g., waiting for parent to collect exit status).

## 7. Kernel Process (optional)
- **Definition:** A process with its own address space, running only kernel code. Rare in embedded kernels, common in NT/UNIX for daemons/services.

## 8. Task (if used)
- **Recommendation:** Use “task” as a generic term for any schedulable entity (thread or process), or as a synonym for “thread” if that matches your codebase.

---

## Summary Table

| Term            | Meaning/Scope                        | Example                        |
|-----------------|-------------------------------------|--------------------------------|
| Process         | Address space + resources + threads  | User program, shell            |
| User Process    | Process running user code            | User application               |
| Kernel Process  | Process running only kernel code     | Kernel service/daemon (rare)   |
| Thread          | Schedulable execution unit           | Main thread, worker thread     |
| User Thread     | Thread in user process               | App’s main thread              |
| Kernel Thread   | Thread in kernel mode                | Idle thread, kernel worker     |
| Idle Thread     | Special kernel thread, runs when idle| CPU idle loop                  |
| Zombie          | Exited, waiting for cleanup          | Dead process/thread            |
| Task            | (Optional) Synonym for thread        | (Depends on your codebase)     |

---

**Tip:**
- Use “process” for address space + resources.
- Use “thread” for schedulable execution units.
- Use “user process” and “kernel thread” for clarity.
- Reserve “task” for legacy code or as a synonym for “thread” if needed.

---

Feel free to update this note as your kernel evolves!