#include "../../../include/ros.h"
#include "paging.h"
#include "../hardware/uart/uart.h"
#include "../lib/string.h"

extern INT _end;
static INT32 num_pages;
static PAGE * all_pages_array;
struct PAGE_LIST free_pages;
static heap_segment_t * heap_segment_list_head;

extern void init_vmm();

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

INT size_page_list(struct PAGE_LIST * list) { return list->size; } 
PAGE * next_page_list(struct PAGE * node) { return node->next_page; }


INT mem_get_size(atag_t* atags){
        //TODO: implement atags here
        return 1024*1024*1024;          // 1GB
}

void mem_init_paging(atag_t * atags){
        int   mem_size,  
                page_array_len, 
                kernel_pages, 
                page_array_end,
                i;

                // Get the total number of pages
        mem_size = mem_get_size(atags);
        num_pages = mem_size / PAGE_SIZE;    

        printf("------------------------------------\n");
        printf("TOTAL MEMORY SIZE 0x%x, total pages 0x%x, page size 0x%x\n", mem_size, num_pages, PAGE_SIZE);   

        printf("Size of heap struct %d\n", sizeof(heap_segment_t));


        // Allocate space for all those pages' metadata.  Start this block just after the src image is finished
        page_array_len = sizeof(page_t) * num_pages;
        all_pages_array = (page_t *)&_end;
        memset((void *) all_pages_array, 0, page_array_len);
        free_pages.head = free_pages.tail = (void *)0; 
        free_pages.size = 0;

        printf("Page Array Length: 0x%x starting at 0x%x\n", page_array_len, all_pages_array);

        // Iterate over all pages and mark them with the appropriate flags
        // Start with src pages, and the paging metadata
        kernel_pages = ((INT)&_end) / PAGE_SIZE;
;
        for (i = 0; i < kernel_pages; i++) {
                all_pages_array[i].vaddr_mapped = i * PAGE_SIZE;    // Identity map the src pages
                all_pages_array[i].flags.allocated = 1;
                all_pages_array[i].flags.kernel_page = 1;
        }

        // map the paging metadata
        for (; i < (kernel_pages + page_array_len / PAGE_SIZE); i++){
                all_pages_array[i].vaddr_mapped = i * PAGE_SIZE;    // Identity map the src pages
                all_pages_array[i].flags.allocated = 1;
                all_pages_array[i].flags.kernel_page = 1;
        }

        // map the heap region
        for (; i < (kernel_pages + (page_array_len / PAGE_SIZE) + (KERNEL_HEAP_SIZE / PAGE_SIZE)); i++){
                // printf("mapping page %d for HEAP\n", i);
                all_pages_array[i].vaddr_mapped = i * PAGE_SIZE;    // Identity map the src pages
                all_pages_array[i].flags.allocated = 1;
                all_pages_array[i].flags.kernel_page = 1;
        }

        // Map the rest of the pages as unallocated, and add them to the free list
        for(; i < num_pages; i++){
                all_pages_array[i].flags.allocated = 0;
                append_page_list(&free_pages, &all_pages_array[i]);
        }

        // Initialize the heap
        page_array_end = (INT)&_end + page_array_len;
        heap_init(page_array_end);

        // initialize the virtual memory
        init_vmm();
}

static void heap_init(INT heap_start) {
        printf("Heap intialized at 0x%d. Total size: %d KB. Size of heap segment is %d\n", heap_start, KERNEL_HEAP_SIZE / 1024, sizeof(heap_segment_t));
    heap_segment_list_head = (heap_segment_t *) heap_start;
    bzero(heap_segment_list_head, sizeof(heap_segment_t));
    heap_segment_list_head->segment_size = KERNEL_HEAP_SIZE;
}

void * mem_alloc_page(void){
        page_t * page;
        void * page_mem;

        if (size_page_list(&free_pages) == 0)
        return 0;


        // Get a free page
        page = pop_page_list(&free_pages);
//        if (!page==0) return 0;
        

        page->flags.kernel_page = 1;
        page->flags.allocated = 1;

        // Get the address the physical page metadata refers to
        page_mem = (void *)((page - all_pages_array) * PAGE_SIZE);



        // Zero out the page, big security flaw to not do this :)
        memset((void *) page_mem, 0, PAGE_SIZE);

        printf("---> mem_alloc_page:: Locating page at 0x%x, address 0x%x\n", page, page_mem);
        
        return page_mem;
}

void mem_free_page(void * ptr){
        page_t * page;

        // Get page metadata from the physical address
        page = all_pages_array + ((INT)ptr / PAGE_SIZE);
        //Mark the page as free
        page->flags.allocated = 0;
        append_page_list(&free_pages, page);
}

void * krealloc(void *ptr, INT32 bytes){

        kdebug("krealloc: Reallocating 0x%x to get new %ld bytes... ", ptr, bytes);

        if(ptr == NULL) {
                kdebug("  -> Not allocated. Allocating new segment");
                return kmalloc(bytes);
        }
        heap_segment_t * header = ptr - sizeof(heap_segment_t);
        void *newchunk = kmalloc(bytes);                        // allocate new chunk
        if(newchunk == NULL) return NULL;                       // Don't know if this can actually happen
        memcpy(newchunk, ptr, header->segment_size);            // copy data from old chunk to new
        kfree(ptr);                                             // free old trunk
        return newchunk;                                        // return new trunk
}

void * kmalloc(INT32 bytes) {
    heap_segment_t * curr, *best = NULL;
    int diff, best_diff = 0x7fffffff; // Max signed int

    kdebug("kmalloc: Allocating %ld bytes... ", bytes);

    // Add the header to the number of bytes we need and make the size 4 byte aligned
    bytes += sizeof(heap_segment_t);
    bytes += bytes % 16 ? 16 - (bytes % 16) : 0;

    // Find the allocation that is closest in size to this request
    for (curr = heap_segment_list_head; curr != NULL; curr = curr->next) {
//       kdebug("---> curr: 0x%x, next 0x%x, address of next 0x%x", curr, curr->next, &curr->next); // some readon curr gets to a really big number curr: 0x202020317E554E5F;

        diff = curr->segment_size - bytes;
        if (!curr->is_allocated && diff < best_diff && diff >= 0) {
            best = curr;
            best_diff = diff;
        }
    }


    // There must be no free memory right now :(
    if (best == NULL){
        kerror("kmalloc: Heap not available for %d bytes", bytes);
        return NULL;
    }

    // If the best difference we could come up with was large, split up this segment into two.
    // Since our segment headers are rather large, the criterion for splitting the segment is that
    // when split, the segment not being requested should be twice a header size
    if (best_diff > (int)(2 * sizeof(heap_segment_t))) {
        bzero(((void*)(best)) + bytes, sizeof(heap_segment_t));
        curr = best->next;
        best->next = ((void*)(best)) + bytes;
        best->next->next = curr;
        best->next->prev = best;
        best->next->segment_size = best->segment_size - bytes;
        best->segment_size = bytes;
    }
        kdebug("  -> Allocated at address: 0x%x", best+1);
    best->is_allocated = 1;

    return best + 1;    // after segment metadata
}

INT64 pointer_to_int(void* ptr){
        INT64 *u=(void*)&ptr;
        return *u;
}

void kfree(void *ptr) {
        if (!ptr)
                return;

        kdebug("kfree: Free heap at address: 0x%x", ptr);
        heap_segment_t * seg = ptr - sizeof(heap_segment_t);
        seg->is_allocated = 0;

        // try to coalesce segements to the left
        while(seg->prev != NULL && !seg->prev->is_allocated) {
                seg->prev->next = seg->next;
                //kinfo("kfree: setting previous segment's next pointer to 0x%x", seg->next);
                seg->prev->segment_size += seg->segment_size;
                seg = seg->prev;
//                kdebug("  -> Merge to left free segment at: 0x%x", seg->prev);
        }
        // try to coalesce segments to the right
        while(seg->next != NULL && !seg->next->is_allocated) {
                seg->segment_size += seg->next->segment_size;
                seg->next = seg->next->next;
            //kinfo("kfree: setting next segment's next pointer to 0x%x", seg->next->next);
//                kdebug("  -> Merge to right free segment at: 0x%x", seg->next);
        }
}
