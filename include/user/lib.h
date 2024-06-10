#ifndef __USER_LIB_H
#define __USER_LIB_H

void fprint(char* buff, char *fmt, ...);
static void putchw(char* buff, int* buff_offset, int n, char z, char* bf);
static char a2i(char ch, char** src,int base,int* nump);
static int a2d(char ch);
static void i2a (int num, char * bf);
static void ui2a(unsigned int num, unsigned int base, int uc,char * bf);
static void li2a (long num, char * bf);
static void uli2a(unsigned long long num, unsigned long base, int uc,char * bf);

#endif