/*
 * ldr_kernel.h
 *
 * Kernel-facing helpers exported by the user runtime / loader bridge.
 */
#ifndef ROS_KERNEL_MY_LOADER_LDR_KERNEL_H
#define ROS_KERNEL_MY_LOADER_LDR_KERNEL_H

#ifdef __cplusplus
extern "C" {
#endif

    int ldr_kernel_release_user_modules_for_task(void* task_ptr);
    int ldr_kernel_forget_user_modules_for_task(void* task_ptr);
    int ldr_kernel_unload_user_shared_library(void* task_ptr, const char* path);
    int ldr_kernel_unload_user_driver(void* task_ptr, const char* path);

#ifdef __cplusplus
}
#endif

#endif