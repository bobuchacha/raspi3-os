#ifndef ROS_APP_CORE_LOG_H
#define ROS_APP_CORE_LOG_H

#include "app/import.h"
#include "user_runtime.h"

#define ROS_CORE_LOG_BUFFER_MAX 256U

#define ROS_CORE_LOG_ERROR "ERROR"
#define ROS_CORE_LOG_WARNING "WARN"
#define ROS_CORE_LOG_INFO "INFO"
#define ROS_CORE_LOG_TRACE "TRACE"

#define ROS_CORE_LOG_COLOR_RESET "\x1b[0m"
#define ROS_CORE_LOG_COLOR_ERROR "\x1b[31m"
#define ROS_CORE_LOG_COLOR_WARNING "\x1b[33m"
#define ROS_CORE_LOG_COLOR_INFO "\x1b[32m"
#define ROS_CORE_LOG_COLOR_TRACE "\x1b[36m"

static inline const char* coreDebugFileName(const char* path) {
    const char* file = path ? path : "?";

    while (path && *path != '\0') {
        if (*path == '/' || *path == '\\') {
            file = path + 1;
        }
        path++;
    }

    return file;
}

static inline Long coreDebugRender(char* buffer, unsigned long size, const char* level, const char* color, const char* file, ULong line, const char* func, const char* message) {
    if (!buffer || size == 0U) {
        return -1;
    }

    return snprintf(
        buffer,
        size,
        "\n%s[%s]%s \x1b[0m(\x1b[2m%s:%lu\x1b[0m) \x1b[1m%s\x1b[0m | %s%s\r\n",
        color ? color : "",
        level ? level : ROS_CORE_LOG_INFO,
        ROS_CORE_LOG_COLOR_RESET,
        coreDebugFileName(file),
        line,
        func ? func : "?",
        message ? message : "",
        ROS_CORE_LOG_COLOR_RESET) < 0 ? -1 : 0;
}

static inline Long coreDebugVLogEmit(const char* level, const char* color, const char* file, ULong line, const char* func, const char* fmt, va_list args) {
    char message[ROS_CORE_LOG_BUFFER_MAX];
    char rendered[512];

    if (!fmt) {
        return -1;
    }
    if (vsnprintf(message, sizeof(message), fmt, args) < 0) {
        return -1;
    }
    if (coreDebugRender(rendered, sizeof(rendered), level, color, file, line, func, message) != 0) {
        return -1;
    }

    if (sendLog(rendered) == 0) {
        return 0;
    }

    return writeLog(rendered);
}

static inline Long coreDebugLogCall(const char* level, const char* color, const char* file, ULong line, const char* func, const char* fmt, ...) {
    Long result;
    va_list args;

    va_start(args, fmt);
    result = coreDebugVLogEmit(level, color, file, line, func, fmt, args);
    va_end(args);
    return result;
}

static inline Long coreDebugInfoCall(const char* file, ULong line, const char* func, const char* fmt, ...) {
    Long result;
    va_list args;

    va_start(args, fmt);
    result = coreDebugVLogEmit(ROS_CORE_LOG_INFO, ROS_CORE_LOG_COLOR_INFO, file, line, func, fmt, args);
    va_end(args);
    return result;
}

static inline Long coreDebugWarningCall(const char* file, ULong line, const char* func, const char* fmt, ...) {
    Long result;
    va_list args;

    va_start(args, fmt);
    result = coreDebugVLogEmit(ROS_CORE_LOG_WARNING, ROS_CORE_LOG_COLOR_WARNING, file, line, func, fmt, args);
    va_end(args);
    return result;
}

static inline Long coreDebugErrorCall(const char* file, ULong line, const char* func, const char* fmt, ...) {
    Long result;
    va_list args;

    va_start(args, fmt);
    result = coreDebugVLogEmit(ROS_CORE_LOG_ERROR, ROS_CORE_LOG_COLOR_ERROR, file, line, func, fmt, args);
    va_end(args);
    return result;
}

static inline Long coreDebugTraceCall(const char* file, ULong line, const char* func, const char* fmt, ...) {
    Long result;
    va_list args;

    va_start(args, fmt);
    result = coreDebugVLogEmit(ROS_CORE_LOG_TRACE, ROS_CORE_LOG_COLOR_TRACE, file, line, func, fmt, args);
    va_end(args);
    return result;
}

#define debugLog(level, color, fmt, ...) coreDebugLogCall((level), (color), __FILE__, (ULong)__LINE__, __func__, (fmt), ##__VA_ARGS__)
#define debugInfo(fmt, ...) coreDebugInfoCall(__FILE__, (ULong)__LINE__, __func__, (fmt), ##__VA_ARGS__)
#define debugWarning(fmt, ...) coreDebugWarningCall(__FILE__, (ULong)__LINE__, __func__, (fmt), ##__VA_ARGS__)
#define debugError(fmt, ...) coreDebugErrorCall(__FILE__, (ULong)__LINE__, __func__, (fmt), ##__VA_ARGS__)
#define debugTrace(fmt, ...) coreDebugTraceCall(__FILE__, (ULong)__LINE__, __func__, (fmt), ##__VA_ARGS__)

#endif