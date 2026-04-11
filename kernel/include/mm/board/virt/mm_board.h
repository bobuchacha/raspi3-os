#ifndef KERNEL_INCLUDE_MM_BOARD_VIRT_MM_BOARD_H
#define KERNEL_INCLUDE_MM_BOARD_VIRT_MM_BOARD_H

#include "types.h"

namespace mm::board_config {

    inline constexpr U64 DeviceBase = 0x08000000ULL;    // This is where device lives
    inline constexpr U64 DeviceLimit = 0x10000000ULL;    // This is the end of the device memory range
    inline constexpr U64 ExtraRamBase = 0x40000000ULL;    // This is where extra RAM starts
    inline constexpr U64 ExtraRamSize = 0x10000000ULL;    // QEMU virt boots with 256 MiB in the current Makefile.
    // Until the boot handoff exposes a proper free-memory map, the active
    // kernel heap lives in one explicit RAM carve-out far enough above the
    // loaded kernel image that the early direct map can treat it as owned.
    inline constexpr U64 EarlyHeapPhysicalBase = 0x42000000ULL;
    inline constexpr U64 EarlyHeapSize = 0x06000000ULL;
    // Page-backed kernel objects and user mappings must not reuse the heap
    // carve-out itself, or page allocations can alias the heap arena and
    // overwrite allocator metadata through direct-map kernel aliases.
    inline constexpr U64 PhysicalPagePoolBase = EarlyHeapPhysicalBase + EarlyHeapSize;
    inline constexpr U64 PhysicalPagePoolSize = (ExtraRamBase + ExtraRamSize) - PhysicalPagePoolBase;

} // namespace mm::board_config

#endif // KERNEL_INCLUDE_MM_BOARD_VIRT_MM_BOARD_H