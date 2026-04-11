#define LOG_ENABLE_TRACE 0

#include "device.h"
#include "log.h"
#include "memory.h"
#include "ros.h"
#include "utils.h"

static unsigned int num_pages;
static PAGE* page_array;
struct PAGE_LIST free_pages;

PhysAddr mem_get_base(void) {
    return (PhysAddr)PHYS_MEMORY_BASE;
}

static void remove_page_list(struct PAGE_LIST* list, PAGE* node) {
    if (!list || !node) {
        return;
    }

    if (node->prev_page) {
        node->prev_page->next_page = node->next_page;
    }
    else {
        list->head = node->next_page;
    }

    if (node->next_page) {
        node->next_page->prev_page = node->prev_page;
    }
    else {
        list->tail = node->prev_page;
    }

    if (list->size > 0) {
        list->size -= 1;
    }

    node->next_page = NULL;
    node->prev_page = NULL;
}

void append_page_list(struct PAGE_LIST* list, PAGE* node) {
    if (!list || !node) {
        return;
    }

    node->next_page = NULL;
    if (list->tail != NULL) {
        list->tail->next_page = node;
        node->prev_page = list->tail;
    }
    else {
        node->prev_page = NULL;
        list->head = node;
    }
    list->tail = node;
    list->size += 1;
}

void push_page_list(struct PAGE_LIST* list, PAGE* node) {
    if (!list || !node) {
        return;
    }

    node->next_page = list->head;
    node->prev_page = ((void*)0);
    if (list->head != NULL) {
        list->head->prev_page = node;
    }
    list->head = node;
    list->size += 1;

    if (list->tail == ((void*)0)) {
        list->tail = node;
    }
}

struct PAGE* peek_page_list(struct PAGE_LIST* list) {
    return list->head;
}

struct PAGE* pop_page_list(struct PAGE_LIST* list) {
    if (!list || list->head == NULL) {
        return NULL;
    }

    struct PAGE* res = list->head;
    list->head = list->head->next_page;
    if (list->head != NULL) {
        list->head->prev_page = ((void*)0);
    }
    list->size -= 1;

    if (list->head == ((void*)0)) {
        list->tail = ((void*)0);
    }

    res->next_page = NULL;
    res->prev_page = NULL;

    return res;
}

unsigned int size_page_list(struct PAGE_LIST* list) {
    return list->size;
}

PAGE* next_page_list(struct PAGE* node) {
    return node->next_page;
}

unsigned long int mem_get_size() {
    return DEVICE_MEMORY_SIZE;
}

unsigned int mem_get_free_pages(void) {
    return size_page_list(&free_pages);
}

Bool mem_is_valid_phys_addr(PhysAddr phys_addr) {
    PhysAddr base = mem_get_base();
    PhysAddr limit = base + mem_get_size();

    return phys_addr >= base && phys_addr < limit;
}

Bool mem_is_valid_phys_page(PhysAddr phys_addr) {
    return mem_is_page_aligned(phys_addr) && mem_is_valid_phys_addr(phys_addr);
}

PAGE* mem_page_from_phys(PhysAddr phys_addr) {
    if (!mem_is_valid_phys_page(phys_addr)) {
        return NULL;
    }

    return &page_array[(phys_addr - mem_get_base()) / PAGE_SIZE];
}

UInt mem_get_page_refcount(Address ptr) {
    PAGE* page = mem_page_from_phys((PhysAddr)(ptr & MM_PAGE_MASK));

    if (!page) {
        return 0;
    }

    return page->ref_count;
}

/**
 * Initialize paging
 */
void mem_init_paging(unsigned long int* heap_start) {
    register int
        mem_size,
        page_array_len,
        page_array_pages,
        kernel_pages,
        heap_reserved_pages,
        i;
    PhysAddr phys_base;

    mem_size = mem_get_size();
    phys_base = mem_get_base();
    num_pages = mem_size / PAGE_SIZE;
    // we place paging right after LOW_MEMORY_CEILING

    _trace("Initialize paging. ");
    _trace_printf("     MEMORY SIZE: %d MB, PAGE SIZE: %d KB, TOTAL PAGES: %d. ", mem_size / 1024 / 1024, PAGE_SIZE / 1024, num_pages);
    _trace_printf("Size of page struct %d\n", sizeof(PAGE));

    // Allocate space for all those pages' metadata.  Start this block just after the src image is finished
    page_array_len = sizeof(PAGE) * num_pages;
    page_array_pages = mem_align_up((ULong)page_array_len) / PAGE_SIZE;
    page_array = (PAGE*)VA_HIGH_MEMORY;
    memzero((Address)page_array, page_array_len);

    // initialize the free page list
    free_pages.head = free_pages.tail = NULL;
    free_pages.size = 0;

    _trace("Page Array ");
    _trace_printf("     length %d bytes, starting at 0x%lX\n", page_array_len, page_array);

    // Iterate over all pages and mark them with the appropriate flags
    // Start with src pages, and the paging metadata
    kernel_pages = LOW_MEMORY_CEILING / PAGE_SIZE;
    heap_reserved_pages = mem_align_up((ULong)KERNEL_HEAP_SIZE) / PAGE_SIZE;

    for (i = 0; i < kernel_pages; i++) {
        page_array[i].phys_addr = phys_base + (i * PAGE_SIZE);
        page_array[i].ref_count = 1;
        page_array[i].flags.allocated = 1;
        page_array[i].flags.kernel_page = 1;
    }

    // map the paging metadata
    for (; i < (kernel_pages + page_array_pages); i++) {
        page_array[i].phys_addr = phys_base + (i * PAGE_SIZE);
        page_array[i].ref_count = 1;
        page_array[i].flags.allocated = 1;
        page_array[i].flags.kernel_page = 1;
    }

    // map the heap region
    for (; i < (kernel_pages + page_array_pages + heap_reserved_pages); i++) {
        page_array[i].phys_addr = phys_base + (i * PAGE_SIZE);
        page_array[i].ref_count = 1;
        page_array[i].flags.allocated = 1;
        page_array[i].flags.kernel_page = 1;
    }

    // Map the rest of the pages as unallocated, and add them to the free list
    for (; i < num_pages; i++) {
        page_array[i].flags.allocated = 0;
        page_array[i].ref_count = 0;
        page_array[i].phys_addr = phys_base + (i * PAGE_SIZE);
        append_page_list(&free_pages, &page_array[i]);
    }

    *heap_start = mem_align_up((ULong)page_array + (ULong)page_array_len); // start the heap on a clean page boundary
}

/**
 * dumps n entries of page map from start
 * @param start
 * @param count
 */
void mem_dump_pagemap(int start, int count) {
    printf("============================== [PAGE MAP DUMP] ==============================\n");
    for (register int i = start; i < (start + count); i++) {
        if (i < (start + count))
            printf(" - Page %d        Address: 0x%lX   refs=%d   %s\n", i, page_array[i].phys_addr, page_array[i].ref_count, page_array[i].flags.allocated ? "Used" : "");
    }
}

/**
 * allocate a free page, and return its physical address.
 */
Address mem_alloc_page(void) {
    page_t* page;
    Address page_mem;

    // _trace("Allocating new page...\n");

    if (size_page_list(&free_pages) == 0)
        return 0;

    // Get a free page
    page = pop_page_list(&free_pages);
    if (!page)
        return 0;
    //        if (!page==0) return 0;

    page->flags.kernel_page = 1;
    page->flags.allocated = 1;
    page->ref_count = 1;

    // Get the address the physical page metadata refers to
    page_mem = page->phys_addr;

    // _tracef("Got a page number %d at %lx",page-page_array, page_mem);

    // Zero out the page, big security flaw to not do this :)
    memzero((Address)mem_phys_to_virt(page_mem), PAGE_SIZE);

    // _trace("Located page ");
    // _trace_printf("at 0x%x, address 0x%x\n", page, page_mem);

    return page_mem; // physical address of the page
}

void mem_retain_page(Address ptr) {
    PAGE* page = mem_page_from_phys((PhysAddr)(ptr & MM_PAGE_MASK));

    if (!page) {
        log_error("Attempted to retain invalid physical page 0x%lX", ptr);
        return;
    }

    page->flags.allocated = 1;
    page->ref_count += 1;
}

/**
 * Free page
 */
void mem_free_page(Address ptr) {
    page_t* page = mem_page_from_phys((PhysAddr)(ptr & MM_PAGE_MASK));

    if (!page) {
        log_error("Attempted to free invalid physical page 0x%lX", ptr);
        return;
    }

    if (page->ref_count == 0) {
        log_warning("Double free detected for physical page 0x%lX", ptr);
        return;
    }

    page->ref_count -= 1;
    if (page->ref_count > 0) {
        _trace("Released PA \x1b[32m0x%lX\x1b[0m, refs=%d", ptr & MM_PAGE_MASK, page->ref_count);
        return;
    }

    // _trace("Freeing 0x%lX", ptr);
    // _trace_printf("page %lX at 0x%lX. Status: %s\n\n", page->phys_addr, ptr, page->flags.allocated ? "Used" : "");
    // Mark the page as free
    page->flags.allocated = 0;
    page->flags.kernel_page = 0;
    append_page_list(&free_pages, page);
    _trace("Unmapped PA \x1b[32m0x%lX\x1b[0m", ptr);
}
