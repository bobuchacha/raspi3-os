#include "user_runtime.h"
#include "stdio.h"

#define CORE_DEBUG_LEVEL_ERROR "ERROR"
#define CORE_DEBUG_LEVEL_WARNING "WARN"
#define CORE_DEBUG_LEVEL_INFO "INFO"
#define CORE_DEBUG_LEVEL_TRACE "TRACE"

#define CORE_DEBUG_COLOR_RESET "\x1b[0m"
#define CORE_DEBUG_COLOR_ERROR "\x1b[31m"
#define CORE_DEBUG_COLOR_WARNING "\x1b[33m"
#define CORE_DEBUG_COLOR_INFO "\x1b[32m"
#define CORE_DEBUG_COLOR_TRACE "\x1b[36m"

DLL_EXPORT(debugLog);
DLL_EXPORT(debugTrace);
DLL_EXPORT(debugInfo);
DLL_EXPORT(debugWarning);
DLL_EXPORT(debugError);

static const char* core_debug_file_name(const char* path) {
    const char* file = path ? path : "?";

    while (path && *path != '\0') {
        if (*path == '/' || *path == '\\') {
            file = path + 1;
        }
        path++;
    }

    return file;
}

static long core_debug_emit(const char* level, const char* color, const char* file, unsigned long line, const char* func, const char* message) {
    char buffer[512];
    int written;

    written = snprintf(
        buffer,
        sizeof(buffer),
        "\n%s[%s]%s \x1b[0m(\x1b[2m%s:%lu\x1b[0m) \x1b[1m%s\x1b[0m | %s%s\r\n",
        color ? color : "",
        level ? level : CORE_DEBUG_LEVEL_INFO,
        CORE_DEBUG_COLOR_RESET,
        core_debug_file_name(file),
        line,
        func ? func : "?",
        message ? message : "",
        CORE_DEBUG_COLOR_RESET);
    if (written < 0) {
        return -1;
    }

    writeText(buffer);
    return 0;
}

long debugLog(const char* level, const char* color, const char* file, unsigned long line, const char* func, const char* message) {
    return core_debug_emit(level, color, file, line, func, message);
}

long debugTrace(const char* file, unsigned long line, const char* func, const char* message) {
    return core_debug_emit(CORE_DEBUG_LEVEL_TRACE, CORE_DEBUG_COLOR_TRACE, file, line, func, message);
}

long debugInfo(const char* file, unsigned long line, const char* func, const char* message) {
    return core_debug_emit(CORE_DEBUG_LEVEL_INFO, CORE_DEBUG_COLOR_INFO, file, line, func, message);
}

long debugWarning(const char* file, unsigned long line, const char* func, const char* message) {
    return core_debug_emit(CORE_DEBUG_LEVEL_WARNING, CORE_DEBUG_COLOR_WARNING, file, line, func, message);
}

long debugError(const char* file, unsigned long line, const char* func, const char* message) {
    return core_debug_emit(CORE_DEBUG_LEVEL_ERROR, CORE_DEBUG_COLOR_ERROR, file, line, func, message);
}