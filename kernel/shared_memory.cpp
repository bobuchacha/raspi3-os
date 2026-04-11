#include "shared_memory.h"

#include "debug-message.h"
#include "heap.h"
#include "mm.h"
#include "process.h"

namespace {

    inline constexpr Size SharedMemoryNameCapacity = 64U;
    inline constexpr Size SharedMemorySlotBytes = mm::backend::L2BlockSize;
    inline constexpr Size SharedMemoryViewSlotCount = 32U;
    inline constexpr VirtAddr SharedMemoryViewBase = 80ULL * mm::backend::L2BlockSize;
    inline constexpr U32 SharedMemoryInvalidSlot = 0xFFFFFFFFU;

    typedef struct SharedMemoryObject {
        bool in_use;
        char name[SharedMemoryNameCapacity];
        Size requested_size;
        Size backing_bytes;
        void* backing;
        PhysAddr physical_base;
        U32 attachment_count;
        struct SharedMemoryObject* next;
    } SharedMemoryObject;

    /*
     * Round one shared-memory payload size up to page granularity.
     *
     * Shared-memory views still occupy fixed 2 MiB virtual slots in EL0, but
     * the physical backing only needs enough pages to cover the requested
     * payload. This removes the previous 2 MiB minimum charge for tiny objects
     * such as font cache headers and small shared state structs.
     *
     * @param value Requested payload size.
     * @return Page-aligned backing size.
     */
    Size align_up_shared_memory_bytes(Size value) {
        const Size mask = mm::PageSize - 1U;

        if (value == 0U) {
            return 0U;
        }

        return (value + mask) & ~mask;
    }

    typedef struct SharedMemoryAttachment {
        bool in_use;
        Process* process;
        SharedMemoryObject* object;
        U32 slot_index;
        VirtAddr view_address;
        struct SharedMemoryAttachment* next;
    } SharedMemoryAttachment;

    bool g_shared_memory_initialized;
    SharedMemoryObject* g_shared_memory_objects;
    SharedMemoryAttachment* g_shared_memory_attachments;
    Size g_shared_memory_object_count;
    Size g_shared_memory_object_peak;
    Size g_shared_memory_attachment_count;
    Size g_shared_memory_attachment_peak;

    /*
     * Emit one shared-memory registry occupancy snapshot.
     *
     * These counters make it obvious whether later multi-process GUI scaling is
     * running into object churn, attachment churn, or some deeper VA-layout
     * ceiling. Shared-memory objects are relatively infrequent, so logging on
     * structural changes is cheap and useful.
     *
     * @param reason Short label describing the triggering event.
     * @return Nothing.
     */
    void shared_memory_log_registry_snapshot(const char* reason) {
        KDEBUG(
            KZONE_MEMORY,
            "[shm] registry reason=%s objects=%lu peak=%lu attachments=%lu peak=%lu\n",
            (reason != NULL) ? reason : "<none>",
            static_cast<unsigned long>(g_shared_memory_object_count),
            static_cast<unsigned long>(g_shared_memory_object_peak),
            static_cast<unsigned long>(g_shared_memory_attachment_count),
            static_cast<unsigned long>(g_shared_memory_attachment_peak));
    }

    /*
     * Link one live shared-memory object into the global registry.
     *
     * @param object Freshly created object record.
     * @return Nothing.
     */
    void link_object(SharedMemoryObject* object) {
        if (object == NULL) {
            return;
        }

        object->next = g_shared_memory_objects;
        g_shared_memory_objects = object;
        ++g_shared_memory_object_count;
        if (g_shared_memory_object_count > g_shared_memory_object_peak) {
            g_shared_memory_object_peak = g_shared_memory_object_count;
        }

        shared_memory_log_registry_snapshot("object-alloc");
    }

    /*
     * Link one live process attachment into the global registry.
     *
     * @param attachment Freshly created attachment record.
     * @return Nothing.
     */
    void link_attachment(SharedMemoryAttachment* attachment) {
        if (attachment == NULL) {
            return;
        }

        attachment->next = g_shared_memory_attachments;
        g_shared_memory_attachments = attachment;
        ++g_shared_memory_attachment_count;
        if (g_shared_memory_attachment_count > g_shared_memory_attachment_peak) {
            g_shared_memory_attachment_peak = g_shared_memory_attachment_count;
        }

        shared_memory_log_registry_snapshot("attachment-alloc");
    }

    /*
     * Unlink one shared-memory object from the global registry.
     *
     * @param object Object record being destroyed.
     * @return Nothing.
     */
    void unlink_object(SharedMemoryObject* object) {
        for (SharedMemoryObject** link = &g_shared_memory_objects; *link != NULL; link = &((*link)->next)) {
            if (*link == object) {
                *link = object->next;
                if (g_shared_memory_object_count != 0U) {
                    --g_shared_memory_object_count;
                }
                shared_memory_log_registry_snapshot("object-release");
                return;
            }
        }
    }

    /*
     * Unlink one shared-memory attachment from the global registry.
     *
     * @param attachment Attachment record being destroyed.
     * @return Nothing.
     */
    void unlink_attachment(SharedMemoryAttachment* attachment) {
        for (SharedMemoryAttachment** link = &g_shared_memory_attachments; *link != NULL; link = &((*link)->next)) {
            if (*link == attachment) {
                *link = attachment->next;
                if (g_shared_memory_attachment_count != 0U) {
                    --g_shared_memory_attachment_count;
                }
                shared_memory_log_registry_snapshot("attachment-release");
                return;
            }
        }
    }

    /*
     * Fold one ASCII character to lowercase for case-insensitive names.
     *
     * @param ch Character to normalize.
     * @return Lowercase ASCII value, or the input byte when no fold exists.
     */
    char ascii_lower(char ch) {
        if ((ch >= 'A') && (ch <= 'Z')) {
            return static_cast<char>(ch - 'A' + 'a');
        }

        return ch;
    }

    /*
     * Compare two ASCII strings case-insensitively.
     *
     * Shared-memory names act like logical object identifiers, so a consistent
     * case fold avoids accidental duplication between callers.
     *
     * @param lhs Left-hand string.
     * @param rhs Right-hand string.
     * @return True when both names match.
     */
    bool same_text_case_insensitive(const char* lhs, const char* rhs) {
        Size index = 0U;

        if (lhs == rhs) {
            return true;
        }
        if ((lhs == NULL) || (rhs == NULL)) {
            return false;
        }

        while ((lhs[index] != '\0') && (rhs[index] != '\0')) {
            if (ascii_lower(lhs[index]) != ascii_lower(rhs[index])) {
                return false;
            }
            ++index;
        }

        return lhs[index] == rhs[index];
    }

    /*
     * Validate that one null-terminated name fits inside the fixed object slot.
     *
     * Rejecting oversize names here prevents silent truncation from merging two
     * distinct logical objects into the same backing block.
     *
     * @param text Candidate shared-memory name.
     * @param capacity Destination slot capacity including the terminator.
     * @return True when the text fits exactly.
     */
    bool text_fits_capacity(const char* text, Size capacity) {
        Size index = 0U;

        if ((text == NULL) || (text[0] == '\0') || (capacity == 0U)) {
            return false;
        }

        while (text[index] != '\0') {
            if ((index + 1U) >= capacity) {
                return false;
            }
            ++index;
        }

        return true;
    }

    /*
     * Copy one validated null-terminated name into fixed storage.
     *
     * @param destination Destination buffer.
     * @param capacity Destination buffer capacity.
     * @param source Source string already known to fit.
     * @return Nothing.
     */
    void copy_text(char* destination, Size capacity, const char* source) {
        Size index = 0U;

        if ((destination == NULL) || (capacity == 0U) || (source == NULL)) {
            return;
        }

        while ((source[index] != '\0') && ((index + 1U) < capacity)) {
            destination[index] = source[index];
            ++index;
        }

        destination[index] = '\0';
    }

    /*
     * Return the fixed EL0 base address for one shared-memory slot.
     *
     * The MMU currently maps whole L2 blocks only, so each attachment lives in
     * a reserved per-process slot range instead of a variable-sized allocator.
     *
     * @param slot_index Slot index owned by one process attachment.
     * @return Stable user virtual address for that slot.
     */
    VirtAddr shared_memory_slot_address(U32 slot_index) {
        return SharedMemoryViewBase + (static_cast<VirtAddr>(slot_index) * SharedMemorySlotBytes);
    }

    /*
     * Find one named shared-memory object.
     *
     * @param name Logical object name.
     * @return Matching object, or NULL when no object exists yet.
     */
    SharedMemoryObject* find_object_by_name(const char* name) {
        for (SharedMemoryObject* object = g_shared_memory_objects; object != NULL; object = object->next) {

            if (object->in_use && same_text_case_insensitive(object->name, name)) {
                return object;
            }
        }

        return NULL;
    }

    /*
     * Allocate one heap-backed object record.
     *
     * @return Free object record, or NULL when allocation fails.
     */
    SharedMemoryObject* reserve_object_slot(void) {
        SharedMemoryObject* object = static_cast<SharedMemoryObject*>(Heap::alloc(sizeof(SharedMemoryObject), alignof(SharedMemoryObject)));

        if (object == NULL) {
            shared_memory_log_registry_snapshot("object-alloc-failed");
            return NULL;
        }

        memzero(object, sizeof(*object));
        return object;
    }

    /*
     * Find one process-local attachment for the requested object name.
     *
     * Reusing the first mapping keeps repeated opens idempotent within one
     * process and avoids burning extra virtual slots.
     *
     * @param process Calling process.
     * @param name Object name to find.
     * @return Existing attachment, or NULL when the process has not mapped it.
     */
    SharedMemoryAttachment* find_attachment(Process* process, const char* name) {
        for (SharedMemoryAttachment* attachment = g_shared_memory_attachments; attachment != NULL; attachment = attachment->next) {

            if (!attachment->in_use || (attachment->process != process) || (attachment->object == NULL)) {
                continue;
            }
            if (same_text_case_insensitive(attachment->object->name, name)) {
                return attachment;
            }
        }

        return NULL;
    }

    /*
     * Report whether one attachment slot is already occupied inside a process.
     *
     * @param process Process that owns the view range.
     * @param slot_index Candidate slot number.
     * @return True when the slot is already in use for that process.
     */
    bool process_uses_slot(Process* process, U32 slot_index) {
        for (const SharedMemoryAttachment* attachment = g_shared_memory_attachments; attachment != NULL; attachment = attachment->next) {

            if (attachment->in_use && (attachment->process == process) && (attachment->slot_index == slot_index)) {
                return true;
            }
        }

        return false;
    }

    /*
     * Choose one free per-process view slot.
     *
     * @param process Process receiving the mapping.
     * @return Slot index, or `SharedMemoryInvalidSlot` when none remain.
     */
    U32 find_free_slot(Process* process) {
        for (U32 slot_index = 0U; slot_index < SharedMemoryViewSlotCount; ++slot_index) {
            if (!process_uses_slot(process, slot_index)) {
                return slot_index;
            }
        }

        return SharedMemoryInvalidSlot;
    }

    /*
     * Allocate one heap-backed attachment record.
     *
     * @return Free attachment record, or NULL when allocation fails.
     */
    SharedMemoryAttachment* reserve_attachment_slot(void) {
        SharedMemoryAttachment* attachment = static_cast<SharedMemoryAttachment*>(Heap::alloc(sizeof(SharedMemoryAttachment), alignof(SharedMemoryAttachment)));

        if (attachment == NULL) {
            shared_memory_log_registry_snapshot("attachment-alloc-failed");
            return NULL;
        }

        memzero(attachment, sizeof(*attachment));
        return attachment;
    }

    /*
     * Free one object that no process still references.
     *
     * @param object Object that may be eligible for destruction.
     * @return Nothing.
     */
    void release_object_if_unused(SharedMemoryObject* object) {
        if ((object == NULL) || !object->in_use || (object->attachment_count != 0U)) {
            return;
        }

        unlink_object(object);
        if (object->backing != NULL) {
            Heap::free(object->backing);
        }
        Heap::free(object);
    }

    /*
     * Drop one process attachment and optionally unmap its EL0 view.
     *
     * @param attachment Attachment record to release.
     * @param process_address_space_alive True when the process can still unmap.
     * @return StatusOK after the attachment was released.
     */
    Status release_attachment(SharedMemoryAttachment* attachment, bool process_address_space_alive) {
        SharedMemoryObject* object;

        if ((attachment == NULL) || !attachment->in_use || (attachment->object == NULL)) {
            return StatusInvalidArgument;
        }

        object = attachment->object;
        if (process_address_space_alive && (attachment->process != NULL) && (attachment->process->process_address_space.page_table_root != 0U)) {
            (void)mm::MemoryManager::unmap(
                &attachment->process->process_address_space,
                attachment->view_address,
                object->backing_bytes);
        }

        if (object->attachment_count != 0U) {
            --object->attachment_count;
        }
        unlink_attachment(attachment);
        Heap::free(attachment);
        release_object_if_unused(object);
        return StatusOK;
    }

} // namespace

Status SharedMemoryManager::init(void) {
    if (!g_shared_memory_initialized) {
        g_shared_memory_objects = NULL;
        g_shared_memory_attachments = NULL;
        g_shared_memory_object_count = 0U;
        g_shared_memory_object_peak = 0U;
        g_shared_memory_attachment_count = 0U;
        g_shared_memory_attachment_peak = 0U;
        g_shared_memory_initialized = true;
    }

    return StatusOK;
}

Status SharedMemoryManager::acquire(Process* process, const char* name, Size requested_size, VirtAddr* address_out) {
    SharedMemoryObject* object = NULL;
    SharedMemoryAttachment* attachment;
    void* backing = NULL;
    U32 slot_index;
    bool created_object = false;
    Status status;

    if ((process == NULL) || (name == NULL) || (address_out == NULL)) {
        return StatusInvalidArgument;
    }
    if ((requested_size > SharedMemorySlotBytes) || !text_fits_capacity(name, SharedMemoryNameCapacity)) {
        return StatusInvalidArgument;
    }

    status = init();
    if (status != StatusOK) {
        return status;
    }

    attachment = find_attachment(process, name);
    if (attachment != NULL) {
        *address_out = attachment->view_address;
        return StatusOK;
    }

    object = find_object_by_name(name);
    if (object == NULL) {
        SharedMemoryObject* reserved_object;

        if (requested_size == 0U) {
            return StatusNotFound;
        }

        reserved_object = reserve_object_slot();
        if (reserved_object == NULL) {
            return StatusNoSpace;
        }

        const Size backing_bytes = align_up_shared_memory_bytes(requested_size);

        if (backing_bytes == 0U) {
            return StatusInvalidArgument;
        }

        backing = Heap::alloc(backing_bytes, mm::PageSize);
        if (backing == NULL) {
            return StatusNoMemory;
        }

        memzero(backing, backing_bytes);
        memzero(reserved_object, sizeof(*reserved_object));
        reserved_object->in_use = true;
        reserved_object->requested_size = requested_size;
        reserved_object->backing_bytes = backing_bytes;
        reserved_object->backing = backing;
        reserved_object->physical_base = mm::MemoryManager::kernel_to_physical(reinterpret_cast<VirtAddr>(backing));
        reserved_object->attachment_count = 0U;
        copy_text(reserved_object->name, sizeof(reserved_object->name), name);
        link_object(reserved_object);
        object = reserved_object;
        created_object = true;
    }
    else if ((requested_size != 0U) && (requested_size > object->requested_size)) {
        return StatusNoSpace;
    }

    slot_index = find_free_slot(process);
    if (slot_index == SharedMemoryInvalidSlot) {
        if (created_object) {
            release_object_if_unused(object);
        }
        return StatusNoSpace;
    }

    attachment = reserve_attachment_slot();
    if (attachment == NULL) {
        if (created_object) {
            release_object_if_unused(object);
        }
        return StatusNoSpace;
    }

    {
        const VirtAddr view_address = shared_memory_slot_address(slot_index);
        const VmMapping mapping = {
            view_address,
            object->physical_base,
            object->backing_bytes,
            PagePresent | PageWritable | PageUser,
        };

        status = mm::MemoryManager::map(&process->process_address_space, &mapping);
        if (status != StatusOK) {
            Heap::free(attachment);
            if (created_object) {
                release_object_if_unused(object);
            }
            return status;
        }

        memzero(attachment, sizeof(*attachment));
        attachment->in_use = true;
        attachment->process = process;
        attachment->object = object;
        attachment->slot_index = slot_index;
        attachment->view_address = view_address;
        link_attachment(attachment);
        object->attachment_count += 1U;
        *address_out = view_address;
    }

    return StatusOK;
}

Status SharedMemoryManager::release_process_mappings(Process* process) {
    const bool process_address_space_alive = (process != NULL) && (process->process_address_space.page_table_root != 0U);

    if (process == NULL) {
        return StatusInvalidArgument;
    }

    for (SharedMemoryAttachment* attachment = g_shared_memory_attachments; attachment != NULL;) {
        SharedMemoryAttachment* next = attachment->next;

        if (!attachment->in_use || (attachment->process != process)) {
            attachment = next;
            continue;
        }

        (void)release_attachment(attachment, process_address_space_alive);
        attachment = next;
    }

    return StatusOK;
}