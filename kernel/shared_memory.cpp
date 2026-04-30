#include "shared_memory.h"

#include "debug-message.h"
#include "mm.h"
#include "mm/physical.h"
#include "process.h"
#include "user_address_space_layout.h"

namespace {

    inline constexpr Size SharedMemoryNameCapacity = 64U;
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

    typedef struct SharedMemoryAttachment {
        bool in_use;
        Process* process;
        SharedMemoryObject* object;
        U32 slot_index;
        VirtAddr view_address;
        struct SharedMemoryAttachment* next;
    } SharedMemoryAttachment;

    typedef struct SharedMemoryRecordSlot {
        struct SharedMemoryRecordSlot* next_free;
    } SharedMemoryRecordSlot;

    typedef struct SharedMemoryRecordPageHeader {
        PhysAddr page_phys;
        struct SharedMemoryRecordPageHeader* next_page;
        U32 live_slots;
        U32 slot_capacity;
    } SharedMemoryRecordPageHeader;

    typedef struct SharedMemoryRecordPool {
        Size slot_bytes;
        Size slot_alignment;
        SharedMemoryRecordSlot* free_list;
        SharedMemoryRecordPageHeader* pages;
    } SharedMemoryRecordPool;

    bool g_shared_memory_initialized;
    SharedMemoryObject* g_shared_memory_objects;
    SharedMemoryAttachment* g_shared_memory_attachments;
    Size g_shared_memory_object_count;
    Size g_shared_memory_object_peak;
    Size g_shared_memory_attachment_count;
    Size g_shared_memory_attachment_peak;
    SharedMemoryRecordPool g_shared_memory_object_pool = {};
    SharedMemoryRecordPool g_shared_memory_attachment_pool = {};

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

    /*
     * Align one unsigned value upward to the next multiple of `alignment`.
     *
     * The page-backed metadata pool needs aligned slot starts so typed record
     * access stays valid after carving slots out of raw physical pages.
     *
     * @param value Raw value to align.
     * @param alignment Required power-of-two alignment.
     * @return Aligned value, or the original value when alignment is zero.
     */
    Size shared_memory_align_up(Size value, Size alignment) {
        if (alignment == 0U) {
            return value;
        }

        return (value + (alignment - 1U)) & ~(alignment - 1U);
    }

    /*
     * Initialize one shared-memory metadata pool lazily.
     *
     * Both object records and attachment records are fixed-size structs, so a
     * page-backed slot pool removes the subsystem's dependency on the general
     * heap while still allowing empty metadata pages to be reclaimed.
     *
     * @param pool Pool state to initialize.
     * @param record_bytes Raw record size.
     * @param record_alignment Natural record alignment.
     * @return Nothing.
     */
    void shared_memory_record_pool_init(SharedMemoryRecordPool* pool, Size record_bytes, Size record_alignment) {
        if ((pool == NULL) || (record_bytes == 0U) || (record_alignment == 0U)) {
            return;
        }
        if (pool->slot_bytes != 0U) {
            return;
        }

        pool->slot_alignment = record_alignment;
        pool->slot_bytes = shared_memory_align_up(record_bytes, record_alignment);
        if (pool->slot_bytes < sizeof(SharedMemoryRecordSlot)) {
            pool->slot_bytes = shared_memory_align_up(sizeof(SharedMemoryRecordSlot), record_alignment);
        }
        pool->free_list = NULL;
        pool->pages = NULL;
    }

    /*
     * Return the page header that owns one metadata slot.
     *
     * Every metadata pool page stores its header at the start of the page and
     * carves aligned record slots out of the remaining bytes.
     *
     * @param slot Slot pointer returned by the metadata pool.
     * @return Owning page header, or NULL for invalid input.
     */
    SharedMemoryRecordPageHeader* shared_memory_record_page_from_slot(const void* slot) {
        if (slot == NULL) {
            return NULL;
        }

        return reinterpret_cast<SharedMemoryRecordPageHeader*>(
            reinterpret_cast<Uptr>(slot) & ~(static_cast<Uptr>(mm::PageSize) - 1U));
    }

    /*
     * Grow one shared-memory metadata pool by one physical page.
     *
     * The subsystem keeps metadata on direct-mapped physical pages so both the
     * registry and the attachment list stay operational even when the kernel
     * heap is exhausted or corrupted.
     *
     * @param pool Pool to extend.
     * @return True when at least one new slot was added.
     */
    bool shared_memory_record_pool_grow(SharedMemoryRecordPool* pool) {
        const PhysAddr page_phys = mm::PhysicalMemory::alloc_page();
        U8* page_base;
        SharedMemoryRecordPageHeader* page_header;
        Uptr slot_start;
        Size available_bytes;
        Size slot_capacity;

        if ((pool == NULL) || (pool->slot_bytes == 0U) || (pool->slot_alignment == 0U) || (page_phys == 0U)) {
            return false;
        }

        page_base = reinterpret_cast<U8*>(mm::MemoryManager::physical_to_kernel(page_phys));
        memzero(page_base, mm::PageSize);
        page_header = reinterpret_cast<SharedMemoryRecordPageHeader*>(page_base);
        page_header->next_page = pool->pages;
        page_header->page_phys = page_phys;

        slot_start = shared_memory_align_up(
            reinterpret_cast<Uptr>(page_base + sizeof(SharedMemoryRecordPageHeader)),
            pool->slot_alignment);
        available_bytes = mm::PageSize - static_cast<Size>(slot_start - reinterpret_cast<Uptr>(page_base));
        slot_capacity = available_bytes / pool->slot_bytes;
        if (slot_capacity == 0U) {
            mm::PhysicalMemory::free_page(page_phys);
            return false;
        }

        page_header->slot_capacity = static_cast<U32>(slot_capacity);
        pool->pages = page_header;

        for (Size slot_index = 0U; slot_index < slot_capacity; ++slot_index) {
            SharedMemoryRecordSlot* slot = reinterpret_cast<SharedMemoryRecordSlot*>(slot_start + (slot_index * pool->slot_bytes));

            slot->next_free = pool->free_list;
            pool->free_list = slot;
        }

        return true;
    }

    /*
     * Allocate one metadata record from a page-backed pool.
     *
     * @param pool Pool supplying the record.
     * @return Zeroed record slot, or NULL when the pool cannot grow.
     */
    void* shared_memory_record_pool_allocate(SharedMemoryRecordPool* pool) {
        SharedMemoryRecordSlot* slot;
        SharedMemoryRecordPageHeader* page_header;

        if (pool == NULL) {
            return NULL;
        }
        if ((pool->free_list == NULL) && !shared_memory_record_pool_grow(pool)) {
            return NULL;
        }

        slot = pool->free_list;
        pool->free_list = slot->next_free;
        page_header = shared_memory_record_page_from_slot(slot);
        if (page_header != NULL) {
            page_header->live_slots += 1U;
        }
        memzero(slot, pool->slot_bytes);
        return slot;
    }

    /*
     * Remove every free-list slot that belongs to one retiring page.
     *
     * @param pool Pool whose free list is being filtered.
     * @param retired_page Page that is leaving the pool.
     * @return Nothing.
     */
    void shared_memory_record_pool_remove_page_slots(SharedMemoryRecordPool* pool, const SharedMemoryRecordPageHeader* retired_page) {
        SharedMemoryRecordSlot* retained_head = NULL;
        SharedMemoryRecordSlot* node;

        if ((pool == NULL) || (retired_page == NULL)) {
            return;
        }

        node = pool->free_list;
        while (node != NULL) {
            SharedMemoryRecordSlot* next = node->next_free;

            if (shared_memory_record_page_from_slot(node) != retired_page) {
                node->next_free = retained_head;
                retained_head = node;
            }
            node = next;
        }

        pool->free_list = retained_head;
    }

    /*
     * Unlink one page header from a metadata pool page chain.
     *
     * @param pool Pool that owns the page.
     * @param page_header Page being unlinked.
     * @return Nothing.
     */
    void shared_memory_record_pool_unlink_page(SharedMemoryRecordPool* pool, SharedMemoryRecordPageHeader* page_header) {
        SharedMemoryRecordPageHeader* current;
        SharedMemoryRecordPageHeader* previous = NULL;

        if ((pool == NULL) || (page_header == NULL)) {
            return;
        }

        current = pool->pages;
        while ((current != NULL) && (current != page_header)) {
            previous = current;
            current = current->next_page;
        }
        if (current == NULL) {
            return;
        }

        if (previous != NULL) {
            previous->next_page = current->next_page;
        }
        else {
            pool->pages = current->next_page;
        }
    }

    /*
     * Return one metadata record slot to its page-backed pool.
     *
     * Empty pages are reclaimed immediately so object churn does not leave dead
     * metadata pages behind after the last object or attachment is released.
     *
     * @param pool Pool receiving the slot.
     * @param record Slot pointer previously returned by the pool.
     * @return Nothing.
     */
    void shared_memory_record_pool_free(SharedMemoryRecordPool* pool, void* record) {
        SharedMemoryRecordPageHeader* page_header;
        SharedMemoryRecordSlot* slot;

        if ((pool == NULL) || (record == NULL)) {
            return;
        }

        page_header = shared_memory_record_page_from_slot(record);
        if (page_header == NULL) {
            return;
        }

        slot = reinterpret_cast<SharedMemoryRecordSlot*>(record);
        slot->next_free = pool->free_list;
        pool->free_list = slot;

        if (page_header->live_slots != 0U) {
            page_header->live_slots -= 1U;
        }
        if (page_header->live_slots != 0U) {
            return;
        }

        shared_memory_record_pool_remove_page_slots(pool, page_header);
        shared_memory_record_pool_unlink_page(pool, page_header);
        mm::PhysicalMemory::free_page(page_header->page_phys);
    }

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
     * Emit one detailed object-lifecycle log line.
     *
     * Heap corruption is currently surfacing during process teardown after the
     * shared-memory subsystem already released at least one attachment. Logging
     * the object name, backing pointer, and rounded backing size at the point
     * where the subsystem mutates ownership makes the next repro actionable
     * without needing to reverse-map a raw heap address by hand.
     *
     * @param reason Short label describing the lifecycle event.
     * @param object Shared-memory object associated with the event.
     * @param attachment Optional process attachment associated with the event.
     * @return Nothing.
     */
    void shared_memory_log_object_event(const char* reason, const SharedMemoryObject* object, const SharedMemoryAttachment* attachment) {
        KDEBUG(
            KZONE_MEMORY,
            "[shm] %s name=%s requested=%lu backing=%lu ptr=%p phys=0x%llx attachments=%lu process=%lld slot=%lu view=0x%llx\n",
            (reason != NULL) ? reason : "<none>",
            (object != NULL) ? object->name : "<null>",
            static_cast<unsigned long>((object != NULL) ? object->requested_size : 0U),
            static_cast<unsigned long>((object != NULL) ? object->backing_bytes : 0U),
            (object != NULL) ? object->backing : NULL,
            static_cast<unsigned long long>((object != NULL) ? object->physical_base : 0U),
            static_cast<unsigned long>((object != NULL) ? object->attachment_count : 0U),
            static_cast<long long>(((attachment != NULL) && (attachment->process != NULL)) ? attachment->process->id : -1),
            static_cast<unsigned long>((attachment != NULL) ? attachment->slot_index : SharedMemoryInvalidSlot),
            static_cast<unsigned long long>((attachment != NULL) ? attachment->view_address : 0U));
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

        shared_memory_log_object_event("object-alloc-detail", object, NULL);
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

        shared_memory_log_object_event("attachment-alloc-detail", attachment->object, attachment);
        shared_memory_log_registry_snapshot("attachment-alloc");
    }

    /*
     * Unlink one shared-memory object from the global registry.
     *
     * @param object Object record being destroyed.
     * @return Nothing.
     */
    void unlink_object(SharedMemoryObject* object) {
        shared_memory_log_object_event("object-release-detail", object, NULL);
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
        shared_memory_log_object_event("attachment-release-detail", (attachment != NULL) ? attachment->object : NULL, attachment);
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
        return user_address_space::SharedMemoryViewBase + (static_cast<VirtAddr>(slot_index) * user_address_space::SharedMemorySlotBytes);
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
        SharedMemoryObject* object;

        shared_memory_record_pool_init(&g_shared_memory_object_pool, sizeof(SharedMemoryObject), alignof(SharedMemoryObject));
        object = static_cast<SharedMemoryObject*>(shared_memory_record_pool_allocate(&g_shared_memory_object_pool));
        if (object == NULL) {
            shared_memory_log_registry_snapshot("object-alloc-failed");
            return NULL;
        }

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
     * The shared-memory view arena size now comes from the central EL0 layout
     * budget, so the allocator scans the derived slot capacity instead of one
     * compile-time constant baked into this subsystem.
     *
     * @param process Process receiving the mapping.
     * @return Slot index, or `SharedMemoryInvalidSlot` when none remain.
     */
    U32 find_free_slot(Process* process) {
        const Size slot_capacity = user_address_space::SharedMemoryViewBytes / user_address_space::SharedMemorySlotBytes;

        for (Size slot_index = 0U; slot_index < slot_capacity; ++slot_index) {
            if (!process_uses_slot(process, static_cast<U32>(slot_index))) {
                return static_cast<U32>(slot_index);
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
        SharedMemoryAttachment* attachment;

        shared_memory_record_pool_init(&g_shared_memory_attachment_pool, sizeof(SharedMemoryAttachment), alignof(SharedMemoryAttachment));
        attachment = static_cast<SharedMemoryAttachment*>(shared_memory_record_pool_allocate(&g_shared_memory_attachment_pool));
        if (attachment == NULL) {
            shared_memory_log_registry_snapshot("attachment-alloc-failed");
            return NULL;
        }

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
        if ((object->physical_base != 0U) && (object->backing_bytes != 0U)) {
            const unsigned int page_count = static_cast<unsigned int>(object->backing_bytes / mm::PageSize);

            mm::PhysicalMemory::release_contiguous_pages(object->physical_base, page_count);
        }
        memzero(object, sizeof(*object));
        shared_memory_record_pool_free(&g_shared_memory_object_pool, object);
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
        memzero(attachment, sizeof(*attachment));
        shared_memory_record_pool_free(&g_shared_memory_attachment_pool, attachment);
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
    if ((requested_size > user_address_space::SharedMemorySlotBytes) || !text_fits_capacity(name, SharedMemoryNameCapacity)) {
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
        const unsigned int page_count = static_cast<unsigned int>(backing_bytes / mm::PageSize);
        PhysAddr backing_phys;

        if (backing_bytes == 0U) {
            shared_memory_record_pool_free(&g_shared_memory_object_pool, reserved_object);
            return StatusInvalidArgument;
        }

        backing_phys = mm::PhysicalMemory::reserve_contiguous_pages(page_count);
        if ((page_count == 0U) || (backing_phys == 0U)) {
            shared_memory_record_pool_free(&g_shared_memory_object_pool, reserved_object);
            return StatusNoMemory;
        }

        backing = reinterpret_cast<void*>(mm::MemoryManager::physical_to_kernel(backing_phys));
        memzero(backing, backing_bytes);
        memzero(reserved_object, sizeof(*reserved_object));
        reserved_object->in_use = true;
        reserved_object->requested_size = requested_size;
        reserved_object->backing_bytes = backing_bytes;
        reserved_object->backing = backing;
        reserved_object->physical_base = backing_phys;
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
            memzero(attachment, sizeof(*attachment));
            shared_memory_record_pool_free(&g_shared_memory_attachment_pool, attachment);
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