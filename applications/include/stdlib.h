#ifndef ROS_APP_STDLIB_H
#define ROS_APP_STDLIB_H

#include "stddef.h"

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t count, size_t size);
void abort(void) __attribute__((noreturn));
void exit(int code) __attribute__((noreturn));

#endif