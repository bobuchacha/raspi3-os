#ifndef ROS_LOADER_PRINTF_H
#define ROS_LOADER_PRINTF_H

#include <stdarg.h>

void init_printf(void* putp, void (*putf)(void*, char));
void tfp_printf(const char* fmt, ...);
int tfp_sprintf(char* s, const char* fmt, ...);
void tfp_format(void* putp, void (*putf)(void*, char), const char* fmt, va_list va);

#define printf tfp_printf
#define sprintf tfp_sprintf

void console_lock(void);
void console_unlock(void);
void kprint(const char* fmt, ...);
void kerror(const char* fmt, ...);
void kpanic(const char* fmt, ...);

#endif
