#ifndef ROS_APP_LOGGER_H
#define ROS_APP_LOGGER_H

#include "stdarg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AppLogLevel
{
    APP_LOG_LEVEL_TRACE = 0,
    APP_LOG_LEVEL_DEBUG = 1,
    APP_LOG_LEVEL_INFO = 2,
    APP_LOG_LEVEL_WARN = 3,
    APP_LOG_LEVEL_ERROR = 4,
} AppLogLevel;

void app_log_set_color_enabled(int enabled);
int app_log_color_enabled(void);
void app_log_vwrite(AppLogLevel level, const char *tag, const char *fmt, va_list args);
void app_log_write(AppLogLevel level, const char *tag, const char *fmt, ...);

#define app_log_trace(tag, fmt, ...) app_log_write(APP_LOG_LEVEL_TRACE, tag, fmt, ##__VA_ARGS__)
#define app_log_debug(tag, fmt, ...) app_log_write(APP_LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define app_log_info(tag, fmt, ...) app_log_write(APP_LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#define app_log_warn(tag, fmt, ...) app_log_write(APP_LOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
#define app_log_error(tag, fmt, ...) app_log_write(APP_LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif