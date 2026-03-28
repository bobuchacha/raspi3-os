//
// Created by Thang Cao on 5/26/24.
//

#ifndef RASPI3_OS_MEMORY_H
#define RASPI3_OS_MEMORY_H

#define VA_START            0xFFFF000000000000

#define LOW_MEMORY_CEILING  0x800000                // first 8MB is reserved for boot images,
                                                    // boot stacks, and early allocator state

#define VA_HIGH_MEMORY      (VA_START + LOW_MEMORY_CEILING)
#define PAGE_SIZE           0x1000                  // 1 page frame = 4KB
#define KERNEL_HEAP_SIZE    0x1000000               // kernel heap size is 16MB
#define KERNEL_ADDR_SPACE(addr) ((addr) + VA_START)

/*
 * Userspace leaves a few guard pages unmapped at the bottom of the address
 * space, then places the first executable image at the next page-aligned base.
 * With 4 KB pages this stays at 0x4000, and with 64 KB pages it becomes 0x40000.
 */
#define USER_NULL_GUARD_PAGES 4UL
#define VA_USER_START       (USER_NULL_GUARD_PAGES * PAGE_SIZE)
#define VA_USER_STACK       VA_USER_START

#ifndef __ASSEMBLER__

#include "ros.h"

typedef ULong PhysAddr;
typedef ULong VirtAddr;

typedef struct {
        Bool         allocated : true;
        Bool         kernel_page : true;
        unsigned int reserved : 30;
} PAGE_FLAGS, page_flags_t;

typedef struct PAGE {
        unsigned long int           phys_addr;   // the virtual address mapped to this page
        UInt                   ref_count;
        PAGE_FLAGS             flags;
        struct PAGE* next_page;
        struct PAGE* prev_page;
}PAGE, page_t;

typedef struct PAGE_LIST {
        struct PAGE* head;
        struct PAGE* tail;
        unsigned int   size;
} PAGE_LIST, page_list_t;

typedef struct __attribute((__packed__, aligned(16))) heap_segment {
        struct heap_segment* next;
        struct heap_segment* prev;
        unsigned int is_allocated;
        unsigned int segment_size;  // Includes this header
} HEAP_SEGMENT, heap_segment_t;

/** some helpers so we dont need expensive functions */
#define mem_get_pagenum(va)     (((unsigned long)va - VA_START) / PAGE_SIZE)

static inline VirtAddr mem_phys_to_virt(PhysAddr phys_addr) {
        return (VirtAddr)(phys_addr + VA_START);
}

static inline PhysAddr mem_virt_to_phys(VirtAddr virt_addr) {
        return (PhysAddr)(virt_addr - VA_START);
}

static inline Bool mem_is_page_aligned(ULong addr) {
        return (addr & (PAGE_SIZE - 1)) == 0;
}

static inline ULong mem_align_down(ULong addr) {
        return addr & ~(PAGE_SIZE - 1);
}

static inline ULong mem_align_up(ULong addr) {
        return (addr + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
}

static inline Bool mem_is_kernel_virt_addr(VirtAddr virt_addr) {
        return virt_addr >= (VirtAddr)VA_START;
}

void init_memory_management();                      // memory.c
void mem_dump_pagemap(int start, int count);        // dump a page map
unsigned long int mem_get_size(void);               // total physical memory size in bytes
unsigned int mem_get_free_pages(void);              // number of currently free physical pages
Bool mem_is_valid_phys_addr(PhysAddr phys_addr);    // address is inside physical memory
Bool mem_is_valid_phys_page(PhysAddr phys_addr);    // address is a page-aligned physical page
PAGE* mem_page_from_phys(PhysAddr phys_addr);       // page metadata entry for a physical address
unsigned long mem_heap_total_bytes(void);           // total kernel heap span managed by kmalloc
unsigned long mem_heap_used_bytes(void);            // bytes currently held by allocated heap segments
unsigned long mem_heap_free_bytes(void);            // bytes currently available in free heap segments
void mem_heap_dump(int count);                      // dump heap segments for allocator debugging
Address mem_alloc_page(void);                        // allocate a page in physical memory
void mem_retain_page(Address ptr);                   // increment page reference count
void mem_free_page(Address  ptr);                    // release one reference to a page
UInt mem_get_page_refcount(Address ptr);            // current reference count for a page
Address kmalloc(int bytes);                              // allocate a memory
Address krealloc(Address ptr, unsigned int bytes);         // reallocate a memory
void kfree(Address ptr);                                  // free a memory

#endif
#endif //RASPI3_OS_MEMORY_H
