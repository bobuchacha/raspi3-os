#ifndef ROS_APP_KERNEL_H
#define ROS_APP_KERNEL_H

#include "app/syscall.h"
#include "user_runtime.h"

static inline long user_kernel_write(const char* text) {
    return call_sys_write((char*)text);
}

/*
 * Keep the legacy helper name while routing allocations through the current
 * in-process shared heap instead of the removed syscall-era compatibility shim.
 */
static inline unsigned long user_kernel_alloc(unsigned long size) {
    return (unsigned long)user_shared_heap_malloc((size_t)size);
}

/*
 * Preserve the legacy helper signature while releasing memory through the
 * shared userspace heap that now owns ordinary EL0 allocations.
 */
static inline long user_kernel_free(void* ptr) {
    user_shared_heap_free(ptr);
    return 0L;
}

static inline long user_kernel_sleep(unsigned long msec) {
    return call_sys_sleep(msec);
}

static inline void user_kernel_delay(unsigned long msec) {
    user_delay(msec);
}

static inline void user_kernel_spin_delay(unsigned long count) {
    user_spin_delay(count);
}

static inline int user_kernel_get_param(int param) {
    return call_sys_get_param(param);
}

static inline long user_kernel_exec(const char* path) {
    return call_sys_exec(path);
}

static inline unsigned long user_kernel_open_shared_library(const char* path) {
    return call_sys_shlib_open(path);
}

static inline long user_kernel_close_shared_library(const char* path) {
    return call_sys_shlib_close(path);
}

static inline unsigned long user_kernel_shared_library_export(const char* path, const char* export_name) {
    return call_sys_shlib_export(path, export_name);
}

static inline unsigned long user_kernel_shared_library_local(const char* path, unsigned long size) {
    return call_sys_shlib_local(path, size);
}

static inline unsigned long user_kernel_create_file_mapping(const char* path, unsigned long size) {
    return call_sys_file_mapping_create(path, size);
}

static inline unsigned long user_kernel_open_file_mapping(const char* path, unsigned long size) {
    return call_sys_file_mapping_open(path, size);
}

static inline long user_kernel_close_file_mapping(unsigned long handle) {
    return call_sys_file_mapping_close(handle);
}

static inline unsigned long user_kernel_map_file_view(unsigned long handle, unsigned long offset, unsigned long size) {
    return call_sys_file_mapping_map(handle, offset, size);
}

static inline long user_kernel_unmap_file_view(unsigned long address) {
    return call_sys_file_mapping_unmap(address);
}

static inline long user_kernel_unload_driver(const char* path) {
    return call_sys_driver_unload(path);
}

static inline long user_kernel_get_name(char* buf, unsigned long size) {
    return call_sys_task_name(buf, size);
}

static inline long user_kernel_get_args(char* buf, unsigned long size) {
    return call_sys_task_args(buf, size);
}

static inline long user_kernel_spawn_with_args(const char* path, const char* name, const char* args) {
    return call_sys_spawn(path, name, args);
}

static inline long user_kernel_spawn(const char* path, const char* name) {
    return call_sys_spawn(path, name, 0);
}

static inline long user_kernel_console_read(void) {
    return call_sys_console_read();
}

static inline long user_kernel_read_file(const char* path, unsigned long offset, char* buf, unsigned long size) {
    return call_sys_read_file(path, offset, buf, size);
}

static inline long user_kernel_dir_entry(const char* path, unsigned long entry_index, UserDirectoryEntry* entry) {
    return call_sys_dir_entry(path, entry_index, entry);
}

static inline long user_kernel_path_info(const char* path, UserPathInfo* info) {
    return call_sys_path_info(path, info);
}

static inline long user_kernel_mkdir(const char* path) {
    return call_sys_mkdir(path);
}

static inline long user_kernel_remove(const char* path) {
    return call_sys_remove(path);
}

static inline long user_kernel_task_info(long pid, UserTaskInfo* info) {
    return call_sys_task_info(pid, info);
}

static inline long user_kernel_wait_pid(long pid, long* result) {
    return call_sys_wait_pid(pid, result);
}

static inline long user_kernel_mem_info(UserMemInfo* info) {
    return call_sys_mem_info(info);
}

static inline long user_kernel_kill(long pid) {
    return call_sys_kill(pid);
}

static inline long user_kernel_reboot(void) {
    return call_sys_reboot();
}

static inline long user_kernel_debug_shell(const char* line) {
    return call_sys_debug_shell(line);
}

/**
 * Invoke an extension function.
 * @param ext The name of the extension to invoke.
 * @param func The name of the function to invoke within the extension.
 * @param a The first argument to pass to the function.
 * @param b The second argument to pass to the function.
 * @return The result of the function call, or -1 if the extension or function was not found.
 */
static inline long user_kernel_extension_invoke(const char* ext, const char* func, unsigned long a, unsigned long b) {
    return call_sys_ext_invoke(ext, func, a, b);
}

static inline long user_kernel_module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result) {
    return call_sys_module_invoke(module_name, export_name, a, b, result);
}

static inline long user_kernel_exit(unsigned long code) {
    return call_sys_exit(code);
}

#endif
