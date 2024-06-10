#ifndef _LOG_H_
#define _LOG_H_

#include "printf.h"

#define LOG_ERROR "E"
#define LOG_WARNING "*"
#define LOG_INFO "i"
#define LOG_TEST "T"
#define LOG_FAIL "F"

#define log(value, level) \
	kprint("(" level ") %s (" __FILE__ ":%d) | ", __func__, __LINE__); \
    kprint(value);


#define log_fail(value)     log(value, LOG_FAIL)
#define log_test(value)     log(value, LOG_TEST)
#define log_info(value)     log(value, LOG_INFO)
#define log_warning(value)  log(value, LOG_WARNING)
#define log_error(value)    log(value, LOG_ERROR)

#define _trace          log_info
#define _trace_printf   printf
#define _trace_p        printf

#define _trace(...)   \
	kprint("[L] %s (" __FILE__ ":%d) >> ", __func__, __LINE__); \
    kprint(__VA_ARGS__); \
    kprint("\n");

#endif