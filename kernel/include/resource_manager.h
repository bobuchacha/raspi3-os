#ifndef KERNEL_INCLUDE_RESOURCE_MANAGER_H
#define KERNEL_INCLUDE_RESOURCE_MANAGER_H

#include "types.h"

#if !defined(__cplusplus)
#error "resource_manager.h requires C++"
#endif

/*
 * Classify one tracked kernel resource allocation.
 *
 * The current table focuses on loader-facing growth hotspots because those are
 * the objects that historically hit fixed ceilings first during repeated GUI
 * process launches.
 */
enum class KernelResourceKind : U32 {
    LoaderStackBacking = 0,
    LoaderImageBacking = 1,
    LoaderModuleRecord = 2,
    LoaderImageCachePayload = 3,
    LoaderImageCacheRecord = 4,
    Count = 5,
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
 * The loader previously relied on several fixed arrays and byte caps. This
 * manager replaces those hard ceilings with a single heap-backed registry that
 * tracks live allocations and high-water marks per resource kind.
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