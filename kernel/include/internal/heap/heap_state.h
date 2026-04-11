#ifndef KERNEL_INTERNAL_HEAP_HEAP_STATE_H
#define KERNEL_INTERNAL_HEAP_HEAP_STATE_H

#include "heap.h"

typedef struct HeapBlock {
    Size length;
    bool is_free;
    struct HeapBlock* next;
    struct HeapBlock* prev;
} HeapBlock;

typedef struct HeapState {
    void* base;
    Size length;
    HeapBlock* head;
} HeapState;

#endif // KERNEL_INTERNAL_HEAP_HEAP_STATE_H
