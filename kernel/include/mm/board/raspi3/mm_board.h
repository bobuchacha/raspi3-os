#ifndef KERNEL_INCLUDE_MM_BOARD_RASPI3_MM_BOARD_H
#define KERNEL_INCLUDE_MM_BOARD_RASPI3_MM_BOARD_H

#include "types.h"

namespace mm::board_config {

    inline constexpr U64 DeviceBase = 0x3F000000ULL;    // This is where device lives
    inline constexpr U64 DeviceLimit = 0x40000000ULL;    // This is the end of the device memory range
    inline constexpr U64 ExtraRamBase = 0x0ULL;    // This is where extra RAM starts
    // The Raspberry Pi path still lacks firmware-provided free-memory regions in
    // the new boot protocol, so reserve one conservative low-RAM arena for the
    // migrated early heap until the board memory map is wired through.
    inline constexpr U64 EarlyHeapPhysicalBase = 0x02000000ULL;
    inline constexpr U64 EarlyHeapSize = 0x04000000ULL;
    // Keep page allocations above the dedicated heap carve-out so kernel page
    // mappings and heap metadata never alias the same physical pages.
    inline constexpr U64 PhysicalPagePoolBase = EarlyHeapPhysicalBase + EarlyHeapSize;
    inline constexpr U64 PhysicalPagePoolSize = DeviceBase - PhysicalPagePoolBase;

} // namespace mm::board_config

#endif // KERNEL_INCLUDE_MM_BOARD_RASPI3_MM_BOARD_H