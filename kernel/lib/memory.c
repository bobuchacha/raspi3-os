#include <stddef.h>

#include "types.h"

void* memset(void* buf, int value, size_t count) {
    U8* bytes = (U8*)buf;

    for (size_t offset = 0; offset < count; ++offset) {
        bytes[offset] = (U8)value;
    }

    return buf;
}

void* memcpy(void* dst, const void* src, size_t count) {
    U8* dst_bytes = (U8*)dst;
    const U8* src_bytes = (const U8*)src;

    for (size_t offset = 0; offset < count; ++offset) {
        dst_bytes[offset] = src_bytes[offset];
    }

    return dst;
}