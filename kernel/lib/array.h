//
// Created by bobuc on 5/11/2024.
//

#ifndef KERNEL_LIB_ARRAY_H
#define KERNEL_LIB_ARRAY_H
#define M_CREATE_ARRAY(name, type) typedef struct { \
    int length;        /* number of entry in buffer*/                             \
    int _entry_size;   /* entry size - for expand process*/                             \
    int _next_entry;   /* next entry in buffer*/                            \
    int _real_length;  /* real length of the buffer*/                                              \
    int _buffer_remaining; /* remaining number of buffer before expansion*/                    \
    int _expansion_increment; /* number of entries should we add everytime we expand */                                                \
    type _base_addr[];} __attribute((__packed__, aligned(16))) *name;\
// end define

/**
 * shift the whole buffer after index into index position
 * @param arr
 */
void darray_remove(void* arr, int index);

void darray_add(void* arr, void* itemAddr);

/**
 * expand the array
 * @param arr, pointer to pointer of DynamicArray. The pointer will be changed
 */
void darray_expand(void* *arr);

/**
 * get an entry
 * @param index
 */
void* darray_get(void* arr, int index);

/**
 * destroy the dynamic array
 * @param arr
 * @param ptr
 */
void darray_destroy(void* arr, void* ptr);

/**
 * initialize new dynamic array
 * @param entry_size
 * @param initial_length {size of initial length}
 * @return pointer to dynamic array
 */
void *darray_init(int entry_size, int initial_length);
#endif //KERNEL_LIB_ARRAY_H
