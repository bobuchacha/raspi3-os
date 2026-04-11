#ifndef KERNEL_INCLUDE_KERNEL_EVENT_H
#define KERNEL_INCLUDE_KERNEL_EVENT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

    /* Keep the event ABI versioned so subscribers can reject mismatched layouts explicitly. */
#define KERNEL_EVENT_ABI_VERSION 1U
#define KERNEL_EVENT_QUEUE_INITIAL_CAPACITY 16U
#define KERNEL_EVENT_MAX_READ_BATCH 16U
#define KERNEL_EVENT_NAME_CAPACITY 64U
#define KERNEL_EVENT_PATH_CAPACITY 160U

    typedef U64 KernelEventSubscriptionId;
    typedef U64 KernelEventFamilyMask;

#define KERNEL_EVENT_FAMILY_NONE (0ULL)
#define KERNEL_EVENT_FAMILY_PROCESS (1ULL << 0)
#define KERNEL_EVENT_FAMILY_THREAD (1ULL << 1)
#define KERNEL_EVENT_FAMILY_FILESYSTEM (1ULL << 2)
#define KERNEL_EVENT_FAMILY_REGISTRATION (1ULL << 3)
#define KERNEL_EVENT_FAMILY_MODULE (1ULL << 4)
#define KERNEL_EVENT_FAMILY_ALL \
        (KERNEL_EVENT_FAMILY_PROCESS \
        | KERNEL_EVENT_FAMILY_THREAD \
        | KERNEL_EVENT_FAMILY_FILESYSTEM \
        | KERNEL_EVENT_FAMILY_REGISTRATION \
        | KERNEL_EVENT_FAMILY_MODULE)

    typedef enum KernelEventType {
        KernelEventTypeNone = 0,

        KernelEventTypeProcessCreated = 1,
        KernelEventTypeProcessExited = 2,

        KernelEventTypeThreadCreated = 16,
        KernelEventTypeThreadExited = 17,
        KernelEventTypeThreadReady = 18,
        KernelEventTypeThreadRunning = 19,

        KernelEventTypeFilesystemCreated = 32,
        KernelEventTypeFilesystemRemoved = 33,

        KernelEventTypeDeviceRegistered = 48,
        KernelEventTypeDriverRegistered = 49,
        KernelEventTypeFilesystemDriverRegistered = 50,

        KernelEventTypeModuleLoaded = 64,
        KernelEventTypeModuleUnloaded = 65,
        KernelEventTypeModuleInvoked = 66,
        KernelEventTypeSharedLibraryOpened = 67,
        KernelEventTypeSharedLibraryClosed = 68,
        KernelEventTypeSharedLibraryExported = 69,
        KernelEventTypeDriverUnloaded = 70,
    } KernelEventType;

    typedef enum KernelEventRegistrationKind {
        KernelEventRegistrationKindNone = 0,
        KernelEventRegistrationKindDevice = 1,
        KernelEventRegistrationKindDriver = 2,
        KernelEventRegistrationKindFilesystemDriver = 3,
    } KernelEventRegistrationKind;

    typedef struct KernelEventProcessData {
        U64 parent_process_id;
        I64 exit_code;
        U32 process_state;
        U32 process_flags;
    } KernelEventProcessData;

    typedef struct KernelEventThreadData {
        U64 parent_process_id;
        U32 previous_state;
        U32 current_state;
        U32 priority;
        U32 reserved0;
    } KernelEventThreadData;

    typedef struct KernelEventFilesystemData {
        U32 node_type;
        U32 reserved0;
        U64 size_bytes;
        U64 reserved1;
    } KernelEventFilesystemData;

    typedef struct KernelEventRegistrationData {
        U32 registration_kind;
        U32 reserved0;
        U64 reserved1;
        U64 reserved2;
    } KernelEventRegistrationData;

    typedef struct KernelEventModuleData {
        I64 result_code;
        U64 value0;
        U64 value1;
        U64 reserved0;
    } KernelEventModuleData;

    typedef union KernelEventPayload {
        KernelEventProcessData process;
        KernelEventThreadData thread;
        KernelEventFilesystemData filesystem;
        KernelEventRegistrationData registration;
        KernelEventModuleData module;
        U8 raw[32];
    } KernelEventPayload;

    typedef struct KernelEventRecord {
        U32 version;
        U32 size;
        U64 sequence;
        U64 uptime_msec;
        KernelEventFamilyMask family;
        U32 type;
        I32 status;
        U64 source_process_id;
        U64 source_thread_id;
        U64 source_object_id;
        char name[KERNEL_EVENT_NAME_CAPACITY];
        char path[KERNEL_EVENT_PATH_CAPACITY];
        KernelEventPayload payload;
    } KernelEventRecord;

    typedef struct KernelEventSubscriptionRequest {
        U32 version;
        U32 flags;
        KernelEventFamilyMask family_mask;
        U64 process_id;
        U64 thread_id;
        char name_prefix[KERNEL_EVENT_NAME_CAPACITY];
        char path_prefix[KERNEL_EVENT_PATH_CAPACITY];
    } KernelEventSubscriptionRequest;

    typedef struct KernelEventSubscriptionInfo {
        U32 version;
        U32 flags;
        KernelEventSubscriptionId subscription_id;
        U64 owner_process_id;
        KernelEventFamilyMask family_mask;
        U64 process_id;
        U64 thread_id;
        U32 queued_count;
        U32 capacity;
        U64 next_sequence;
        U64 newest_sequence;
        U64 dropped_count;
        char name_prefix[KERNEL_EVENT_NAME_CAPACITY];
        char path_prefix[KERNEL_EVENT_PATH_CAPACITY];
    } KernelEventSubscriptionInfo;

#ifdef __cplusplus
}
#endif

#endif // KERNEL_INCLUDE_KERNEL_EVENT_H