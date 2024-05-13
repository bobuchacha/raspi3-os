//
// Created by bobuc on 5/11/2024.
//
#include "array.h"
#include "string.h"
#include "../memory/paging.h"

#define ARRAY_BUFFER_SIZE 1          // size of buffer for array expand
M_CREATE_ARRAY(DynamicArray, void*)


/**
 * initialize new dynamic array
 * @param entry_size
 * @param initial_length {size of initial length}
 * @return pointer to dynamic array
 */
void *darray_init(int entry_size, int initial_length){
    int expansion_inc = (initial_length == 0) ? ARRAY_BUFFER_SIZE : initial_length;
    DynamicArray ptr = kmalloc(sizeof(DynamicArray) + (expansion_inc * entry_size));
    ptr->length = 0;
    ptr->_real_length = expansion_inc;      // we have ARRAY_BUFFER_SIZE entries left in buffer
    ptr->_expansion_increment = expansion_inc;
    ptr->_entry_size = entry_size;              // entry size, for expand
    return ptr;
}

/**
 * destroy the dynamic array
 * @param arr
 * @param ptr
 */
void darray_destroy(void* arr, void* ptr){
    kfree(ptr);
}

/**
 * get an entry
 * @param index
 */
void* darray_get(void* arr, int index){
    return ((DynamicArray)arr)->_base_addr[index];
}

/**
 * expand the array
 * @param arr, pointer to pointer of DynamicArray. The pointer will be changed
 */
void darray_expand(void* *arr){
    // allocate ARRAY_BUFFER_SIZE more for buffer
    DynamicArray _arr = *arr;
    int arrayCurrentSize = sizeof(DynamicArray) + _arr->_real_length * _arr->_entry_size;
    _arr = krealloc(_arr, arrayCurrentSize + (_arr->_entry_size * _arr->_expansion_increment));
    _arr->_real_length+= (_arr->_entry_size * _arr->_expansion_increment);
    _arr->_buffer_remaining += _arr->_expansion_increment;
}

void darray_add(void* arr, void* itemAddr){
    DynamicArray _arr = (DynamicArray) arr;

    if (_arr->_buffer_remaining < 1) darray_expand(&arr);
    memcpy(&_arr->_base_addr[_arr->_next_entry++], itemAddr, _arr->_entry_size);
}

/**
 * shift the whole buffer after index into index position
 * @param arr
 */
void darray_remove(void* _arr, int index){
    DynamicArray arr = (DynamicArray)_arr;
    // move entry to the left
    char* dest = arr->_base_addr[index++];
    char* src = arr->_base_addr[index];
    int length = (arr->_real_length - index - 1) * arr->_entry_size;
    memcpy(dest, src, length);
    // zero out the last entry
    bzero(arr->_base_addr[arr->_real_length-1], arr->_entry_size);
}