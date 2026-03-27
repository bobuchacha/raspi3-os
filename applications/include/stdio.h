#ifndef ROS_APP_STDIO_H
#define ROS_APP_STDIO_H

#include "stdarg.h"
#include "stddef.h"

#ifdef __cplusplus
extern "C" {
#endif

int vsnprintf(char *buffer, size_t size, const char *fmt, va_list args);
int snprintf(char *buffer, size_t size, const char *fmt, ...);
int printf(const char *fmt, ...);
int puts(const char *text);
void fprint(char *buffer, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif