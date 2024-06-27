#ifndef _LOG_H_
#define _LOG_H_

#include "printf.h"

#define LOG_ERROR "E"
#define LOG_WARNING "*"
#define LOG_INFO "i"
#define LOG_TEST "T"
#define LOG_FAIL "F"

#define EXPAND( x ) x

#define log(value, level) \
	kprint("\n(" level ") %s (" __FILE__ ":%d) | ", __func__, __LINE__); \
    kprint(value);

#define _log(level, ...) \
	kprint("\n(" level ") %s (" __FILE__ ":%d) | ", __func__, __LINE__); \
    kprint(__VA_ARGS__);

#define log_fail(...)       EXPAND(_log(LOG_FAIL, __VA_ARGS__))
#define log_test(...)       EXPAND(_log(LOG_TEST, __VA_ARGS__))
#define log_info(...)       EXPAND(_log(LOG_INFO, __VA_ARGS__))
#define log_warning(...)    EXPAND(_log(LOG_WARNING, __VA_ARGS__))
#define log_error(...)      EXPAND(_log(LOG_ERROR, __VA_ARGS__))

#define _trace(...)   \
	kprint("\n[L] %s (" __FILE__ ":%d) >> ", __func__, __LINE__); \
    kprint(__VA_ARGS__); \
    kprint("\n");
#define _trace_printf   printf
#define _trace_p        printf

#endif