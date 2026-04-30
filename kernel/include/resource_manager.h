#ifndef KERNEL_INCLUDE_RESOURCE_MANAGER_H
#define KERNEL_INCLUDE_RESOURCE_MANAGER_H

#include "types.h"

#if !defined(__cplusplus)
#error "resource_manager.h requires C++"
#endif

/*
 * Classify one tracked kernel resource allocation.
 *
 * The current table focuses on kernel-owned growth hotspots so repeated loads,
 * GUI activity, and user-heap demand paging can expose the same bookkeeping
 * pressure points through one shared stats surface.
 */
enum class KernelResourceKind : U32 {
    LoaderStackBacking = 0,
    LoaderImageBacking = 1,
    LoaderModuleRecord = 2,
    LoaderSharedImageRecord = 3,
    LoaderImageCachePayload = 4,
    LoaderImageCacheRecord = 5,
    UserHeapAllocationRecord = 6,
    UserHeapMappedPageRecord = 7,
    GuiSurfaceRecord = 8,
    GuiSurfaceBacking = 9,
    FileMappingBacking = 10,
    Count = 11,
};

/*
 * Summarize one resource-kind occupancy snapshot.
 */
typedef struct KernelResourceStats {
    Size live_count;
    Size peak_count;
    Size live_bytes;
    Size peak_bytes;
} KernelResourceStats;

/*
 * Central bookkeeping for dynamically growing kernel resources.
 *
 * Subsystems such as the loader and user-heap manager previously relied on
 * ad-hoc growth accounting. This manager replaces those hard ceilings with one
 * heap-backed registry that tracks live allocations and high-water marks per
 * resource kind.
 */
class KernelResourceManager final {
public:
    static Status init(void);
    static void* allocate(KernelResourceKind kind, Size size, Size alignment, U64 owner_id, const char* label);
    static Status track_external(KernelResourceKind kind, void* address, Size size, U64 owner_id, const char* label);
    static Status untrack(KernelResourceKind kind, const void* address);
    static void release(void* address);
    static void get_stats(KernelResourceKind kind, KernelResourceStats* stats_out);
};

#endif // KERNEL_INCLUDE_RESOURCE_MANAGER_H