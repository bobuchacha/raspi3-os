#ifndef UTIL_H
#define UTIL_H

/**
 * read 16 bit integer from a pointer into an int
*/
unsigned int readi16(unsigned char *buff, unsigned int offset);

/**
 * read 32 bit integer in memory into an int
*/
unsigned int readi32(unsigned char *buff, unsigned int offset);

#endif