#ifndef ROS_APP_KERNEL_HPP
#define ROS_APP_KERNEL_HPP

extern "C"
{
#include "app/kernel.h"
}

namespace user::kernel {
    inline void write(const char* text) {
        user_kernel_write(text);
    }

    inline unsigned long alloc(unsigned long size) {
        return user_kernel_alloc(size);
    }

    inline void free(void* ptr) {
        user_kernel_free(ptr);
    }

    inline long sleep(unsigned long msec) {
        return user_kernel_sleep(msec);
    }

    inline void delay(unsigned long msec) {
        user_kernel_delay(msec);
    }

    inline void spin_delay(unsigned long count) {
        user_kernel_spin_delay(count);
    }

    inline int get_param(int param) {
        return user_kernel_get_param(param);
    }

    [[noreturn]] inline void exit(unsigned long code) {
        user_kernel_exit(code);
        while (true) {
        }
    }

    inline long exec(const char* path) {
        return user_kernel_exec(path);
    }

    inline unsigned long open_shared_library(const char* path) {
        return user_kernel_open_shared_library(path);
    }

    inline unsigned long shared_library_export(const char* path, const char* export_name) {
        return user_kernel_shared_library_export(path, export_name);
    }

    inline unsigned long shared_library_local(const char* path, unsigned long size) {
        return user_kernel_shared_library_local(path, size);
    }

    inline long get_name(char* buf, unsigned long size) {
        return user_kernel_get_name(buf, size);
    }

    inline long get_args(char* buf, unsigned long size) {
        return user_kernel_get_args(buf, size);
    }

    inline long spawn_with_args(const char* path, const char* name, const char* args) {
        return user_kernel_spawn_with_args(path, name, args);
    }

    inline long spawn(const char* path, const char* name) {
        return user_kernel_spawn(path, name);
    }

    inline long console_read(void) {
        return user_kernel_console_read();
    }

    inline long read_file(const char* path, unsigned long offset, char* buf, unsigned long size) {
        return user_kernel_read_file(path, offset, buf, size);
    }

    inline long dir_entry(const char* path, unsigned long entry_index, UserDirectoryEntry* entry) {
        return user_kernel_dir_entry(path, entry_index, entry);
    }

    inline long mkdir(const char* path) {
        return user_kernel_mkdir(path);
    }

    inline long task_info(long pid, UserTaskInfo* info) {
        return user_kernel_task_info(pid, info);
    }

    inline long mem_info(UserMemInfo* info) {
        return user_kernel_mem_info(info);
    }

    inline long kill(long pid) {
        return user_kernel_kill(pid);
    }

    inline long module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result) {
        return user_kernel_module_invoke(module_name, export_name, a, b, result);
    }

    [[noreturn]] inline void reboot(void) {
        user_kernel_reboot();
        while (true) {
        }
    }
}

#endif