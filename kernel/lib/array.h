//
// Created by bobuc on 5/11/2024.
//

#ifndef KERNEL_LIB_ARRAY_H
#define KERNEL_LIB_ARRAY_H
#define M_CREATE_ARRAY(name, type) typedef struct { \
    int length;\
    int _entry_size;\
    type baseAddr[];} __attribute((__packed__, aligned(16))) *name;\
// end define

#endif //KERNEL_LIB_ARRAY_H
