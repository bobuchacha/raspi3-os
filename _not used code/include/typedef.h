#ifndef __typedef_h
#define __typedef_h

#define CONST const
#define TRUE 1
#define FALSE 0

typedef signed char     SINT8;          // A 8 bit integer, signed -128 to 127, or a CHAR %c 
typedef unsigned char   INT8;           // A 8 bit unsigned integer. 0 to 255 
typedef unsigned short int INT16;       // A 16 bit unsigned integer. 0 to 65,535 
typedef short int       SINT16;         // A 16 bit signed integer. -32,768 to 32,767 
typedef unsigned int    INT32;          // A 32-bit integer. 0 to 4,294,967,295 
typedef int             SINT32;         // A 32-bit integer. -2,147,483,648 to 2,147,483,647
typedef long long int   SINT64;         // A 64-bit integer. -(2^63) to (2^63)-1 
typedef unsigned long long int INT64;   // A 64-bit integer. 0 to 18,446,744,073,709,551,615
typedef float           FLOAT32;        // A 32 bit float. 1.2E-38 to 3.4E+38
typedef double          FLOAT64;        // A 64 bit float or double. 1.7E-308 to 1.7E+308        
typedef long double     FLOAT128;       // A 128 bit float; 3.4E-4932 to 1.1E+4932

typedef int             BOOL;                   // 	A Boolean variable (should be TRUE or FALSE).
typedef unsigned char   BOOLEAN;                // 	A Boolean variable (should be TRUE or FALSE).

typedef BOOL            *PTR_BOOL;              //      A pointer to a BOOL
typedef BOOLEAN         *PTR_BOOLEAN;           //      A pointer to a BOOLEAN
typedef INT8            *PTR_INT8;              //      A pointer to a INT8   
typedef FLOAT32         *PTR_FLOAT32;           //      A pointer to a FLOAT32   
// typedef DWORD           *PTR_DWORD;             //      A pointer to a DWORD   
// typedef SDWORD          *PTR_SDWORD;            //      A pointer to a SDWORD   


typedef INT64           INT;                      // default type of Integer - 64 bit
typedef SINT64          SINT;                    // default type signed integer

typedef SINT64 HWINDOW;                 // A Handler to Window. Or a Window ID
typedef char* STRING;

#define NULL            0

#endif

