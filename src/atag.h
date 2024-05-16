
#ifndef ATAG_H
#define ATAG_H

#include "ros.h"

typedef enum {
    NONE = 0x00000000,
    CORE = 0x54410001,
    MEM = 0x54410002,
    INITRD2 = 0x54420005,
    CMDLINE = 0x54410009,
} ATAG_TAG, atag_tag_t;

typedef struct {
    INT32 size;
    INT32 start;
} MEMORY_STRUCT, mem_t;

typedef struct {
    INT32 start;
    INT32 size;
} INITRD2_STRUCT, initrd2_t;

typedef struct {
    char line[1];
} CMDLINE_STRUCT, cmdline_t;

typedef struct atag {
    INT32 tag_size;
    atag_tag_t tag;
    union {
        mem_t mem;
        initrd2_t initrd2;
        cmdline_t cmdline;
    };
} atag_t;

INT32 get_mem_size(atag_t * atags);

#endif