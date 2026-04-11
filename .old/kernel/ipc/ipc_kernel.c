/*
 * ipc_kernel.c
 *
 * Bind the standalone IPC module to real kernel services.
 *
 * The IPC implementation itself deliberately knows nothing about the kernel's
 * allocator, timer source, or logging conventions. This file supplies those
 * callbacks and owns one global context so the module can participate in a
 * normal kernel boot.
 */
#include "../include/ipc_kernel.h"
#include "../include/ipc_module.h"

#include "../include/log.h"

 /* Pull only the specific kernel entry points we need to avoid ros.h conflicts. */
extern unsigned long kmalloc(int bytes);
extern void kfree(unsigned long ptr);
extern unsigned long schedler_get_ticks(void);

/* Single kernel-owned IPC instance shared by tests and future subsystems. */
static PIPC_CONTEXT g_ipc_kernel_context = NULL;

/* Route IPC heap allocations through the kernel heap. */
static void* ipc_kernel_heap_alloc(size_t size) {
    return (void*)kmalloc((int)(size ? size : 1u));
}

/* Return IPC-owned memory back to the kernel heap. */
static void ipc_kernel_heap_free(void* memory) {
    if (memory) {
        kfree((unsigned long)memory);
    }
}

/* Reuse scheduler ticks so IPC timeouts observe the kernel's time base. */
static uint64_t ipc_kernel_get_tick_count(void) {
    return (uint64_t)schedler_get_ticks();
}

/* Forward IPC diagnostics into the kernel log with a simple severity split. */
static void ipc_kernel_log_line(int level, const char* message) {
    if (!message) {
        return;
    }

    if (level > 0) {
        log_warning("ipc: %s", message);
    }
    else {
        log_info("ipc: %s", message);
    }
}

/* Callback table captured by ipc_init() for the singleton context. */
static const IPC_KERNELAPI g_ipc_kernel_api = {
    .heapAlloc = ipc_kernel_heap_alloc,
    .heapFree = ipc_kernel_heap_free,
    .getTickCount = ipc_kernel_get_tick_count,
    .logLine = ipc_kernel_log_line,
};

int ipc_kernel_init(void) {
    IPC_RESULT result;

    if (g_ipc_kernel_context) {
        return 0;
    }

    result = ipc_init(&g_ipc_kernel_api, &g_ipc_kernel_context);
    if (result != IPC_OK) {
        log_warning("IPC kernel init failed (result=%d)", (int)result);
        g_ipc_kernel_context = NULL;
        return -1;
    }

    log_info("IPC kernel context initialized");
    return 0;
}

PIPC_CONTEXT ipc_kernel_context(void) {
    return g_ipc_kernel_context;
}

void ipc_kernel_shutdown(void) {
    if (!g_ipc_kernel_context) {
        return;
    }

    (void)ipc_shutdown(g_ipc_kernel_context);
    g_ipc_kernel_context = NULL;
}