/**
 * ros.h
 *
 * Shared public kernel ABI types that are intended to stay usable from both
 * kernel code and future user-space code.
 */

#ifndef KERNEL_ROS_H
#define KERNEL_ROS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

    /* Common ABI-facing aliases layered on top of the canonical scalar types. */
    typedef bool Bool;
    typedef Uptr Address;
    typedef Uptr Offset;
    typedef U64 Flags;
    typedef void* Pointer;
    typedef const void* ConstPointer;
    typedef U8* Buffer;
    typedef const U8* ConstBuffer;

    typedef U64 HandleValue;
    typedef U64 ObjectId;
    typedef U32 AccessMask;
    typedef U32 ObjectFlags;
    typedef U32 HandleFlags;

    enum {
        InvalidHandleValue = 0,
        InvalidObjectId = 0,
        ObjectNameCapacity = 64
    };

    typedef enum ObjectType {
        ObjectTypeInvalid = 0,
        ObjectTypeGeneric,
        ObjectTypeProcess,
        ObjectTypeThread,
        ObjectTypeModule,
        ObjectTypeService,
        ObjectTypeDevice,
        ObjectTypeFile,
        ObjectTypeDirectory,
        ObjectTypeMemory,
        ObjectTypeEvent,
        ObjectTypeMailbox,
        ObjectTypeChannel,
        ObjectTypeWindow
    } ObjectType;

    typedef enum AccessRight {
        AccessRightNone = 0,
        AccessRightRead = 1u << 0,
        AccessRightWrite = 1u << 1,
        AccessRightExecute = 1u << 2,
        AccessRightMap = 1u << 3,
        AccessRightWait = 1u << 4,
        AccessRightSignal = 1u << 5,
        AccessRightInspect = 1u << 6,
        AccessRightDuplicate = 1u << 7,
        AccessRightTransfer = 1u << 8,
        AccessRightDestroy = 1u << 9,
        AccessRightAdmin = 1u << 10,
        AccessRightAll = 0xffffffffu
    } AccessRight;

    typedef enum HandleFlag {
        HandleFlagNone = 0,
        HandleFlagInherited = 1u << 0,
        HandleFlagDuplicated = 1u << 1,
        HandleFlagProtectClose = 1u << 2,
        HandleFlagKernel = 1u << 3
    } HandleFlag;

    typedef enum ObjectFlag {
        ObjectFlagNone = 0,
        ObjectFlagKernel = 1u << 0,
        ObjectFlagUserVisible = 1u << 1,
        ObjectFlagNamed = 1u << 2,
        ObjectFlagPermanent = 1u << 3,
        ObjectFlagShared = 1u << 4,
        ObjectFlagReadable = 1u << 5,
        ObjectFlagWritable = 1u << 6,
        ObjectFlagExecutable = 1u << 7,
        ObjectFlagSignaled = 1u << 8
    } ObjectFlag;

    // a handle to any object
    typedef struct Handle {
        HandleValue value;          // kernel internal value
    } Handle;

    // information about a handle
    typedef struct HandleInfo {
        Handle handle;              // the handle itself
        ObjectId object_id;         // the ID of the object
        ObjectType type;            // the type of the object
        AccessMask granted_access;  // the access rights granted to the handle
        HandleFlags flags;          // flags associated with the handle
    } HandleInfo;

    typedef struct ObjectName {
        char text[ObjectNameCapacity];
    } ObjectName;

    typedef struct ObjectHeader {
        ObjectId id;
        ObjectType type;
        ObjectFlags flags;
        AccessMask allowed_access;
        U32 reference_count;
        U32 handle_count;
        ObjectName name;
    } ObjectHeader;

    typedef struct Object {
        Handle handle;
        ObjectHeader header;
    } Object;

    static inline Handle handle_invalid(void) {
        Handle handle = { InvalidHandleValue };
        return handle;
    }

    static inline bool handle_is_valid(Handle handle) {
        return handle.value != InvalidHandleValue;
    }

    static inline bool object_is_named(const Object* object) {
        return (object != NULL) && ((object->header.flags & ObjectFlagNamed) != 0u);
    }

    static inline bool object_has_access(const Object* object, AccessMask desired_access) {
        return (object != NULL) &&
            ((object->header.allowed_access & desired_access) == desired_access);
    }

#ifdef __cplusplus
}
#endif

#endif // KERNEL_ROS_H
