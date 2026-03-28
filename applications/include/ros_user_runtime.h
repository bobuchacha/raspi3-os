#ifndef APPLICATIONS_ROS_USER_RUNTIME_H
#define APPLICATIONS_ROS_USER_RUNTIME_H

#include <stddef.h>

#define ROS_SYS_WRITE 0UL
#define ROS_SYS_SLEEP 6UL
#define ROS_SYS_SHLIB_OPEN 8UL
#define ROS_SYS_TASK_NAME 9UL
#define ROS_SYS_SPAWN 10UL
#define ROS_SYS_TASK_ARGS 22UL
#define ROS_SYS_SHLIB_EXPORT 24UL

static inline unsigned long ros_syscall0(unsigned long number) {
    register unsigned long x0 asm("x0");
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline unsigned long ros_syscall1(unsigned long number, unsigned long arg0) {
    register unsigned long x0 asm("x0") = arg0;
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline unsigned long ros_syscall2(unsigned long number, unsigned long arg0, unsigned long arg1) {
    register unsigned long x0 asm("x0") = arg0;
    register unsigned long x1 asm("x1") = arg1;
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
    return x0;
}

static inline unsigned long ros_syscall3(unsigned long number, unsigned long arg0, unsigned long arg1, unsigned long arg2) {
    register unsigned long x0 asm("x0") = arg0;
    register unsigned long x1 asm("x1") = arg1;
    register unsigned long x2 asm("x2") = arg2;
    register unsigned long x8 asm("x8") = number;

    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

static inline unsigned long ros_strlen(const char* text) {
    unsigned long length = 0;

    if (!text) {
        return 0;
    }

    while (text[length] != '\0') {
        length++;
    }

    return length;
}

static inline char* ros_append_text(char* dst, const char* text) {
    if (!dst || !text) {
        return dst;
    }

    while (*text) {
        *dst++ = *text++;
    }

    return dst;
}

static inline char* ros_append_ulong(char* dst, unsigned long value) {
    char digits[32];
    unsigned long count = 0;

    if (!dst) {
        return dst;
    }
    if (value == 0) {
        *dst++ = '0';
        return dst;
    }

    while (value != 0) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count > 0) {
        *dst++ = digits[--count];
    }

    return dst;
}

static inline void ros_write(const char* text) {
    ros_syscall1(ROS_SYS_WRITE, (unsigned long)text);
}

static inline void ros_write_line(const char* text) {
    ros_write(text);
    ros_write("\r\n");
}

static inline long ros_sleep(unsigned long msec) {
    return (long)ros_syscall1(ROS_SYS_SLEEP, msec);
}

static inline long ros_task_name(char* buffer, unsigned long size) {
    return (long)ros_syscall2(ROS_SYS_TASK_NAME, (unsigned long)buffer, size);
}

static inline long ros_task_args(char* buffer, unsigned long size) {
    return (long)ros_syscall2(ROS_SYS_TASK_ARGS, (unsigned long)buffer, size);
}

static inline unsigned long ros_shlib_open(const char* path) {
    return ros_syscall1(ROS_SYS_SHLIB_OPEN, (unsigned long)path);
}

static inline long ros_spawn(const char* path, const char* name, const char* args) {
    return (long)ros_syscall3(ROS_SYS_SPAWN, (unsigned long)path, (unsigned long)name, (unsigned long)args);
}

static inline unsigned long ros_shlib_export(const char* path, const char* export_name) {
    return ros_syscall2(ROS_SYS_SHLIB_EXPORT, (unsigned long)path, (unsigned long)export_name);
}

#endif