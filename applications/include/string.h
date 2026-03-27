#ifndef ROS_APP_STRING_H
#define ROS_APP_STRING_H

#include "stddef.h"

void *memcpy(void *dest, const void *src, size_t size);
void *memmove(void *dest, const void *src, size_t size);
void *memset(void *dest, int value, size_t size);
int memcmp(const void *lhs, const void *rhs, size_t size);
size_t strlen(const char *text);
size_t strnlen(const char *text, size_t max_size);
int strcmp(const char *lhs, const char *rhs);
int strncmp(const char *lhs, const char *rhs, size_t size);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t size);

#endif