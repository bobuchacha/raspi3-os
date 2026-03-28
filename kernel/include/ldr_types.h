/*
 * ldr_types.h
 *
 * Shared base data types used by the loader.
 */
#ifndef LDR_TYPES_H
#define LDR_TYPES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef _ROS_H_
	typedef unsigned char ubyte;
	typedef unsigned short uword;
	typedef unsigned int uint;
	typedef unsigned long int ulong;

	typedef char Byte;
	typedef short Word;
	typedef int Int;
	typedef long int Long;

	typedef ubyte UByte;
	typedef uword UWord;
	typedef uint UInt;
	typedef ulong ULONG;
	typedef ulong ULong;

#ifndef CONST
#define CONST const
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef null
#define null ((void *)0)
#endif

#ifdef __cplusplus
	typedef bool Bool;
#else
typedef enum
{
	true = !0,
	false = 0
} Bool;
#endif

	typedef ULong Address;
	typedef ULong Offset;
	typedef ULong Flags;
	typedef ubyte *Buffer;
	typedef ULong Size;
	typedef void *Pointer;

	enum Status
	{
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
#endif

#ifdef __cplusplus
}
#endif

#endif /* LDR_TYPES_H */