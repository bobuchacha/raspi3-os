/*
 * gwe_api.h
 *
 * Kernel integration contract for the graphics/windowing/events module.
 */
#ifndef GWE_API_H
#define GWE_API_H

#include "gwe_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*GWE_HEAP_ALLOC_FN)(size_t size);
typedef void (*GWE_HEAP_FREE_FN)(void *memory);
typedef uint64_t (*GWE_GET_TICK_FN)(void);
typedef void (*GWE_LOG_LINE_FN)(int level, const char *message);

typedef struct GweKernelApiStruct {
    GWE_HEAP_ALLOC_FN heapAlloc;
    GWE_HEAP_FREE_FN heapFree;
    GWE_GET_TICK_FN getTickCount;
    GWE_LOG_LINE_FN logLine;
} GWE_KERNELAPI;

typedef const GWE_KERNELAPI *PCGWE_KERNELAPI;

typedef struct GweInitInfoStruct {
    int32_t desktopWidth;
    int32_t desktopHeight;
    bool compositorEnabled;
} GWE_INITINFO;

#ifdef __cplusplus
}
#endif

#endif /* GWE_API_H */
