#include "memory.h"
#include "device.h"

extern unsigned long int    _end;
static unsigned int         num_pages;
static PAGE                 *page_array;
struct PAGE_LIST            free_pages;
static HEAP_SEGMENT         *heap_segment_list_head;


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
void memory_init_paging(){
    register long int
                        mem_size,  
                        page_array_len, 
                        kernel_pages, 
                        page_array_end,
                        i;
}