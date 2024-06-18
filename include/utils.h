#ifndef	_BOOT_H
#define	_BOOT_H

#include "ros.h"

extern void delay ( unsigned long);             // delays certain clock cycle

extern void put32 ( unsigned long, int );       // write an int to an address
extern unsigned int get32 ( unsigned long );    // get an int at an address

extern void put16 ( unsigned long, short );     // write a short to an address
extern unsigned int get16 ( unsigned long );    // get a short at an address

extern void put8 ( unsigned long, char);         // write a char to an address
extern unsigned int get8 ( unsigned long );     // get char at an address

extern int get_el ( void );             // get current Exception Level

/**
 * zero memory at ptr for [count] bytes
 * @param ptr
 * @param count
 */
extern void memzero(Address ptr, int count);

/**
 * copy memory
 * @param dest
 * @param src
 * @param count
 */
extern void memcpy(Address dest, Address src, int count);

extern void set_pgd(Address);
extern Address get_pgd();

#endif  /*_BOOT_H */
