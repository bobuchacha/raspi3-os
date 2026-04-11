#ifndef ROS_LOADER_TCR_H
#define ROS_LOADER_TCR_H

#define TCR_MISS_NO_FAULT ((unsigned long long)0x0 << 7)
#define TCR_MISS_FAULT ((unsigned long long)0x1 << 7)

#define TCR_TT0_GRANULE_4KB ((unsigned long long)0x0 << 14)
#define TCR_TT1_GRANULE_4KB ((unsigned long long)0x2 << 30)

#define TCR_INNER_SHAREABLE ((unsigned long long)0x3 << 12)

#define TCR_CACHEABLE_WB_WA (unsigned long long)0x1

#define TCR_IPA_32BIT ((unsigned long long)0x0 << 32)
#define TCR_TOP_BYTE_USED ((unsigned long long)0x0 << 37)
#define TCR_ASID_TTBR0 ((unsigned long long)0x0 << 22)
#define TCR_ASID_8BIT ((unsigned long long)0x0 << 36)
#define TCR_EPD1_TTBR1_DISABLED (1ULL << 23ULL)

#endif
