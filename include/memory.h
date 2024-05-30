//
// Created by Thang Cao on 5/26/24.
//

#ifndef RASPI3_OS_MEMORY_H
#define RASPI3_OS_MEMORY_H

#define VA_START            0xFFFF000000000000
#define LOW_MEMORY_CEILING  0x200000                // first 2MB is for low memory.
                                                    // This is for our kernel resides, its heap and stacks
#define VA_HIGH_MEMORY      (VA_START + LOW_MEMORY_CEILING)
#define PAGE_SIZE           0x1000                  // 1 page frame = 4KB
#define KERNEL_HEAP_SIZE    0x1000000               // kernel heap size is 16MB
#define KERNEL_ADDR_SPACE(addr) ((addr) + VA_START)


#ifndef __ASSEMBLER__

#include "ros.h"

typedef struct {
        BOOLEAN allocated: TRUE;
        BOOLEAN kernel_page: TRUE;
        unsigned int   reserved: 30;
} PAGE_FLAGS, page_flags_t;

typedef struct PAGE {
        unsigned long int           vaddr_mapped;   // the virtual address mapped to this page
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


void init_memory_management();                      // memory.c




#endif
#endif //RASPI3_OS_MEMORY_H
