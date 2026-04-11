/**
 * types.h
 *
 * This header defines standard types that we will be using throughout our kernel
 */

#ifndef KERNEL_TYPES_H
#define KERNEL_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

 /* Fixed-width scalar typedefs (short uppercase names) */
typedef uint8_t  U8;
typedef int8_t   I8;
typedef uint16_t U16;
typedef int16_t  I16;
typedef uint32_t U32;
typedef int32_t  I32;
typedef uint64_t U64;
typedef int64_t  I64;

typedef U64 Size;
typedef I64 SSize;
typedef U64 PhysAddr;
typedef U64 VirtAddr;
typedef uintptr_t Uptr;
typedef intptr_t Iptr;

/* General boolean uses standard 'bool' */

/* Common Status enum for APIs */
typedef enum Status {
    StatusOK = 0,
    StatusInvalidArgument = -1,
    StatusNotFound = -2,
    StatusAlreadyExists = -3,
    StatusNotSupported = -4,
    StatusNoMemory = -5,
    StatusNoSpace = -6,
    StatusIoError = -7,
    StatusBusy = -8,
    StatusFault = -9,
    StatusFileNotFound = -10
} Status;

/* Small helpers */
static inline void memzero(void* buf, Size n) {
    U8* bytes = (U8*)buf;

    for (Size offset = 0; offset < n; ++offset) {
        bytes[offset] = 0;
    }
}

static inline void memcopy(void* dst, const void* src, Size n) {
    U8* dst_bytes = (U8*)dst;
    const U8* src_bytes = (const U8*)src;

    for (Size offset = 0; offset < n; ++offset) {
        dst_bytes[offset] = src_bytes[offset];
    }
}

#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))
#define PACKED __attribute__((packed))
// #define PAGE_ALIGNED __attribute__((aligned(KERNEL_PAGE_SIZE)));

/* Compatibility aliases for legacy names (temporary) */
#define ROS_NULL NULL

#endif // KERNEL_TYPES_H
