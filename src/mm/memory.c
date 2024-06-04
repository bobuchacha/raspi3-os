unsigned long int heap_start;

void mem_init_paging(unsigned long int *heap_start);                          // in paging.c
void mem_init_heap(unsigned long int);           // heap.c
void mem_dump_pagemap(int start, int count);     // paging.c: dump n entry of page map from starrt

void init_memory_management(){
    mem_init_paging(&heap_start);
    mem_init_heap(heap_start);

}