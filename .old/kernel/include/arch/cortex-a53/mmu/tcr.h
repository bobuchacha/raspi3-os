#ifndef TCR_H
#define TCR_H

#define TCR_MISS_NO_FAULT	((unsigned long long)0x0 << 7)
#define TCR_MISS_FAULT		((unsigned long long)0x1 << 7)

#define TCR_TT0_GRANULE_4KB	((unsigned long long)0x0 << 14)
#define TCR_TT0_GRANULE_64KB	((unsigned long long)0x1 << 14)
#define TCR_TT0_GRANULE_16KB	((unsigned long long)0x2 << 14)

#define TCR_TT1_GRANULE_16KB	((unsigned long long)0x1 << 30)
#define TCR_TT1_GRANULE_4KB	((unsigned long long)0x2 << 30)
#define TCR_TT1_GRANULE_64KB	((unsigned long long)0x3 << 30)

#define TCR_NON_SHAREABLE	((unsigned long long)0x0 << 12)
#define TCR_OUTER_SHAREABLE	((unsigned long long)0x2 << 12)
#define TCR_INNER_SHAREABLE	((unsigned long long)0x3 << 12)

#define TCR_NON_CACHEABLE	(unsigned long long)0x0
#define TCR_CACHEABLE_WB_WA	(unsigned long long)0x1
#define TCR_CACHEABLE_WT_NWA	(unsigned long long)0x2
#define TCR_CACHEABLE_WB_NWA	(unsigned long long)0x3

#define TCR_IPA_32BIT		((unsigned long long)0x0 << 32)
#define TCR_IPA_36BIT		((unsigned long long)0x1 << 32)
#define TCR_IPA_40BIT		((unsigned long long)0x2 << 32)
#define TCR_IPA_42BIT		((unsigned long long)0x3 << 32)
#define TCR_IPA_44BIT		((unsigned long long)0x4 << 32)
#define TCR_IPA_48BIT		((unsigned long long)0x5 << 32)
#define TCR_IPA_52BIT		((unsigned long long)0x6 << 32)

#define TCR_TOP_BYTE_USED	((unsigned long long)0x0 << 37)
#define TCR_TOP_BYTE_IGNORED	((unsigned long long)0x1 << 37)

#define TCR_ASID_TTBR0		((unsigned long long)0x0 << 22)
#define TCR_ASID_TTBR1		((unsigned long long)0x1 << 22)

#define TCR_ASID_8BIT		((unsigned long long)0x0 << 36)
#define TCR_ASID_16BIT		((unsigned long long)0x1 << 36)

#define TCR_EPD1_TTBR1_DISABLED (1ULL << 23ULL)

#endif