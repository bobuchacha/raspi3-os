//
// Created by bobuc on 5/11/2024.
//
#include "array.h"
#include "../memory/paging.h"

M_CREATE_ARRAY(_dummy, void*)

void *array_init(int headerSize, int entry_size){
    _dummy ptr = kmalloc(headerSize);
    ptr->_entry_size = entry_size;
    return ptr;
}

void array_destroy(void* ptr){
    kfree(ptr);
}
