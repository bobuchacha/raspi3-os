#ifndef ROS_LOADER_LOG_H
#define ROS_LOADER_LOG_H

#include "printf.h"

#ifndef LOG_ENABLE_TRACE
#define LOG_ENABLE_TRACE 0
#endif

#define log_error(...)                 \
    do {                               \
        console_lock();                \
        kprint("[error] ");            \
        kprint(__VA_ARGS__);           \
        kprint("\r\n");                \
        console_unlock();              \
    } while (0)

#define log_info(...)                  \
    do {                               \
        console_lock();                \
        kprint("[info] ");             \
        kprint(__VA_ARGS__);           \
        kprint("\r\n");                \
        console_unlock();              \
    } while (0)

#if LOG_ENABLE_TRACE
#define _trace(...)                    \
    do {                               \
        console_lock();                \
        kprint("[trace] ");            \
        kprint(__VA_ARGS__);           \
        kprint("\r\n");                \
        console_unlock();              \
    } while (0)
#define _trace_printf printf
#else
#define _trace(...) ((void)0)
#define _trace_printf(...) ((void)0)
#endif

#endif
