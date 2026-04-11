#include "object.h"

#include "dll_loader.h"
#include "heap_list.h"
#include "process.h"
#include "thread.h"

namespace {

    // Kernel objects still keep stable self-owned storage, but the registry now
    // grows on demand instead of rejecting new objects at one compiled-in cap.
    HeapList<ObjectHeader*> g_objects;

    // Every registered kernel object now carries one stable identity directly in the shared header.
    U64 object_identity(const ObjectHeader* object) {
        if (object == NULL) {
            return 0;
        }

        return object->object_id;
    }

} // namespace

Status KernelObjectManager::init(void) {
    g_objects.clear();
    return StatusOK;
}

Status KernelObjectManager::register_object(ObjectHeader* object) {
    if (object == NULL) {
        return StatusInvalidArgument;
    }

    for (Size index = 0U; index < g_objects.count(); ++index) {
        if (g_objects[index] == object) {
            return StatusAlreadyExists;
        }
    }

    return g_objects.append(object);
}

Status KernelObjectManager::unregister_object(ObjectHeader* object) {
    if (object == NULL) {
        return StatusInvalidArgument;
    }

    for (Size index = 0U; index < g_objects.count(); ++index) {
        if (g_objects[index] == object) {
            g_objects.remove_at(index);
            return StatusOK;
        }
    }

    return StatusNotFound;
}

ObjectHeader* KernelObjectManager::find_object(ObjectType type, U64 object_id) {
    for (Size index = 0U; index < g_objects.count(); ++index) {
        ObjectHeader* object = g_objects[index];

        if ((object != NULL) && (object->type == type) && (object_identity(object) == object_id)) {
            return object;
        }
    }

    return NULL;
}

Status KernelObjectManager::reference_object(ObjectHeader* object) {
    if (object == NULL) {
        return StatusInvalidArgument;
    }

    object_ref(object);
    return StatusOK;
}

Status KernelObjectManager::dereference_object(ObjectHeader* object) {
    if (object == NULL) {
        return StatusInvalidArgument;
    }
    if (object_has_flag(object->flags, ObjectFlags::Permanent) && (object->ref_count <= 1U)) {
        return StatusBusy;
    }
    if (!object_unref(object)) {
        return StatusOK;
    }

    destroy_object(object);
    return StatusOK;
}

Size KernelObjectManager::object_count(void) {
    return g_objects.count();
}

void destroy_object(ObjectHeader* object) {
    if (object == NULL) {
        return;
    }

    // Centralized destruction keeps reference-count teardown consistent no matter which subsystem drops the last reference.
    switch (object->type) {
    case ObjectType::Process:
        (void)ProcessManager::destroy_process(reinterpret_cast<Process*>(object));
        break;
    case ObjectType::Thread:
        (void)ThreadManager::destroy_thread(reinterpret_cast<Thread*>(object));
        break;
    case ObjectType::Module:
        (void)DllLoader::destroy_module_object(reinterpret_cast<LoadedModule*>(object));
        break;
    default:
        (void)KernelObjectManager::unregister_object(object);
        memzero(object, sizeof(*object));
        break;
    }
}