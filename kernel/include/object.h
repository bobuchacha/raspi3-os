#pragma once

#include "types.h"

#if !defined(__cplusplus)
#error "object.h requires C++"
#endif

enum class ObjectFlags : U32 {
    None = 0,
    Permanent = 1U << 0,
    Kernel = 1U << 1,
};

inline constexpr ObjectFlags operator|(ObjectFlags lhs, ObjectFlags rhs) {
    return static_cast<ObjectFlags>(static_cast<U32>(lhs) | static_cast<U32>(rhs));
}

inline constexpr bool object_has_flag(ObjectFlags value, ObjectFlags flag) {
    return (static_cast<U32>(value) & static_cast<U32>(flag)) != 0U;
}

enum class ObjectType : U32 {
    Unknown = 0,
    Process,
    Thread,
    Module,
    Event,
    Mutex,
};

struct ObjectHeader {
    ObjectType type;
    ObjectFlags flags;
    U64 object_id;
    U64 ref_count;
};

inline void object_ref(ObjectHeader* object) {
    if (object != NULL) {
        ++object->ref_count;
    }
}

inline bool object_unref(ObjectHeader* object) {
    if ((object == NULL) || (object->ref_count == 0U)) {
        return false;
    }

    --object->ref_count;
    return object->ref_count == 0U;
}

class KernelObjectManager final {
public:
    static Status init(void);
    static Status register_object(ObjectHeader* object);
    static Status unregister_object(ObjectHeader* object);
    static ObjectHeader* find_object(ObjectType type, U64 object_id);
    static Status reference_object(ObjectHeader* object);
    static Status dereference_object(ObjectHeader* object);
    static Size object_count(void);
};

void destroy_object(ObjectHeader* object);