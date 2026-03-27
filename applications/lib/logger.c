#include "app/kernel.h"
#include "logger.h"
#include "stacktrace.h"
#include "stdio.h"

#ifndef APP_LOG_ERROR_DUMP_STACK
#define APP_LOG_ERROR_DUMP_STACK 1
#endif

static int g_app_log_color_enabled = 1;

static const char* app_log_level_name(AppLogLevel level) {
    switch (level) {
    case APP_LOG_LEVEL_TRACE:
        return "TRACE";
    case APP_LOG_LEVEL_DEBUG:
        return "DEBUG";
    case APP_LOG_LEVEL_INFO:
        return "INFO";
    case APP_LOG_LEVEL_WARN:
        return "WARN";
    case APP_LOG_LEVEL_ERROR:
        return "ERROR";
    default:
        return "LOG";
    }
}

static const char* app_log_level_color(AppLogLevel level) {
    switch (level) {
    case APP_LOG_LEVEL_TRACE:
        return "\x1b[36m";
    case APP_LOG_LEVEL_DEBUG:
        return "\x1b[34m";
    case APP_LOG_LEVEL_INFO:
        return "\x1b[32m";
    case APP_LOG_LEVEL_WARN:
        return "\x1b[33m";
    case APP_LOG_LEVEL_ERROR:
        return "\x1b[31m";
    default:
        return "\x1b[0m";
    }
}

void app_log_set_color_enabled(int enabled) {
    g_app_log_color_enabled = enabled != 0;
}

int app_log_color_enabled(void) {
    return g_app_log_color_enabled;
}

void app_log_vwrite(AppLogLevel level, const char* tag, const char* fmt, va_list args) {
    char message[384];
    char line[512];
    const char* level_name = app_log_level_name(level);

    vsnprintf(message, sizeof(message), fmt, args);
    if (tag && tag[0] != '\0') {
        if (g_app_log_color_enabled) {
            snprintf(line, sizeof(line), "\n%s[%s]\x1b[0m[%s] %s\n", app_log_level_color(level), level_name, tag, message);
        }
        else {
            snprintf(line, sizeof(line), "\n[%s][%s] %s\n", level_name, tag, message);
        }
    }
    else if (g_app_log_color_enabled) {
        snprintf(line, sizeof(line), "\n%s[%s]\x1b[0m %s\n", app_log_level_color(level), level_name, message);
    }
    else {
        snprintf(line, sizeof(line), "\n[%s] %s\n", level_name, message);
    }

    user_kernel_write(line);
    if (APP_LOG_ERROR_DUMP_STACK && level == APP_LOG_LEVEL_ERROR) {
        app_dump_stack();
    }
}

void app_log_write(AppLogLevel level, const char* tag, const char* fmt, ...) {
    va_list args;

    va_start(args, fmt);
    app_log_vwrite(level, tag, fmt, args);
    va_end(args);
}