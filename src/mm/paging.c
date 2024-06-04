#include "ros.h"
#include "memory.h"
#include "device.h"
#include "log.h"
#include "utils.h"


#define _trace log_info
#define _trace_printf printf

static unsigned int         num_pages;
static PAGE                 *page_array;
struct PAGE_LIST            free_pages;

void append_page_list(struct PAGE_LIST * list, PAGE * node) {
        list->tail->next_page = node; 
        node->prev_page = list->tail; 
        list->tail = node; 
        node->next_page = ((void *)0); 
        list->size += 1; 
        
        if (list->head == ((void *)0)) { 
                list->head = node; 
        } 
} 

void push_page_list(struct PAGE_LIST * list, PAGE * node) { 
        node->next_page = list->head; 
        node->prev_page = ((void *)0); 
        list->head = node; 
        list->size += 1; 
        
        if (list->tail == ((void *)0)) { list->tail = node; } 
} 

struct PAGE * peek_page_list(struct PAGE_LIST * list) { 
        return list->head; 
} 

struct PAGE * pop_page_list(struct PAGE_LIST * list) { 
        struct PAGE * res = list->head; 
        list->head = list->head->next_page; 
        list->head->prev_page = ((void *)0); 
        list->size -= 1; 
        
        if (list->head == ((void *)0)) { list->tail = ((void *)0); }
        
         return res; 
} 

unsigned int size_page_list(struct PAGE_LIST * list) { return list->size; } 
PAGE * next_page_list(struct PAGE * node) { return node->next_page; }
unsigned long int mem_get_size(){ return DEVICE_MEMORY_SIZE; }

/**
 * Initialize paging
 */
void mem_init_paging(unsigned long int *heap_start){
    register int
                        mem_size,  
                        page_array_len, 
                        kernel_pages, 
                        page_array_end,
                        i;

    mem_size = mem_get_size();
    num_pages = mem_size / PAGE_SIZE;
    // we place paging right after LOW_MEMORY_CEILING

    _trace("Initialize paging. ");
    _trace_printf("MEMORY SIZE: %d MB, PAGE SIZE: %d KB, TOTAL PAGES: %d. ", mem_size / 1024 / 1024, PAGE_SIZE / 1024, num_pages);
    _trace_printf("Size of heap struct %d\n", sizeof(HEAP_SEGMENT));

    // Allocate space for all those pages' metadata.  Start this block just after the src image is finished
    page_array_len = sizeof(PAGE) * num_pages;
    page_array = (PAGE *)VA_HIGH_MEMORY;
    memzero((void *) page_array, page_array_len);

    // initialize the free page list
    free_pages.head = free_pages.tail = NULL;
    free_pages.size = 0;

    _trace("Page Array ");
    _trace_printf("length %d bytes, starting at 0x%lX\n", page_array_len, page_array);

    // Iterate over all pages and mark them with the appropriate flags
    // Start with src pages, and the paging metadata
    kernel_pages = LOW_MEMORY_CEILING / PAGE_SIZE;

    for (i = 0; i < kernel_pages; i++) {
        page_array[i].vaddr_mapped = (i * PAGE_SIZE);    // Identity map the src pages
        page_array[i].flags.allocated = 1;
        page_array[i].flags.kernel_page = 1;
    }

    // map the paging metadata
    for (; i < (kernel_pages + page_array_len / PAGE_SIZE); i++){
        page_array[i].vaddr_mapped = (i * PAGE_SIZE);    // Identity map the src pages
        page_array[i].flags.allocated = 1;
        page_array[i].flags.kernel_page = 1;
    }

    // map the heap region
    for (; i < (kernel_pages + (page_array_len / PAGE_SIZE) + (KERNEL_HEAP_SIZE / PAGE_SIZE)); i++){
        page_array[i].vaddr_mapped = (i * PAGE_SIZE);    // Identity map the src pages
        page_array[i].flags.allocated = 1;
        page_array[i].flags.kernel_page = 1;
    }

    // Map the rest of the pages as unallocated, and add them to the free list
    for(; i < num_pages; i++){
        page_array[i].flags.allocated = 0;
        append_page_list(&free_pages, &page_array[i]);
    }

    *heap_start = (unsigned long int)page_array + page_array_len;
}

/**
 * dumps n entries of page map from start
 * @param start
 * @param count
 */
void mem_dump_pagemap(int start, int count){
    printf("============================== [PAGE MAP DUMP] ==============================\n");
    for (register int i = start; i < count; i++) {
        if (i < count) printf(" - Page %d vaddr 0x%lX\n", i, page_array[i].vaddr_mapped);
    }
}

void * mem_alloc_page(void){
        page_t * page;
        void * page_mem;

        // _trace("Allocating new page...\n");

        if (size_page_list(&free_pages) == 0)
        return 0;

        // Get a free page
        page = pop_page_list(&free_pages);
//        if (!page==0) return 0;
        
        page->flags.kernel_page = 1;
        page->flags.allocated = 1;

        // Get the address the physical page metadata refers to
        page_mem = (void *)((page - page_array) * PAGE_SIZE);

        _trace("Got a page ");
        _trace_printf("at %x\n", page_mem);

        // Zero out the page, big security flaw to not do this :)
        memzero((void *) page_mem, PAGE_SIZE);

        // _trace("Located page ");
        // _trace_printf("at 0x%x, address 0x%x\n", page, page_mem);
        
        return page_mem;
}

void mem_free_page(void * ptr){
        page_t * page;

        // Get page metadata from the physical address
        page = page_array + ((int)ptr / PAGE_SIZE);
        //Mark the page as free
        page->flags.allocated = 0;
        append_page_list(&free_pages, page);
}

