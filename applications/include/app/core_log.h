#ifndef ROS_APP_CORE_LOG_H
#define ROS_APP_CORE_LOG_H

#include "stdarg.h"
#include "stdio.h"
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

/*
 * coreDebugRender
 *
 * Keep the shell/core logging helper independent from the legacy DLL import
 * header because the live tree builds C sources with the C compiler, while the
 * dormant import header uses C++-only helpers. This formatter only needs the
 * user-runtime logging syscalls plus stdarg/stdio declarations.
 */
static inline long coreDebugRender(char* buffer, unsigned long size, const char* level, const char* color, const char* file, unsigned long line, const char* func, const char* message) {
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

/*
 * coreDebugVLogEmit
 *
 * Try the buffered brokered log path first so core-owned consumers can capture
 * structured lines, then fall back to the direct console write path when the
 * broker is unavailable.
 */
static inline long coreDebugVLogEmit(const char* level, const char* color, const char* file, unsigned long line, const char* func, const char* fmt, va_list args) {
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

static inline long coreDebugLogCall(const char* level, const char* color, const char* file, unsigned long line, const char* func, const char* fmt, ...) {
    long result;
    va_list args;

    va_start(args, fmt);
    result = coreDebugVLogEmit(level, color, file, line, func, fmt, args);
    va_end(args);
    return result;
}

static inline long coreDebugInfoCall(const char* file, unsigned long line, const char* func, const char* fmt, ...) {
    long result;
    va_list args;

    va_start(args, fmt);
    result = coreDebugVLogEmit(ROS_CORE_LOG_INFO, ROS_CORE_LOG_COLOR_INFO, file, line, func, fmt, args);
    va_end(args);
    return result;
}

static inline long coreDebugWarningCall(const char* file, unsigned long line, const char* func, const char* fmt, ...) {
    long result;
    va_list args;

    va_start(args, fmt);
    result = coreDebugVLogEmit(ROS_CORE_LOG_WARNING, ROS_CORE_LOG_COLOR_WARNING, file, line, func, fmt, args);
    va_end(args);
    return result;
}

static inline long coreDebugErrorCall(const char* file, unsigned long line, const char* func, const char* fmt, ...) {
    long result;
    va_list args;

    va_start(args, fmt);
    result = coreDebugVLogEmit(ROS_CORE_LOG_ERROR, ROS_CORE_LOG_COLOR_ERROR, file, line, func, fmt, args);
    va_end(args);
    return result;
}

static inline long coreDebugTraceCall(const char* file, unsigned long line, const char* func, const char* fmt, ...) {
    long result;
    va_list args;

    va_start(args, fmt);
    result = coreDebugVLogEmit(ROS_CORE_LOG_TRACE, ROS_CORE_LOG_COLOR_TRACE, file, line, func, fmt, args);
    va_end(args);
    return result;
}

#define debugLog(level, color, fmt, ...) coreDebugLogCall((level), (color), __FILE__, (unsigned long)__LINE__, __func__, (fmt), ##__VA_ARGS__)
#define debugInfo(fmt, ...) coreDebugInfoCall(__FILE__, (unsigned long)__LINE__, __func__, (fmt), ##__VA_ARGS__)
#define debugWarning(fmt, ...) coreDebugWarningCall(__FILE__, (unsigned long)__LINE__, __func__, (fmt), ##__VA_ARGS__)
#define debugError(fmt, ...) coreDebugErrorCall(__FILE__, (unsigned long)__LINE__, __func__, (fmt), ##__VA_ARGS__)
#define debugTrace(fmt, ...) coreDebugTraceCall(__FILE__, (unsigned long)__LINE__, __func__, (fmt), ##__VA_ARGS__)

#endif