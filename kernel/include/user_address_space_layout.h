#pragma once

#include "mm.h"

#if !defined(__cplusplus)
#error "user_address_space_layout.h requires C++"
#endif

namespace user_address_space {

    /**
     * Round one user virtual address up to the next EL0 L2 boundary.
     *
     * Several user-visible windows still consume whole 2 MiB slots even when
     * their backing is much smaller. Keeping the align-up rule in one place
     * prevents the loader, GUI service, and shared-memory code from drifting
     * into incompatible address calculations as the layout evolves.
     *
     * @param address Exclusive end address of the preceding reserved range.
     * @return First L2-aligned address at or above `address`.
     */
    constexpr VirtAddr align_up_to_l2(VirtAddr address) {
        return (address + (mm::backend::L2BlockSize - 1ULL)) & ~(mm::backend::L2BlockSize - 1ULL);
    }

    // Keep the main executable at the first stable EL0 L2 slot so existing
    // process startup code still lands on the same well-known boundary.
    inline constexpr VirtAddr ExecutableBase = mm::backend::L2BlockSize;

    // Reserve the next L2 slot for the initial user stack so process startup
    // keeps one stable stack window even while the actual mapped stack size is
    // temporarily smaller during the migration to page-granular EL0 backing.
    inline constexpr VirtAddr InitialStackBase = ExecutableBase + mm::backend::L2BlockSize;
    inline constexpr VirtAddr InitialStackLimit = InitialStackBase + mm::backend::L2BlockSize;
    inline constexpr Size InitialThreadStackSlotBytes = 256U * 1024U;
    inline constexpr Size InitialThreadStackSlotCount = (InitialStackLimit - InitialStackBase) / InitialThreadStackSlotBytes;

    // Dynamic modules continue to reserve the low EL0 arena immediately after
    // the executable and other fixed startup ranges.
    inline constexpr VirtAddr ModuleRegionBase = 3ULL * mm::backend::L2BlockSize;
    inline constexpr VirtAddr ModuleRegionLimit = 64ULL * mm::backend::L2BlockSize;

    // GUI surfaces use 64 KiB virtual slots so many small windows can share one
    // broader reserved arena without burning a full 2 MiB view per surface.
    inline constexpr Size GuiSurfaceSlotUnitBytes = 64U * 1024U;
    inline constexpr Size GuiSurfaceSlotUnitCount = 4096U;
    inline constexpr Size GuiSurfaceViewRegionBytes = GuiSurfaceSlotUnitBytes * GuiSurfaceSlotUnitCount;
    inline constexpr VirtAddr GuiSurfaceViewBase = 64ULL * mm::backend::L2BlockSize;
    inline constexpr VirtAddr GuiSurfaceViewLimit = GuiSurfaceViewBase + GuiSurfaceViewRegionBytes;

    // Shared input now reserves one 64 KiB view so the shared ring can grow
    // its consumer registry without changing the fixed user VA anchor.
    inline constexpr Size GuiSharedInputViewBytes = 64U * 1024U;
    inline constexpr VirtAddr GuiSharedInputViewBase = GuiSurfaceViewLimit;
    inline constexpr VirtAddr GuiSharedInputViewLimit = GuiSharedInputViewBase + GuiSharedInputViewBytes;

    // Named shared-memory views still use whole L2 slots. Place the shared
    // view region at the next L2 boundary after shared input so it cannot
    // overlap GUI surfaces, then derive both shared-memory and file-mapping
    // capacity from that remaining pre-heap arena.
    inline constexpr Size SharedMemorySlotBytes = mm::backend::L2BlockSize;
    inline constexpr VirtAddr HeapLimit = mm::backend::TableEntries * mm::backend::L2BlockSize;
    inline constexpr VirtAddr HeapBase = 256ULL * mm::backend::L2BlockSize;
    inline constexpr Size HeapReservedBytes = HeapLimit - HeapBase;
    inline constexpr VirtAddr HeapCompatBase = HeapBase + (HeapReservedBytes / 2U);
    inline constexpr Size HeapCompatBytes = HeapLimit - HeapCompatBase;
    inline constexpr VirtAddr SharedViewRegionBase = align_up_to_l2(GuiSharedInputViewLimit);
    inline constexpr VirtAddr SharedViewRegionLimit = HeapBase;
    inline constexpr Size SharedViewRegionBytes = SharedViewRegionLimit - SharedViewRegionBase;
    inline constexpr Size SharedViewRegionSlotCount = SharedViewRegionBytes / SharedMemorySlotBytes;
    inline constexpr Size SharedMemoryViewBytes = (SharedViewRegionSlotCount / 2U) * SharedMemorySlotBytes;
    inline constexpr VirtAddr SharedMemoryViewBase = SharedViewRegionBase;
    inline constexpr VirtAddr SharedMemoryViewLimit = SharedMemoryViewBase + SharedMemoryViewBytes;

    // File mappings consume the remainder of the shared-view region below the
    // heap reservation, so both subsystems scale from the same authoritative
    // address-layout budget rather than separate hard-coded slot counts.
    inline constexpr Size FileMappingViewSlotBytes = mm::backend::L2BlockSize;
    inline constexpr VirtAddr FileMappingViewBase = SharedMemoryViewLimit;
    inline constexpr VirtAddr FileMappingViewLimit = SharedViewRegionLimit;
    inline constexpr Size FileMappingViewBytes = FileMappingViewLimit - FileMappingViewBase;

    static_assert((GuiSurfaceSlotUnitBytes% mm::PageSize) == 0U, "GUI surface slots must stay page aligned");
    static_assert((InitialThreadStackSlotBytes% mm::PageSize) == 0U, "thread stack slots must stay page aligned");
    static_assert((InitialStackLimit - InitialStackBase) == (InitialThreadStackSlotBytes * InitialThreadStackSlotCount), "initial stack reservation must divide into thread slots");
    static_assert((GuiSharedInputViewBase% mm::PageSize) == 0U, "shared input view must stay page aligned");
    static_assert(ModuleRegionLimit <= GuiSurfaceViewBase, "module arena must stay below GUI views");
    static_assert(SharedViewRegionBase >= GuiSharedInputViewLimit, "shared view region must start after shared input");
    static_assert(SharedViewRegionLimit == HeapBase, "shared view region must stop at the heap base");
    static_assert((SharedViewRegionBytes% SharedMemorySlotBytes) == 0U, "shared view region must divide into whole L2 slots");
    static_assert(SharedViewRegionSlotCount >= 2U, "shared view region must provide at least one slot for each subsystem");
    static_assert(GuiSharedInputViewLimit <= SharedMemoryViewBase, "shared memory must not overlap GUI views");
    static_assert(SharedMemoryViewBytes >= SharedMemorySlotBytes, "shared memory must keep at least one L2 slot");
    static_assert((SharedMemoryViewBytes% SharedMemorySlotBytes) == 0U, "shared memory must divide into whole L2 slots");
    static_assert((FileMappingViewBase% mm::PageSize) == 0U, "file mapping view must stay page aligned");
    static_assert(FileMappingViewBytes >= FileMappingViewSlotBytes, "file mapping view must keep at least one L2 slot");
    static_assert((FileMappingViewBytes% FileMappingViewSlotBytes) == 0U, "file mapping view must divide into whole L2 slots");
    static_assert(SharedMemoryViewLimit <= FileMappingViewBase, "file mappings must not overlap shared memory");
    static_assert(HeapBase < HeapLimit, "heap range must have positive size");
    static_assert(HeapCompatBase < HeapLimit, "compat heap range must fit inside the heap reservation");
    static_assert(FileMappingViewLimit <= HeapBase, "file mapping window must stay below the heap base");
    static_assert(SharedMemoryViewLimit <= HeapBase, "heap base must stay above shared views");
    static_assert(HeapLimit <= (mm::backend::TableEntries * mm::backend::L2BlockSize), "heap limit must stay inside the first user GiB");

} // namespace user_address_space