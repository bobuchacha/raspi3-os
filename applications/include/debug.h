#ifndef ROS_APPLICATION_DEBUG_H
#define ROS_APPLICATION_DEBUG_H

#include <stdio.h>

#define DBG_COLOR_RESET "\033[0m"
#define DBG_COLOR_MSG "\033[1;32m"
#define DBG_COLOR_TRACE "\033[1;36m"

/*
 * Return the trailing file component for one source path.
 *
 * Keeping the prefix trimming in one tiny inline helper makes the macros emit
 * compact file names instead of full workspace paths while still relying only
 * on the caller-provided `__FILE__` string.
 *
 * @param path Compiler-provided source path.
 * @return Pointer to the last path component, or the original pointer when no separator exists.
 */
static inline const char* dbg_file_name(const char* path) {
    const char* cursor = path;
    const char* base = path;

    if (path == 0) {
        return "<unknown>";
    }

    while (*cursor != '\0') {
        if (*cursor == '/' || *cursor == '\\') {
            base = cursor + 1;
        }
        ++cursor;
    }

    return base;
}

#ifndef DBG_DISABLE_MSG
#define DBG_MSG(fmt, ...) \
    do { \
        printf(DBG_COLOR_MSG "[MSG] %s:%d: " fmt DBG_COLOR_RESET "\n", dbg_file_name(__FILE__), __LINE__, ##__VA_ARGS__); \
    } while (0)
#else
#define DBG_MSG(...) ((void)0)
#endif

#ifndef DBG_DISABLE_TRACE
#define DBG_TRACE(fmt, ...) \
    do { \
        printf(DBG_COLOR_TRACE "[TRACE] %s:%d: " fmt DBG_COLOR_RESET "\n", dbg_file_name(__FILE__), __LINE__, ##__VA_ARGS__); \
    } while (0)
#else
#define DBG_TRACE(...) ((void)0)
#endif

#endif