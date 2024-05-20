#ifndef	_MM_H
#define	_MM_H
#include "peripherals/base.h"

#define VA_KERNEL_START 0xC0000000
#define VA_KERNEL_END   0xFFFFFFFF
#define VA_USER_START   0x00000000
#define VA_USER_END     0xB0000000
#define VA_PERIPHERALS  0xF2000000

#define VA_START 		0xffff000000000000      // We will use kernel address space. This is to select TLBR1

#define PHYS_MEMORY_SIZE 		0x40000000      // 1Gb memory Raspberry Pi 3

#define PAGE_MASK			0xfffffffffffff000  // mask the address to get page base address
#define PAGE_SHIFT	 		12                  // number of bits to index a page content
#define TABLE_SHIFT 			9               // number of bits to index a table content
#define SECTION_SHIFT			(PAGE_SHIFT + TABLE_SHIFT)  // 2M indexing

#define PAGE_SIZE   			(1 << PAGE_SHIFT)   // the page size in byte. Should be 4K
#define SECTION_SIZE			(1 << SECTION_SHIFT)    // section size in byte. Should be 2M

#define LOW_MEMORY              	(2 * SECTION_SIZE)  // first 4M will be low memory
#define HIGH_MEMORY             	DEVICE_BASE         // peripheral base start. Device MMIO takes about 15MB physical address space

#define PAGING_MEMORY 			(HIGH_MEMORY - LOW_MEMORY)  // our page start from 4M to 1008 MB
#define PAGING_PAGES 			(PAGING_MEMORY/PAGE_SIZE)

#define ENTRY_PER_TABLE			(1 << TABLE_SHIFT)

#define PGD_SHIFT			(PAGE_SHIFT + 3*TABLE_SHIFT)
#define PUD_SHIFT			(PAGE_SHIFT + 2*TABLE_SHIFT)
#define PMD_SHIFT			(PAGE_SHIFT + TABLE_SHIFT)

#define PG_DIR_SIZE			(3 * PAGE_SIZE)


/*
 * Memory region attributes:
 *
 *   n = AttrIndx[2:0]
 *            n    MAIR
 *   DEVICE_nGnRnE    000    00000000
 *   NORMAL_NC        001    01000100
 */
#define MT_DEVICE_nGnRnE            0x0 // slot 0 of mair
#define MT_NORMAL_NC                0x1 // slot 1 of mair
#define MT_DEVICE_nGnRnE_FLAGS      0x00
#define MT_NORMAL_NC_FLAGS          0x44
#define MAIR_VALUE     ((MT_DEVICE_nGnRnE_FLAGS << (8 * MT_DEVICE_nGnRnE)) | \
                        (MT_NORMAL_NC_FLAGS << (8 * MT_NORMAL_NC)))                 // modify me if memory structure change

#define MMU_FLAGS	 		(MM_TYPE_BLOCK | (MT_NORMAL_NC << 2) | MM_ACCESS)
#define MMU_DEVICE_FLAGS		(MM_TYPE_BLOCK | (MT_DEVICE_nGnRnE << 2) | MM_ACCESS)
#define MMU_PTE_FLAGS			(MM_TYPE_PAGE | (MT_NORMAL_NC << 2) | MM_ACCESS | MM_ACCESS_PERMISSION)

#define TCR_T0SZ			(64 - 48)
#define TCR_T1SZ			((64 - 48) << 16)
#define TCR_TG0_4K			(0 << 14)
#define TCR_TG1_4K			(2 << 30)
#define TCR_VALUE			(TCR_T0SZ | TCR_T1SZ | TCR_TG0_4K | TCR_TG1_4K)

#define MM_TYPE_PAGE_TABLE		0x3
#define MM_TYPE_PAGE 			0x3
#define MM_TYPE_BLOCK			0x1
#define MM_ACCESS			(0x1 << 10)
#define MM_ACCESS_PERMISSION		(0x01 << 6)

#ifndef __ASSEMBLER__

/**
 * get a free page base address
 * @return
 */
unsigned long get_free_page();

/**
 * free a page
 * @param p
 */
void free_page(unsigned long p);

/**
 * zero out dest address [count] of bytes
 * @param src_addr
 * @param count
 */
void memzero(unsigned long dest_addr, unsigned long count);

/**
 * copy from src_addr to dest_addr [count] number of bytes
 * @param dest_addr
 * @param src_addr
 * @param count
 */
void memcpy(unsigned char dest_addr, unsigned char src_addr, long count);

#endif

#endif  /*_MM_H */
