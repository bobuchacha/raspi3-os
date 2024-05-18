#ifndef	_BOOT_H
#define	_BOOT_H

extern void delay ( unsigned long);             // delays certain clock cycle
extern void put32 ( unsigned long, int );       // write an int to an address
extern unsigned int get32 ( unsigned long );    // get an int at an address

extern void put16 ( unsigned long, short );     // write a short to an address
extern unsigned int get16 ( unsigned long );    // get a short at an address

extern void put8 ( unsigned long, char);         // write a char to an address
extern unsigned int get8 ( unsigned long );     // get char at an address

extern int get_el ( void );             // get current Exception Level

#endif  /*_BOOT_H */
