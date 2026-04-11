/*
 * sp_api.h
 *
 * Kernel integration contract consumed by the scheduler/process-manager module.
 */
#ifndef SP_API_H
#define SP_API_H

#include "sp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*SP_HEAP_ALLOC_FN)(size_t size);
typedef void (*SP_HEAP_FREE_FN)(void *memory);
typedef uint64_t (*SP_GET_TICK_FN)(void);
typedef void (*SP_LOG_LINE_FN)(int level, const char *message);

typedef struct SpKernelApiStruct {
    SP_HEAP_ALLOC_FN heapAlloc;
    SP_HEAP_FREE_FN  heapFree;
    SP_GET_TICK_FN   getTickCount;
    SP_LOG_LINE_FN   logLine;
} SP_KERNELAPI;

typedef const SP_KERNELAPI *PCSP_KERNELAPI;

#ifdef __cplusplus
}
#endif

#endif /* SP_API_H */
