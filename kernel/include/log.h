#ifndef _LOG_H_
#define _LOG_H_

#include "printf.h"
#include "stacktrace.h"

#define LOG_ERROR "ERROR"
#define LOG_WARNING "WARN"
#define LOG_INFO "INFO"
#define LOG_TEST "TEST"
#define LOG_FAIL "FAIL"
#define LOG_TRACE "TRACE"

#define LOG_COLOR_RESET "\x1b[0m"
#define LOG_COLOR_ERROR "\x1b[31m"
#define LOG_COLOR_WARNING "\x1b[33m"
#define LOG_COLOR_INFO "\x1b[32m"
#define LOG_COLOR_TEST "\x1b[35m"
#define LOG_COLOR_FAIL "\x1b[31;1m"
#define LOG_COLOR_TRACE "\x1b[36m"

#ifndef LOG_ENABLE_MESSAGES
#define LOG_ENABLE_MESSAGES 1
#endif

#ifndef LOG_ENABLE_TRACE
#define LOG_ENABLE_TRACE 1
#endif

#ifndef LOG_ENABLE_ERROR
#define LOG_ENABLE_ERROR LOG_ENABLE_MESSAGES
#endif

#ifndef LOG_ENABLE_WARNING
#define LOG_ENABLE_WARNING LOG_ENABLE_MESSAGES
#endif

#ifndef LOG_ENABLE_INFO
#define LOG_ENABLE_INFO LOG_ENABLE_MESSAGES
#endif

#ifndef LOG_ENABLE_TEST
#define LOG_ENABLE_TEST LOG_ENABLE_MESSAGES
#endif

#ifndef LOG_ENABLE_FAIL
#define LOG_ENABLE_FAIL LOG_ENABLE_MESSAGES
#endif

#ifndef LOG_ENABLE_COLOR
#define LOG_ENABLE_COLOR 1
#endif

#ifndef LOG_ERROR_DUMP_STACK
#define LOG_ERROR_DUMP_STACK 1
#endif

static inline const char* log_file_name(const char* path) {
    const char* file = path;

    while (*path) {
        if (*path == '/' || *path == '\\') {
            file = path + 1;
        }
        path++;
    }

    return file;
}

static inline const char* log_level_color(const char* level) {
#if LOG_ENABLE_COLOR
    if (level == LOG_ERROR) {
        return LOG_COLOR_ERROR;
    }
    if (level == LOG_WARNING) {
        return LOG_COLOR_WARNING;
    }
    if (level == LOG_INFO) {
        return LOG_COLOR_INFO;
    }
    if (level == LOG_TEST) {
        return LOG_COLOR_TEST;
    }
    if (level == LOG_FAIL) {
        return LOG_COLOR_FAIL;
    }
    if (level == LOG_TRACE) {
        return LOG_COLOR_TRACE;
    }
#endif
    return "";
}

static inline const char* log_color_reset(void) {
#if LOG_ENABLE_COLOR
    return LOG_COLOR_RESET;
#else
    return "";
#endif
}

#define _log_prefix(level) \
    kprint("\n%s[%s]%s \x1b[0m(\x1b[2m%s:%d\x1b[0m) \x1b[1m%s\x1b[0m | ", log_level_color(level), level, log_color_reset(), log_file_name(__FILE__), __LINE__, __func__)


#define _log_emit(level, ...) \
    do                        \
    {                         \
        console_lock();       \
        _log_prefix(level);   \
        kprint(__VA_ARGS__);  \
        kprint("%s", log_color_reset()); \
        if (LOG_ERROR_DUMP_STACK && (level) == LOG_ERROR) { \
            dump_stack(); \
        } \
        console_unlock();     \
    } while (0)

#define log(value, level)   \
    do                      \
    {                       \
        _log_prefix(level); \
        kprint(value);      \
    } while (0)

#if LOG_ENABLE_FAIL
#define log_fail(...) _log_emit(LOG_FAIL, __VA_ARGS__)
#else
#define log_fail(...) ((void)0)
#endif

#if LOG_ENABLE_TEST
#define log_test(...) _log_emit(LOG_TEST, __VA_ARGS__)
#else
#define log_test(...) ((void)0)
#endif

#if LOG_ENABLE_INFO
#define log_info(...) _log_emit(LOG_INFO, __VA_ARGS__)
#else
#define log_info(...) ((void)0)
#endif

#if LOG_ENABLE_WARNING
#define log_warning(...) _log_emit(LOG_WARNING, __VA_ARGS__)
#else
#define log_warning(...) ((void)0)
#endif

#if LOG_ENABLE_ERROR
#define log_error(...) _log_emit(LOG_ERROR, __VA_ARGS__)
#else
#define log_error(...) ((void)0)
#endif

#if LOG_ENABLE_TRACE
#define _trace(...)             \
    do                          \
    {                           \
        console_lock();         \
        _log_prefix(LOG_TRACE); \
        kprint(__VA_ARGS__);    \
        console_unlock();       \
    } while (0)
#define _trace_printf printf
#define _trace_p printf
#else
#define _trace(...) ((void)0)
#define _trace_printf(...) ((void)0)
#define _trace_p(...) ((void)0)
#endif

#endif