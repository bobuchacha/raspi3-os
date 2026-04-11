#ifndef _ROS_H_
#define _ROS_H_

/* Basic integer aliases used across the kernel and user code. */
typedef unsigned char      ubyte;
typedef unsigned short     uword;
typedef unsigned int       uint;
typedef unsigned long int  ulong;

typedef char               Byte;
typedef short              Word;
typedef int                Int;
typedef long int           Long;

typedef ubyte              UByte;
typedef uword              UWord;
typedef uint               UInt;
typedef ulong              ULONG;
typedef ulong              ULong;

#define CONST           const
#define NULL            ((void*)0)
#define null            ((void*)0)
#define AddressOf(T)    T *
#define Ptr(T)          T *
#define SizeOf(T)       sizeof(T)



typedef enum {
    true = !0,
    false = 0
} Bool;

typedef ULong Address;
typedef ULong Offset;
typedef ULong Flags;

typedef ubyte* Buffer;
typedef ULong              Size;
typedef void* Pointer;

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



// include libs here
#include "lib/string.h"
#include "lib/util.h"
#endif

