#include "ros.h"
#include "list.h"
#include "../atag.h"

#ifndef MEM_H
#define MEM_H

#define PAGE_SIZE 4096
#define KERNEL_HEAP_SIZE (1024*1024*32)         // reserve 32 MB for kernel heap

typedef struct {
        BOOLEAN allocated: TRUE;
        BOOLEAN kernel_page: TRUE;
        INT32   reserved: 30;
} PAGE_FLAGS, page_flags_t;

typedef struct PAGE {
        INT32           vaddr_mapped;   // the virtual address mapped to this page
        PAGE_FLAGS      flags;
        struct PAGE*           next_page;
        struct PAGE*           prev_page;
}PAGE, page_t;

typedef struct PAGE_LIST { 
        struct PAGE *  head; 
        struct PAGE *  tail; 
        INT32   size;
} PAGE_LIST, page_list_t;

typedef struct heap_segment{
    struct heap_segment * next;
    struct heap_segment * prev;
    INT32 is_allocated;
    INT32 segment_size;  // Includes this header
} HEAP_SEGMENT, heap_segment_t;

INT mem_get_size(atag_t* atags);
void mem_init_paging(atag_t * atags);

void * mem_alloc_page(void);
void mem_free_page(void * ptr);
static void heap_init(INT heap_start);
void * kmalloc(INT32 bytes);
void * krealloc(void *ptr, INT32 bytes);
void kfree(void *ptr);

#endif