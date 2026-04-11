#include "logger.h"

#include "stdio.h"
#include "string.h"
#include "user_runtime.h"

#define APP_LOG_MESSAGE_MAX 192U
#define APP_LOG_LINE_MAX 256U

#define APP_LOG_COLOR_RESET "\x1b[0m"
#define APP_LOG_COLOR_TRACE "\x1b[36m"
#define APP_LOG_COLOR_DEBUG "\x1b[34m"
#define APP_LOG_COLOR_INFO "\x1b[32m"
#define APP_LOG_COLOR_WARN "\x1b[33m"
#define APP_LOG_COLOR_ERROR "\x1b[31m"

static int g_app_log_color_enabled = 0;

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
        return "INFO";
    }
}

static const char* app_log_level_color(AppLogLevel level) {
    switch (level) {
    case APP_LOG_LEVEL_TRACE:
        return APP_LOG_COLOR_TRACE;
    case APP_LOG_LEVEL_DEBUG:
        return APP_LOG_COLOR_DEBUG;
    case APP_LOG_LEVEL_INFO:
        return APP_LOG_COLOR_INFO;
    case APP_LOG_LEVEL_WARN:
        return APP_LOG_COLOR_WARN;
    case APP_LOG_LEVEL_ERROR:
        return APP_LOG_COLOR_ERROR;
    default:
        return APP_LOG_COLOR_INFO;
    }
}

void app_log_set_color_enabled(int enabled) {
    g_app_log_color_enabled = enabled != 0;
}

int app_log_color_enabled(void) {
    return g_app_log_color_enabled;
}

void app_log_vwrite(AppLogLevel level, const char* tag, const char* fmt, va_list args) {
    char message[APP_LOG_MESSAGE_MAX];
    char line[APP_LOG_LINE_MAX];
    const char* level_name;
    const char* color;
    int written;
    size_t length;

    if (!fmt) {
        return;
    }

    if (vsnprintf(message, sizeof(message), fmt, args) < 0) {
        return;
    }

    level_name = app_log_level_name(level);
    color = app_log_level_color(level);

    if (tag && tag[0] != '\0') {
        if (g_app_log_color_enabled) {
            written = snprintf(line, sizeof(line), "%s%s%s: %s", color, tag, APP_LOG_COLOR_RESET, message);
        }
        else {
            written = snprintf(line, sizeof(line), "%s: %s", tag, message);
        }
    }
    else if (g_app_log_color_enabled) {
        written = snprintf(line, sizeof(line), "%s%s%s: %s", color, level_name, APP_LOG_COLOR_RESET, message);
    }
    else {
        written = snprintf(line, sizeof(line), "%s: %s", level_name, message);
    }

    if (written < 0) {
        return;
    }

    length = strlen(line);
    if (length + 3U < sizeof(line)) {
        line[length] = '\r';
        line[length + 1U] = '\n';
        line[length + 2U] = '\0';
    }
    else {
        line[sizeof(line) - 3U] = '\r';
        line[sizeof(line) - 2U] = '\n';
        line[sizeof(line) - 1U] = '\0';
    }

    (void)writeLog(line);
}

void app_log_write(AppLogLevel level, const char* tag, const char* fmt, ...) {
    va_list args;

    va_start(args, fmt);
    app_log_vwrite(level, tag, fmt, args);
    va_end(args);
}