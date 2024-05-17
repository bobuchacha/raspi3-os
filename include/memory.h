#ifndef	_MM_H
#define	_MM_H

#define PAGE_SHIFT	 	12
#define TABLE_SHIFT 		9
#define SECTION_SHIFT		(PAGE_SHIFT + TABLE_SHIFT)

#define PAGE_SIZE   		(1 << PAGE_SHIFT)
#define SECTION_SIZE		(1 << SECTION_SHIFT)

#define LOW_MEMORY              (2 * SECTION_SIZE)
#define HIGH_MEMORY             PBASE

#define PAGING_MEMORY           (HIGH_MEMORY - LOW_MEMORY)
#define PAGING_PAGES            (PAGING_MEMORY/PAGE_SIZE)

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
