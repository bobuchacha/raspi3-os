#ifndef ROS_APP_IMPORT_H
#define ROS_APP_IMPORT_H

#include "stddef.h"
#include "stdint.h"
#include "stdarg.h"
#include "stdio.h"
#include "string.h"

#include "app/kernel.h"
#include "app/system_ext.h"
#include "app/syscall.h"

typedef unsigned long Address;
typedef unsigned long ULong;
typedef unsigned int UInt;
typedef unsigned short UShort;
typedef unsigned char UByte;
typedef long Long;
typedef int Int;
typedef short Short;
typedef char Byte;
typedef int Bool;

#define true 1
#define false 0
#define null ((void*)0)
#define TRUE true
#define FALSE false

typedef void (*Function)(void);

#define STATUS_SUCCESS 0
#define STATUS_FAILURE 1
#define STATUS_NOT_FOUND 2
#define STATUS_INVALID 3
#define STATUS_ERROR 4

typedef int Status;

#define DLL_ACCESSOR(api_type, accessor_name, entry_type, path_literal) \
static inline const api_type* accessor_name(void) { \
    static const api_type* cached_api = 0; \
    if (!cached_api) { \
        entry_type entry = (entry_type)user_kernel_open_shared_library(path_literal); \
        if (!entry) { \
            return 0; \
        } \
        cached_api = entry(); \
    } \
    return cached_api; \
}

#define ROS_DLL_ACCESSOR DLL_ACCESSOR

#define ROS_DLL_IMPORT(ret_type, wrapper_name, params, args, accessor_name, field_name) \
static inline ret_type wrapper_name params { \
    const decltype(accessor_name()) api = accessor_name(); \
    if (!api || !api->field_name) { \
        return (ret_type)0; \
    } \
    return api->field_name args; \
}

#define ROS_DLL_IMPORT_VOID(wrapper_name, params, args, accessor_name, field_name) \
static inline void wrapper_name params { \
    const decltype(accessor_name()) api = accessor_name(); \
    if (!api || !api->field_name) { \
        return; \
    } \
    api->field_name args; \
}

#define ROS_DLL_IMPORT_AS(ret_type, wrapper_name, params, args, accessor_name, field_name) \
    ROS_DLL_IMPORT(ret_type, wrapper_name, params, args, accessor_name, field_name)

#define ROS_DLL_IMPORT_AS_VOID(wrapper_name, params, args, accessor_name, field_name) \
    ROS_DLL_IMPORT_VOID(wrapper_name, params, args, accessor_name, field_name)

#define DECLARE(ret_type, wrapper_name, params, args, accessor_name) \
    ROS_DLL_IMPORT(ret_type, wrapper_name, params, args, accessor_name, wrapper_name)

#define DECLARE_AS(ret_type, wrapper_name, params, args, accessor_name, field_name) \
    ROS_DLL_IMPORT_AS(ret_type, wrapper_name, params, args, accessor_name, field_name)

#define DLL_IMPORT(module_path, export_name, ret_type, wrapper_name, params, args) \
static inline ret_type wrapper_name params { \
    typedef ret_type (*wrapper_name##_fn) params; \
    static wrapper_name##_fn cached_fn = 0; \
    if (!cached_fn) { \
        cached_fn = (wrapper_name##_fn)(unsigned long)user_kernel_shared_library_export(module_path, export_name); \
        if (!cached_fn) { \
            return (ret_type)0; \
        } \
    } \
    return cached_fn args; \
}

#define DLL_IMPORT_VOID(module_path, export_name, wrapper_name, params, args) \
static inline void wrapper_name params { \
    typedef void (*wrapper_name##_fn) params; \
    static wrapper_name##_fn cached_fn = 0; \
    if (!cached_fn) { \
        cached_fn = (wrapper_name##_fn)(unsigned long)user_kernel_shared_library_export(module_path, export_name); \
        if (!cached_fn) { \
            return; \
        } \
    } \
    cached_fn args; \
}

#define IMPORT_DLL DLL_IMPORT
#define IMPORT_DLL_VOID DLL_IMPORT_VOID

#define DLL_IMPORT_DECL(ret_type, wrapper_name, params, args, module_path, export_name) \
    DLL_IMPORT(module_path, export_name, ret_type, wrapper_name, params, args)

#define DLL_IMPORT_DECL_VOID(wrapper_name, params, args, module_path, export_name) \
    DLL_IMPORT_VOID(module_path, export_name, wrapper_name, params, args)

#define IMPORT_DLL_DECL DLL_IMPORT_DECL
#define IMPORT_DLL_DECL_VOID DLL_IMPORT_DECL_VOID

#define MODULE_IMPORT(module_name, export_name, ret_type, wrapper_name, params, args) \
static inline ret_type wrapper_name params { \
    long import_result = 0; \
    if (user_kernel_module_invoke(module_name, export_name, args, &import_result) != 0) { \
        return (ret_type)0; \
    } \
    return (ret_type)import_result; \
}

#define MODULE_IMPORT_VOID(module_name, export_name, wrapper_name, params, args) \
static inline void wrapper_name params { \
    long import_result = 0; \
    if (user_kernel_module_invoke(module_name, export_name, args, &import_result) != 0) { \
        return; \
    } \
}

#define IMPORT_MODULE MODULE_IMPORT
#define IMPORT_MODULE_VOID MODULE_IMPORT_VOID

#endif