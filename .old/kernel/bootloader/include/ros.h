#ifndef ROS_LOADER_ROS_H
#define ROS_LOADER_ROS_H

typedef unsigned char ubyte;
typedef unsigned short uword;
typedef unsigned int uint;
typedef unsigned long ulong;

typedef char Byte;
typedef short Word;
typedef int Int;
typedef long Long;

typedef ubyte UByte;
typedef uword UWord;
typedef uint UInt;
typedef ulong ULONG;
typedef ulong ULong;

typedef enum {
    true = !0,
    false = 0
} Bool;

typedef ULong Address;
typedef ULong Offset;
typedef ULong Flags;
typedef ubyte* Buffer;
typedef ULong Size;
typedef void* Pointer;

#define NULL ((void*)0)
#define null ((void*)0)
#define PACKED __attribute__((packed))

enum Status {
    SUCCESS,
    ERROR_INVAILD = -1,
    ERROR_NOT_EXIST = -2,
    ERROR_EXIST = -3,
    ERROR_NOT_FILE = -4,
    ERROR_NOT_DIRECTORY = -5,
    ERROR_READ_FAIL = -6,
    ERROR_OUT_OF_SPACE = -7,
    ERROR_WRITE_FAIL = -8,
    ERROR_NO_PERM = -9,
};

#include "lib/string.h"

#endif
