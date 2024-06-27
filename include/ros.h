#ifndef _ROS_H_
#define _ROS_H_

typedef unsigned long int ULONG;
typedef unsigned long int ulong;   // 64 bit
typedef unsigned int      uint;         // 32 bit
typedef unsigned short    uword;      // 16 bit
typedef unsigned char     ubyte;        // 8 bit

#define CONST           const
#define NULL            ((void*)0)
#define null            ((void*)0)

typedef enum {
    true  = !0,
    false = 0
} Bool;



typedef unsigned long long Address;
typedef unsigned long long Offset;
typedef unsigned long int  ULong;
typedef long int           Long;
typedef int                Int;
typedef unsigned int       UInt;
typedef short              Word;
typedef unsigned short     UWord;
typedef char               Byte;
typedef unsigned char      UByte;
typedef unsigned char*     Buffer;
typedef unsigned int       Size;
typedef void*              Pointer;
typedef unsigned long long Flags;

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

