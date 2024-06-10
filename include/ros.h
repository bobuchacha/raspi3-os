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
    true  = 1,
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


#endif

