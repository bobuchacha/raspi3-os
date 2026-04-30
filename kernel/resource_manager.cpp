#include "resource_manager.h"

#include "arch.h"
#include "debug-message.h"
#include "heap.h"

#undef KZONE_LOADER
#define KZONE_LOADER 0

namespace {

    inline constexpr Size KernelResourceLabelCapacity = 40U;

    typedef struct KernelResourceRecord {
        KernelResourceKind kind;
        bool owns_memory;
        U16 reserved0;
        U32 reserved1;
        Size size;
        U64 owner_id;
        void* address;
        char label[KernelResourceLabelCapacity];
        KernelResourceRecord* next;
        KernelResourceRecord* prev;
    } KernelResourceRecord;

    bool g_kernel_resource_initialized = false;
    KernelResourceRecord* g_kernel_resource_head = NULL;
    KernelResourceRecord* g_kernel_resource_tail = NULL;
    KernelResourceStats g_kernel_resource_stats[static_cast<Size>(KernelResourceKind::Count)] = {};

    /*
     * Copy one stable debug label into fixed record storage.
     *
     * @param destination Fixed-capacity destination buffer.
     * @param source Optional source text.
     * @return Nothing.
     */
    void resource_copy_label(char* destination, const char* source) {
        Size index = 0U;

        if ((destination == NULL) || (KernelResourceLabelCapacity == 0U)) {
            return;
        }

        if (source == NULL) {
            source = "";
        }

        while ((source[index] != '\0') && ((index + 1U) < KernelResourceLabelCapacity)) {
            destination[index] = source[index];
            ++index;
        }

        destination[index] = '\0';
    }

    /*
     * Convert one resource kind into a readable debug label.
     *
     * @param kind Resource kind to describe.
     * @return Static text label for logs.
     */
    const char* resource_kind_name(KernelResourceKind kind) {
        switch (kind) {
        case KernelResourceKind::LoaderStackBacking:
            return "loader-stack";
        case KernelResourceKind::LoaderImageBacking:
            return "loader-image";
        case KernelResourceKind::LoaderModuleRecord:
            return "loader-module";
        case KernelResourceKind::LoaderSharedImageRecord:
            return "loader-shared-image";
        case KernelResourceKind::LoaderImageCachePayload:
            return "image-cache-payload";
        case KernelResourceKind::LoaderImageCacheRecord:
            return "image-cache-record";
        case KernelResourceKind::UserHeapAllocationRecord:
            return "user-heap-allocation";
        case KernelResourceKind::UserHeapMappedPageRecord:
            return "user-heap-page";
        case KernelResourceKind::GuiSurfaceRecord:
            return "gui-surface-record";
        case KernelResourceKind::GuiSurfaceBacking:
            return "gui-surface-backing";
        case KernelResourceKind::FileMappingBacking:
            return "file-mapping-backing";
        default:
            return "unknown";
        }
    }

    /*
     * Translate one resource kind into an array index.
     *
     * @param kind Resource kind to convert.
     * @return Valid array index, or `KernelResourceKind::Count` for invalid input.
     */
    Size resource_kind_index(KernelResourceKind kind) {
        const Size index = static_cast<Size>(kind);

        return (index < static_cast<Size>(KernelResourceKind::Count))
            ? index
            : static_cast<Size>(KernelResourceKind::Count);
    }

    /*
     * Link one record into the global resource registry.
     *
     * @param record Resource record to publish.
     * @return Nothing.
     */
    void resource_link_record(KernelResourceRecord* record) {
        const Size index = resource_kind_index(record->kind);
        KernelResourceStats* stats = &g_kernel_resource_stats[index];

        record->prev = g_kernel_resource_tail;
        record->next = NULL;
        if (g_kernel_resource_tail != NULL) {
            g_kernel_resource_tail->next = record;
        }
        else {
            g_kernel_resource_head = record;
        }

        g_kernel_resource_tail = record;
        ++stats->live_count;
        stats->live_bytes += record->size;
        if (stats->live_count > stats->peak_count) {
            stats->peak_count = stats->live_count;
        }
        if (stats->live_bytes > stats->peak_bytes) {
            stats->peak_bytes = stats->live_bytes;
        }
    }

    /*
     * Unlink one record from the global resource registry.
     *
     * @param record Resource record being removed.
     * @return Nothing.
     */
    void resource_unlink_record(KernelResourceRecord* record) {
        const Size index = resource_kind_index(record->kind);
        KernelResourceStats* stats = &g_kernel_resource_stats[index];

        if (record->prev != NULL) {
            record->prev->next = record->next;
        }
        else {
            g_kernel_resource_head = record->next;
        }

        if (record->next != NULL) {
            record->next->prev = record->prev;
        }
        else {
            g_kernel_resource_tail = record->prev;
        }

        record->prev = NULL;
        record->next = NULL;
        if (stats->live_count != 0U) {
            --stats->live_count;
        }
        if (record->size <= stats->live_bytes) {
            stats->live_bytes -= record->size;
        }
        else {
            stats->live_bytes = 0U;
        }
    }

    /*
     * Find one record by address.
     *
     * @param address Caller-visible resource address.
     * @return Matching record, or NULL when no record owns the address.
     */
    KernelResourceRecord* resource_find_record(const void* address) {
        for (KernelResourceRecord* record = g_kernel_resource_head; record != NULL; record = record->next) {
            if (record->address == address) {
                return record;
            }
        }

        return NULL;
    }

    /*
     * Create one registry record for an already-existing resource.
     *
     * @param kind Resource kind being tracked.
     * @param address Resource address.
     * @param size Live size in bytes.
     * @param owner_id Optional owner identifier for diagnostics.
     * @param label Optional short label for diagnostics.
     * @param owns_memory True when `release()` should free the payload.
     * @return StatusOK on success.
     */
    Status resource_register_record(
        KernelResourceKind kind,
        void* address,
        Size size,
        U64 owner_id,
        const char* label,
        bool owns_memory) {
        KernelResourceRecord* record;
        const Size index = resource_kind_index(kind);

        if ((index >= static_cast<Size>(KernelResourceKind::Count)) || (address == NULL) || (size == 0U)) {
            return StatusInvalidArgument;
        }
        if (resource_find_record(address) != NULL) {
            return StatusAlreadyExists;
        }

        record = static_cast<KernelResourceRecord*>(Heap::alloc(sizeof(KernelResourceRecord), alignof(KernelResourceRecord)));
        if (record == NULL) {
            return StatusNoMemory;
        }

        memzero(record, sizeof(*record));
        record->kind = kind;
        record->owns_memory = owns_memory;
        record->size = size;
        record->owner_id = owner_id;
        record->address = address;
        resource_copy_label(record->label, label);
        resource_link_record(record);
        KDEBUG(
            KZONE_LOADER,
            "[resource] alloc kind=%s addr=%p bytes=%llu owner=%llu label=%s live=%llu peak=%llu\n",
            resource_kind_name(kind),
            address,
            static_cast<unsigned long long>(size),
            static_cast<unsigned long long>(owner_id),
            record->label,
            static_cast<unsigned long long>(g_kernel_resource_stats[index].live_count),
            static_cast<unsigned long long>(g_kernel_resource_stats[index].peak_count));
        return StatusOK;
    }

} // namespace

Status KernelResourceManager::init(void) {
    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

    if (!g_kernel_resource_initialized) {
        g_kernel_resource_initialized = true;
        g_kernel_resource_head = NULL;
        g_kernel_resource_tail = NULL;
        memzero(g_kernel_resource_stats, sizeof(g_kernel_resource_stats));
    }

    arch::Arch::restore_interrupts(interrupts_enabled);
    return StatusOK;
}

void* KernelResourceManager::allocate(KernelResourceKind kind, Size size, Size alignment, U64 owner_id, const char* label) {
    void* address;
    Status status;
    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

    (void)init();
    arch::Arch::restore_interrupts(interrupts_enabled);

    address = Heap::alloc(size, alignment);
    if (address == NULL) {
        return NULL;
    }

    status = resource_register_record(kind, address, size, owner_id, label, true);
    if (status != StatusOK) {
        Heap::free(address);
        return NULL;
    }

    return address;
}

Status KernelResourceManager::track_external(KernelResourceKind kind, void* address, Size size, U64 owner_id, const char* label) {
    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();
    Status status;

    (void)init();
    status = resource_register_record(kind, address, size, owner_id, label, false);
    arch::Arch::restore_interrupts(interrupts_enabled);
    return status;
}

Status KernelResourceManager::untrack(KernelResourceKind kind, const void* address) {
    KernelResourceRecord* record;
    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

    if ((resource_kind_index(kind) >= static_cast<Size>(KernelResourceKind::Count)) || (address == NULL)) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusInvalidArgument;
    }

    record = resource_find_record(address);
    if ((record == NULL) || (record->kind != kind)) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusNotFound;
    }

    resource_unlink_record(record);
    Heap::free(record);
    arch::Arch::restore_interrupts(interrupts_enabled);
    return StatusOK;
}

void KernelResourceManager::release(void* address) {
    KernelResourceRecord* record;
    bool owns_memory;
    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

    if (address == NULL) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return;
    }

    record = resource_find_record(address);
    if (record == NULL) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return;
    }

    owns_memory = record->owns_memory;
    resource_unlink_record(record);
    Heap::free(record);
    arch::Arch::restore_interrupts(interrupts_enabled);

    if (owns_memory) {
        Heap::free(address);
    }
}

void KernelResourceManager::get_stats(KernelResourceKind kind, KernelResourceStats* stats_out) {
    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();
    const Size index = resource_kind_index(kind);

    if ((stats_out != NULL) && (index < static_cast<Size>(KernelResourceKind::Count))) {
        *stats_out = g_kernel_resource_stats[index];
    }
    else if (stats_out != NULL) {
        memzero(stats_out, sizeof(*stats_out));
    }

    arch::Arch::restore_interrupts(interrupts_enabled);
}