/*
 * ipc_api.h
 *
 * File purpose:
 *   Declares the callback table that lets the IPC module plug into the host
 *   kernel without hard-coding allocator, timer, or logging policy.
 *
 * Design notes:
 *   This follows the same pattern as my-loader and my-schedproc. The module
 *   owns IPC object logic, while the embedding kernel supplies services such as
 *   heap allocation, tick retrieval, and optional diagnostics.
 */
#ifndef IPC_API_H
#define IPC_API_H

#include "ipc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Allocate zero or more bytes for module-owned state. */
typedef void *(*IPC_HEAP_ALLOC_FN)(size_t size);
/* Free memory previously returned by heapAlloc. */
typedef void (*IPC_HEAP_FREE_FN)(void *memory);
/* Return the current kernel tick for timeout accounting and tracing. */
typedef uint64_t (*IPC_GET_TICK_FN)(void);
/* Emit a diagnostic line at a caller-defined verbosity level. */
typedef void (*IPC_LOG_LINE_FN)(int level, const char *message);

typedef struct IpcKernelApiStruct {
    /* Optional custom allocator used for all module-owned memory. */
    IPC_HEAP_ALLOC_FN heapAlloc;
    /* Matching free routine for memory returned by heapAlloc. */
    IPC_HEAP_FREE_FN heapFree;
    /* Optional time source used once blocking waits are implemented. */
    IPC_GET_TICK_FN getTickCount;
    /* Optional logger for state transitions and failure reporting. */
    IPC_LOG_LINE_FN logLine;
} IPC_KERNELAPI;

/* Read-only view of the kernel callback table passed into ipc_init. */
typedef const IPC_KERNELAPI *PCIPC_KERNELAPI;

#ifdef __cplusplus
}
#endif

#endif /* IPC_API_H */
