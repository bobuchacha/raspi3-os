//
// Created by Thang Cao on 5/26/24.
//

#ifndef RASPI3_OS_MEMORY_H
#define RASPI3_OS_MEMORY_H

#define VA_START            0xFFFF000000000000
#define VA_USER_START       0x4000                      // user programm start virtual address
#define VA_USER_STACK       0x4000                      // user programm stack size. This grows below program start

#define LOW_MEMORY_CEILING  0x200000                // first 2MB is for low memory.
                                                    // This is for our kernel resides, its heap and stacks
                                                    
#define VA_HIGH_MEMORY      (VA_START + LOW_MEMORY_CEILING)
#define PAGE_SIZE           0x1000                  // 1 page frame = 4KB
#define KERNEL_HEAP_SIZE    0x1000000               // kernel heap size is 16MB
#define KERNEL_ADDR_SPACE(addr) ((addr) + VA_START)

#ifndef __ASSEMBLER__

#include "ros.h"

typedef struct {
        Bool         allocated: true;
        Bool         kernel_page: true;
        unsigned int reserved: 30;
} PAGE_FLAGS, page_flags_t;

typedef struct PAGE {
        unsigned long int           phys_addr;   // the virtual address mapped to this page
        PAGE_FLAGS             flags;
        struct PAGE*           next_page;
        struct PAGE*           prev_page;
}PAGE, page_t;

typedef struct PAGE_LIST { 
        struct PAGE *  head; 
        struct PAGE *  tail;
        unsigned int   size;
} PAGE_LIST, page_list_t;

typedef struct __attribute((__packed__, aligned(16))) heap_segment{
    struct heap_segment * next;
    struct heap_segment * prev;
    unsigned int is_allocated;
    unsigned int segment_size;  // Includes this header
} HEAP_SEGMENT, heap_segment_t;

/** some helpers so we dont need expensive functions */
#define mem_get_pagenum(va)     (((unsigned long)va - VA_START) / PAGE_SIZE)

void init_memory_management();                      // memory.c
void mem_dump_pagemap(int start, int count);        // dump a page map
void * mem_alloc_page(void);                        // allocate a page in physical memory
void mem_free_page(void * ptr);                     // free a page
void * kmalloc(int bytes);                              // allocate a memory
void * krealloc(void *ptr, unsigned int bytes);         // reallocate a memory
void kfree(void *ptr);                                  // free a memory

#endif
#endif //RASPI3_OS_MEMORY_H
