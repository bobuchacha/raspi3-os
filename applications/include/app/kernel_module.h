#ifndef ROS_APP_KERNEL_MODULE_H
#define ROS_APP_KERNEL_MODULE_H

#include "stddef.h"
#include "stdint.h"

#define ROS_KERNEL_MODULE_ABI_VERSION 1U
#define ROS_KERNEL_MODULE_EMBEDDED_MAGIC "ROSKMETA"
#define ROS_KERNEL_MODULE_EMBEDDED_VERSION 1U
#define ROS_KERNEL_MODULE_EMBEDDED_NAME_MAX 64U
#define ROS_KERNEL_MODULE_EMBEDDED_SYMBOL_MAX 64U
#define ROS_KERNEL_MODULE_EMBEDDED_EXPORT_MAX 4U

#define ROS_KERNEL_MODULE_METADATA_SECTION __attribute__((section(".ros.module.meta"), used))
#define ROS_KERNEL_MODULE_EXPORT(name, symbol_name) { name, #symbol_name }

typedef struct RosKernelModuleApi {
    uint32_t abi_version;
    uint32_t reserved0;
    void (*logf)(const char* level, const char* module_name, const char* fmt, ...);
    void* (*alloc)(size_t size, size_t align);
    void (*free)(void* ptr);
    uint64_t(*ticks_ms)(void);
} RosKernelModuleApi;

typedef int (*RosKernelModuleInit)(const RosKernelModuleApi* api);
typedef void (*RosKernelModuleShutdown)(void);
typedef void (*RosKernelModuleIdle)(uint64_t now_ms);

typedef struct RosKernelModuleEmbeddedExport {
    char name[16];
    char symbol[ROS_KERNEL_MODULE_EMBEDDED_SYMBOL_MAX];
} __attribute__((packed)) RosKernelModuleEmbeddedExport;

typedef struct RosKernelModuleEmbeddedMetadata {
    char magic[8];
    uint32_t version;
    uint32_t flags;
    char name[ROS_KERNEL_MODULE_EMBEDDED_NAME_MAX];
    char init[ROS_KERNEL_MODULE_EMBEDDED_SYMBOL_MAX];
    char shutdown[ROS_KERNEL_MODULE_EMBEDDED_SYMBOL_MAX];
    char idle[ROS_KERNEL_MODULE_EMBEDDED_SYMBOL_MAX];
    uint32_t export_count;
    RosKernelModuleEmbeddedExport exports[ROS_KERNEL_MODULE_EMBEDDED_EXPORT_MAX];
} __attribute__((packed)) RosKernelModuleEmbeddedMetadata;

#endif
