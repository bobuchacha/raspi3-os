unsigned long int heap_start;

void memory_init_paging(unsigned long int *heap_start);                          // in paging.c
void memory_init_heap(unsigned long int);           // heap.c
void memory_dump_pagemap(int start, int count);     // paging.c: dump n entry of page map from starrt

void init_memory_management(){
    memory_init_paging(&heap_start);
    memory_init_heap(heap_start);
}