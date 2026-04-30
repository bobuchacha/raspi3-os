
#include "dll_loader.h"

#include "arch.h"
#include "debug-message.h"
#include "dll_image.h"
#include "heap.h"
#include "heap_list.h"
#include "kernel_time.h"
#include "mm.h"
#include "mm/physical.h"
#include "object.h"
#include "process.h"
#include "resource_manager.h"
#include "scheduler.h"
#include "serial.h"
#include "user_address_space_layout.h"
#include "vfs.h"

#undef KZONE_LOADER
#define KZONE_LOADER 0


struct SharedLoadedImage {
    U32 reference_count;
    U16 image_type;
    U16 reserved0;
    void* backing;
    Size image_bytes;
    VirtAddr image_base;
    char path[VFS_PATH_CAPACITY];
    char module_name[VFS_PATH_CAPACITY];
    SharedLoadedImage* next;
    SharedLoadedImage* prev;

};

/*
 * Track one process-private image page.
 *
 * Shared image backings stay canonical and immutable so multiple processes can
 * reuse the same physical pages. Any page that needs writable state, import
 * patching, or base-delta relocation is therefore cloned into one process-local
 * backing and described by this record.
 */
struct LoaderPrivatePage {
    void* backing;
    U64 virtual_address;
    Size mapped_bytes;
    U32 section_flags;
    U32 patch_flags;
    LoaderPrivatePage* next;
};

enum {
    LoaderPrivatePagePatchWritable = 1U << 0,
    LoaderPrivatePagePatchRelocation = 1U << 1,
    LoaderPrivatePagePatchImport = 1U << 2,
};

struct LoaderModuleSnapshot {
    unsigned long image_base;
    unsigned long image_bytes;
    unsigned long shared_backing_bytes;
    unsigned long private_backing_bytes;
    unsigned long shared_reference_count;
    unsigned long flags;
    char module_name[64];
    char path[260];
    unsigned long section_count;
    DllLoaderSectionSnapshot sections[DllLoaderSnapshotSectionCapacity];
};

struct LoadedModule {
    ObjectHeader header;
    Process* owner_process;
    SharedLoadedImage* shared_image;
    void* backing;
    Size image_bytes;
    VirtAddr image_base;
    char path[VFS_PATH_CAPACITY];
    char module_name[VFS_PATH_CAPACITY];
    LoaderModuleSnapshot snapshot;
    LoaderPrivatePage* private_pages;
    U32 private_page_count;
    bool pending_process_attach;
    bool pending_process_detach;
    LoadedModule* next;
    LoadedModule* prev;
};

#if 0

return basename;
    }

    inline constexpr unsigned long UserTaskModuleFlagSharedBacking = 1UL;
    inline constexpr unsigned long UserTaskModuleFlagPendingAttach = 2UL;
    inline constexpr unsigned long UserTaskModuleFlagPendingDetach = 4UL;
    inline constexpr unsigned long UserTaskModuleFlagPrivateWritable = 8UL;

    void copy_snapshot_text(char* destination, Size capacity, const char* source) {
        Size index = 0U;

        if ((destination == NULL) || (capacity == 0U)) {
            return;
        }

        if (source == NULL) {
            destination[0] = '\0';
            return;
        }

        while ((source[index] != '\0') && ((index + 1U) < capacity)) {
            destination[index] = source[index];
            ++index;
        }

        destination[index] = '\0';
    }

    unsigned long module_private_backing_bytes(const LoadedModule* module) {
        if ((module == NULL) || (module->private_page_count == 0U)) {
            return 0UL;
        }

        return static_cast<unsigned long>(module->private_page_count) * static_cast<unsigned long>(mm::PageSize);
    }

    /*
     * Return the union of section flags that touch one image page.
     *
     * Packed DLL sections are not guaranteed to start or end on page
     * boundaries, so the mapper must aggregate permissions page-by-page. This
     * keeps execute permission on mixed text/rodata pages and forces any page
     * with writable bytes to use a private per-process clone.
     *
     * @param backing Resident image backing.
     * @param page_rva Page-aligned image-relative page offset.
     * @return Union of `DLL_SEC_*` flags for overlapping sections.
     */
    U32 page_flags_in_backing(const void* backing, U64 page_rva) {
        const dll_header* header = header_of_backing(backing);
        const dll_section* sections = sections_of_backing(backing);
        U32 page_flags = 0U;

        if ((header == NULL) || (sections == NULL)) {
            return 0U;
        }

        for (U32 section_index = 0U; section_index < header->section_count; ++section_index) {
            const dll_section* section = &sections[section_index];

            if ((section->virtual_size == 0U)

#endif

                namespace {

                /*
                 * Round one fixed-record size up to the next aligned slot boundary.
                 *
                 * The page-backed loader metadata pools carve one physical page into equal
                 * slots, so every slot stride must preserve the natural alignment of the
                 * stored record type.
                 *
                 * @param value Raw record size in bytes.
                 * @param alignment Required slot alignment.
                 * @return Aligned slot size.
                 */
                constexpr Size loader_align_up(Size value, Size alignment) {
                    return (value + (alignment - 1U)) & ~(alignment - 1U);
                }

                inline constexpr Size VfsReadProgressMinimumBytes = 4UL * 1024UL;
                inline constexpr U32 LoaderRelocationPollStride = 128U;
                inline constexpr U32 LoaderImportPollStride = 64U;
                inline constexpr U64 ImageCacheTtlMsec = 5ULL * 60ULL * 1000ULL;
                inline constexpr U64 DefaultDllPreferredBase = user_address_space::ModuleRegionBase;
                inline constexpr U64 DefaultDriverPreferredBase = user_address_space::ModuleRegionBase + (8ULL * mm::backend::L2BlockSize);

                typedef struct CachedImageObject CachedImageObject;

                typedef struct ParsedImageView {
                    const U8* file_bytes;
                    Size file_size;
                    CachedImageObject* cache_object;
                    bool direct_file_population;
                    const dll_header* header;
                    const dll_section* sections;
                    const dll_import_module* import_modules;
                    const dll_import_symbol* import_symbols;
                    const dll_export_symbol* exports;
                    const dll_relocation* relocations;
                    const char* string_table;
                } ParsedImageView;

                typedef struct CachedImageObject {
                    U64 object_id;
                    U32 reference_count;
                    U32 reserved1;
                    U64 last_used_msec;
                    U64 expires_at_msec;
                    Size file_size;
                    U8* file_bytes;
                    char path[VFS_PATH_CAPACITY];
                    struct CachedImageObject* next;
                    struct CachedImageObject* prev;
                } CachedImageObject;

                typedef struct LoaderRecordSlot {
                    struct LoaderRecordSlot* next_free;
                } LoaderRecordSlot;

                typedef struct LoaderRecordPageHeader {
                    struct LoaderRecordPageHeader* next_page;
                    PhysAddr page_phys;
                    U32 live_slots;
                    U32 slot_capacity;
                } LoaderRecordPageHeader;

                typedef struct LoaderRecordPool {
                    Size slot_bytes;
                    Size slot_alignment;
                    LoaderRecordSlot* free_list;
                    LoaderRecordPageHeader* pages;
                } LoaderRecordPool;

                bool g_dll_loader_initialized;
                U64 g_next_module_id = 1ULL;
                LoadedModule* g_module_head;
                LoadedModule* g_module_tail;
                SharedLoadedImage* g_shared_image_head;
                SharedLoadedImage* g_shared_image_tail;
                CachedImageObject* g_cached_image_head;
                CachedImageObject* g_cached_image_tail;
                Size g_cached_image_count;
                Size g_cached_image_peak_count;
                Size g_cached_image_bytes;
                Size g_cached_image_peak_bytes;
                U64 g_next_cached_image_object_id = 1ULL;
                LoaderRecordPool g_loaded_module_record_pool = {};
                LoaderRecordPool g_shared_image_record_pool = {};
                LoaderRecordPool g_cached_image_record_pool = {};
                LoaderRecordPool g_private_image_page_record_pool = {};

                /*
                 * Initialize one loader-local fixed-record pool lazily.
                 *
                 * The loader only needs the page-backed metadata pools once it starts
                 * publishing shared-image or cached-image records, so the description is
                 * populated on first use instead of at boot.
                 *
                 * @param pool Pool state to initialize.
                 * @param record_bytes Raw record size.
                 * @param record_alignment Natural record alignment.
                 * @return Nothing.
                 */
                void loader_record_pool_init(LoaderRecordPool* pool, Size record_bytes, Size record_alignment) {
                    if ((pool == NULL) || (record_bytes == 0U) || (record_alignment == 0U)) {
                        return;
                    }
                    if (pool->slot_bytes != 0U) {
                        return;
                    }

                    pool->slot_alignment = record_alignment;
                    pool->slot_bytes = loader_align_up(record_bytes, record_alignment);
                    pool->free_list = NULL;
                    pool->pages = NULL;
                }

                /*
                 * Return the page header that owns one loader-record slot.
                 *
                 * Every pool page stores its header at the start of the page and carves
                 * record slots out of the remaining bytes.
                 *
                 * @param slot Slot pointer returned by one loader record pool.
                 * @return Owning page header, or NULL for invalid input.
                 */
                LoaderRecordPageHeader* loader_record_page_from_slot(const void* slot) {
                    if (slot == NULL) {
                        return NULL;
                    }

                    return reinterpret_cast<LoaderRecordPageHeader*>(
                        reinterpret_cast<Uptr>(slot) & ~(static_cast<Uptr>(mm::PageSize) - 1U));
                }

                /*
                 * Grow one loader metadata pool by carving one physical page into slots.
                 *
                 * Shared-image and cached-image records are long-lived fixed-size objects,
                 * so a page-backed slot allocator keeps them off the general heap while
                 * still allowing pages to be reclaimed when a pool page becomes empty.
                 *
                 * @param pool Pool to extend.
                 * @return True when at least one new slot was added.
                 */
                bool loader_record_pool_grow(LoaderRecordPool* pool) {
                    const PhysAddr page_phys = mm::PhysicalMemory::alloc_page();
                    U8* page_base;
                    LoaderRecordPageHeader* page_header;
                    Uptr slot_start;
                    Size available_bytes;
                    Size slot_capacity;

                    if ((pool == NULL) || (pool->slot_bytes == 0U) || (pool->slot_alignment == 0U) || (page_phys == 0U)) {
                        return false;
                    }

                    page_base = reinterpret_cast<U8*>(mm::MemoryManager::physical_to_kernel(page_phys));
                    page_header = reinterpret_cast<LoaderRecordPageHeader*>(page_base);
                    memzero(page_header, sizeof(*page_header));
                    page_header->next_page = pool->pages;
                    page_header->page_phys = page_phys;

                    slot_start = loader_align_up(
                        reinterpret_cast<Uptr>(page_base + sizeof(LoaderRecordPageHeader)),
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
                        LoaderRecordSlot* slot = reinterpret_cast<LoaderRecordSlot*>(slot_start + (slot_index * pool->slot_bytes));

                        slot->next_free = pool->free_list;
                        pool->free_list = slot;
                    }

                    return true;
                }

                /*
                 * Allocate one fixed-size metadata record from a loader-local pool.
                 *
                 * @param pool Pool supplying the record.
                 * @return Zeroed record slot, or NULL when the pool cannot grow.
                 */
                void* loader_record_pool_allocate(LoaderRecordPool* pool) {
                    LoaderRecordSlot* slot;
                    LoaderRecordPageHeader* page_header;

                    if (pool == NULL) {
                        return NULL;
                    }
                    if ((pool->free_list == NULL) && !loader_record_pool_grow(pool)) {
                        return NULL;
                    }

                    slot = pool->free_list;
                    pool->free_list = slot->next_free;
                    page_header = loader_record_page_from_slot(slot);
                    if (page_header != NULL) {
                        page_header->live_slots += 1U;
                    }
                    memzero(slot, pool->slot_bytes);
                    return slot;
                }

                /*
                 * Remove every free-list node that belongs to one retiring page.
                 *
                 * Before a page can be returned to the physical allocator, the pool must
                 * strip any free slots from that page out of the global free list.
                 *
                 * @param pool Pool whose free list is being filtered.
                 * @param retired_page Page that is leaving the pool.
                 * @return Nothing.
                 */
                void loader_record_pool_remove_page_slots(LoaderRecordPool* pool, const LoaderRecordPageHeader* retired_page) {
                    LoaderRecordSlot* retained_head = NULL;
                    LoaderRecordSlot* node;

                    if ((pool == NULL) || (retired_page == NULL)) {
                        return;
                    }

                    node = pool->free_list;
                    while (node != NULL) {
                        LoaderRecordSlot* next = node->next_free;

                        if (loader_record_page_from_slot(node) != retired_page) {
                            node->next_free = retained_head;
                            retained_head = node;
                        }
                        node = next;
                    }

                    pool->free_list = retained_head;
                }

                /*
                 * Unlink one page header from a loader-local pool page chain.
                 *
                 * @param pool Pool that owns the page.
                 * @param page_header Page being unlinked.
                 * @return Nothing.
                 */
                void loader_record_pool_unlink_page(LoaderRecordPool* pool, LoaderRecordPageHeader* page_header) {
                    LoaderRecordPageHeader* current;
                    LoaderRecordPageHeader* previous = NULL;

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
                 * Return one metadata record slot to a loader-local pool.
                 *
                 * Empty pages are reclaimed immediately so record churn does not leak page
                 * pressure after the last live record on a page is released.
                 *
                 * @param pool Pool receiving the record.
                 * @param record Record pointer previously returned by the pool.
                 * @return Nothing.
                 */
                void loader_record_pool_free(LoaderRecordPool* pool, void* record) {
                    LoaderRecordPageHeader* page_header;
                    LoaderRecordSlot* slot;

                    if ((pool == NULL) || (record == NULL)) {
                        return;
                    }

                    page_header = loader_record_page_from_slot(record);
                    if (page_header == NULL) {
                        return;
                    }

                    slot = reinterpret_cast<LoaderRecordSlot*>(record);
                    slot->next_free = pool->free_list;
                    pool->free_list = slot;

                    if (page_header->live_slots != 0U) {
                        page_header->live_slots -= 1U;
                    }
                    if (page_header->live_slots != 0U) {
                        return;
                    }

                    loader_record_pool_remove_page_slots(pool, page_header);
                    loader_record_pool_unlink_page(pool, page_header);
                    mm::PhysicalMemory::free_page(page_header->page_phys);
                }

                void loader_track_external_best_effort(
                    KernelResourceKind kind,
                    void* address,
                    Size size,
                    U64 owner_id,
                    const char* label);

                /*
                 * Allocate one page-backed shared-image record and publish it to resource accounting.
                 *
                 * @param owner_id Diagnostic owner identifier.
                 * @param label Short resource label.
                 * @return Shared-image record, or NULL on failure.
                 */
                SharedLoadedImage* allocate_shared_image_record(U64 owner_id, const char* label) {
                    SharedLoadedImage* image;
                    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

                    loader_record_pool_init(&g_shared_image_record_pool, sizeof(SharedLoadedImage), alignof(SharedLoadedImage));
                    image = static_cast<SharedLoadedImage*>(loader_record_pool_allocate(&g_shared_image_record_pool));
                    arch::Arch::restore_interrupts(interrupts_enabled);
                    if (image == NULL) {
                        return NULL;
                    }

                    loader_track_external_best_effort(
                        KernelResourceKind::LoaderSharedImageRecord,
                        image,
                        g_shared_image_record_pool.slot_bytes,
                        owner_id,
                        label);

                    return image;
                }

                /*
                 * Return one shared-image record to its page-backed metadata pool.
                 *
                 * @param image Shared-image record being destroyed.
                 * @return Nothing.
                 */
                void free_shared_image_record(SharedLoadedImage* image) {
                    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

                    (void)KernelResourceManager::untrack(KernelResourceKind::LoaderSharedImageRecord, image);
                    loader_record_pool_free(&g_shared_image_record_pool, image);
                    arch::Arch::restore_interrupts(interrupts_enabled);
                }

                /*
                 * Allocate one page-backed module record and publish it to resource accounting.
                 *
                 * Loaded modules can stay resident for the lifetime of a process tree, so the
                 * redesign keeps those fixed-size bookkeeping records off the general heap and
                 * in the same reclaimable page-backed pool model used for the other persistent
                 * loader metadata records.
                 *
                 * @param owner_id Diagnostic owner identifier.
                 * @param label Short resource label.
                 * @return Module record, or NULL on failure.
                 */
                LoadedModule* allocate_loaded_module_record(U64 owner_id, const char* label) {
                    LoadedModule* module;
                    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

                    loader_record_pool_init(&g_loaded_module_record_pool, sizeof(LoadedModule), alignof(LoadedModule));
                    module = static_cast<LoadedModule*>(loader_record_pool_allocate(&g_loaded_module_record_pool));
                    arch::Arch::restore_interrupts(interrupts_enabled);
                    if (module == NULL) {
                        return NULL;
                    }

                    loader_track_external_best_effort(
                        KernelResourceKind::LoaderModuleRecord,
                        module,
                        g_loaded_module_record_pool.slot_bytes,
                        owner_id,
                        label);

                    return module;
                }

                /*
                 * Return one module record to its page-backed metadata pool.
                 *
                 * @param module Loaded-module record being destroyed.
                 * @return Nothing.
                 */
                void free_loaded_module_record(LoadedModule* module) {
                    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

                    (void)KernelResourceManager::untrack(KernelResourceKind::LoaderModuleRecord, module);
                    loader_record_pool_free(&g_loaded_module_record_pool, module);
                    arch::Arch::restore_interrupts(interrupts_enabled);
                }

                /*
                 * Allocate one page-backed cached-image record and publish it to resource accounting.
                 *
                 * @param label Short resource label.
                 * @return Cached-image record, or NULL on failure.
                 */
                CachedImageObject* allocate_cached_image_record(const char* label) {
                    CachedImageObject* object;
                    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

                    loader_record_pool_init(&g_cached_image_record_pool, sizeof(CachedImageObject), alignof(CachedImageObject));
                    object = static_cast<CachedImageObject*>(loader_record_pool_allocate(&g_cached_image_record_pool));
                    arch::Arch::restore_interrupts(interrupts_enabled);
                    if (object == NULL) {
                        return NULL;
                    }

                    loader_track_external_best_effort(
                        KernelResourceKind::LoaderImageCacheRecord,
                        object,
                        g_cached_image_record_pool.slot_bytes,
                        0ULL,
                        label);

                    return object;
                }

                /*
                 * Return one cached-image record to its page-backed metadata pool.
                 *
                 * @param object Cached-image record being destroyed.
                 * @return Nothing.
                 */
                void free_cached_image_record(CachedImageObject* object) {
                    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

                    (void)KernelResourceManager::untrack(KernelResourceKind::LoaderImageCacheRecord, object);
                    loader_record_pool_free(&g_cached_image_record_pool, object);
                    arch::Arch::restore_interrupts(interrupts_enabled);
                }

                /*
                 * Allocate one page-backed private-page record.
                 *
                 * Rebased images may need one record per patched page, so this metadata
                 * must scale without pinning long-lived arrays in the general heap.
                 * The loader therefore reuses the same fixed-record pool pattern as its
                 * other permanent registries.
                 *
                 * @return Private-page record, or NULL on pool exhaustion.
                 */
                LoaderPrivatePage* allocate_private_page_record(void) {
                    LoaderPrivatePage* page;
                    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

                    loader_record_pool_init(&g_private_image_page_record_pool, sizeof(LoaderPrivatePage), alignof(LoaderPrivatePage));
                    page = static_cast<LoaderPrivatePage*>(loader_record_pool_allocate(&g_private_image_page_record_pool));
                    arch::Arch::restore_interrupts(interrupts_enabled);
                    return page;
                }

                /*
                 * Return one private-page record to the loader-local page pool.
                 *
                 * The page backing itself is released separately so this helper only
                 * returns the bookkeeping slot.
                 *
                 * @param page Record to release.
                 * @return Nothing.
                 */
                void free_private_page_record(LoaderPrivatePage* page) {
                    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

                    loader_record_pool_free(&g_private_image_page_record_pool, page);
                    arch::Arch::restore_interrupts(interrupts_enabled);
                }

                /**
                 * Round one image backing size up to page granularity.
                 *
                 * EXE and DLL images no longer need to reserve a full 2 MiB physical block
                 * when the packed image is only tens of kilobytes. The current user VA
                 * layout still places images on stable 2 MiB boundaries, but the backing
                 * allocation and mapping length now follow the actual image size.
                 *
                 * @param image_bytes Exact image size declared in the header.
                 * @return Page-rounded backing size.
                 */
                Size align_up_image_bytes(Size image_bytes) {
                    const Size mask = mm::PageSize - 1U;

                    if (image_bytes == 0U) {
                        return 0U;
                    }

                    return (image_bytes + mask) & ~mask;
                }

                /**
                 * Log one compact loader-backing occupancy snapshot.
                 *
                 * These counters make the next scaling ceiling obvious in logs by showing
                 * how much pressure is coming from the reserved 2 MiB pool versus exact
                 * page-backed fallback allocations.
                 *
                 * @param reason Short phase label for the snapshot.
                 * @return Nothing.
                 */
                void loader_log_backing_snapshot(const char* reason) {
                    KernelResourceStats stats = {};

                    KernelResourceManager::get_stats(KernelResourceKind::LoaderImageBacking, &stats);
                    KDEBUG(
                        KZONE_LOADER,
                        "[loader] backing usage reason=%s live=%lu peak=%lu live_bytes=%lu peak_bytes=%lu\n",
                        (reason != NULL) ? reason : "<none>",
                        static_cast<unsigned long>(stats.live_count),
                        static_cast<unsigned long>(stats.peak_count),
                        static_cast<unsigned long>(stats.live_bytes),
                        static_cast<unsigned long>(stats.peak_bytes));
                }

                /**
                 * Emit one compact cached-image occupancy snapshot.
                 *
                 * Repeated EXE and DLL launches depend heavily on the loader image cache, so
                 * count and byte visibility helps distinguish cache churn from true file-I/O
                 * pressure.
                 *
                 * @param reason Short phase label for the snapshot.
                 * @return Nothing.
                 */
                void cached_image_log_snapshot(const char* reason) {
                    KDEBUG(
                        KZONE_LOADER,
                        "[loader] image-cache reason=%s entries=%lu peak=%lu bytes=%lu peak_bytes=%lu\n",
                        (reason != NULL) ? reason : "<none>",
                        static_cast<unsigned long>(g_cached_image_count),
                        static_cast<unsigned long>(g_cached_image_peak_count),
                        static_cast<unsigned long>(g_cached_image_bytes),
                        static_cast<unsigned long>(g_cached_image_peak_bytes));
                }

                /**
                 * Link one cached image object into the global registry.
                 *
                 * @param object Freshly created cache object.
                 * @return Nothing.
                 */
                void cached_image_link(CachedImageObject* object) {
                    if (object == NULL) {
                        return;
                    }

                    object->prev = g_cached_image_tail;
                    object->next = NULL;
                    if (g_cached_image_tail != NULL) {
                        g_cached_image_tail->next = object;
                    }
                    else {
                        g_cached_image_head = object;
                    }

                    g_cached_image_tail = object;
                    ++g_cached_image_count;
                    if (g_cached_image_count > g_cached_image_peak_count) {
                        g_cached_image_peak_count = g_cached_image_count;
                    }
                    if (g_cached_image_bytes > g_cached_image_peak_bytes) {
                        g_cached_image_peak_bytes = g_cached_image_bytes;
                    }
                    cached_image_log_snapshot("alloc");
                }

                /**
                 * Unlink one cached image object from the global registry.
                 *
                 * @param object Cached object being removed.
                 * @return Nothing.
                 */
                void cached_image_unlink(CachedImageObject* object) {
                    if (object == NULL) {
                        return;
                    }

                    if (object->prev != NULL) {
                        object->prev->next = object->next;
                    }
                    else {
                        g_cached_image_head = object->next;
                    }

                    if (object->next != NULL) {
                        object->next->prev = object->prev;
                    }
                    else {
                        g_cached_image_tail = object->prev;
                    }

                    object->next = NULL;
                    object->prev = NULL;
                    if (g_cached_image_count != 0U) {
                        --g_cached_image_count;
                    }
                }

                /**
                 * Let the cooperative scheduler and polled device path make progress
                 * during long image loads.
                 *
                 * Polling alone advances timer/device state, but it still leaves the
                 * current worker thread running until a later forced reschedule point.
                 * When async launches moved onto a normal-priority worker to avoid
                 * starvation, that worker could then monopolize the cooperative
                 * scheduler long enough for Explorer input and repaint work to appear
                 * blocked during the load. Yield explicitly after the existing batched
                 * poll checkpoints so foreground UI threads can run between loader work
                 * chunks without regressing back to a permanently starved worker.
                 *
                 * @return Nothing.
                 */
                void loader_cooperative_poll(void) {
                    Scheduler::poll();
                    Scheduler::yield();
                }

                /**
                 * Accumulate one unit of loader work and yield cooperatively once the
                 * configured threshold is crossed.
                 *
                 * The loader's heavy paths are mostly memcpy-style loops. Polling on every
                 * iteration would be noisy, so this helper batches progress and only pumps
                 * the scheduler once enough work has been completed.
                 *
                 * @param accumulated_work Running work counter updated by the caller.
                 * @param completed_work Newly completed work units.
                 * @param threshold Minimum accumulated work before polling.
                 * @return Nothing.
                 */
                void loader_poll_after_progress(U64* accumulated_work, U64 completed_work, U64 threshold) {
                    if ((accumulated_work == NULL) || (threshold == 0U)) {
                        return;
                    }

                    *accumulated_work += completed_work;
                    if (*accumulated_work >= threshold) {
                        loader_cooperative_poll();
                        *accumulated_work = 0U;
                    }
                }

                /*
                 * Compare two null-terminated strings without ASCII case sensitivity.
                 *
                 * @param lhs Left string.
                 * @param rhs Right string.
                 * @return True when both strings match ignoring ASCII case.
                 */
                bool same_text_case_insensitive(const char* lhs, const char* rhs);
                bool ranges_overlap(VirtAddr first_base, Size first_bytes, VirtAddr second_base, Size second_bytes);
                Status store_loader_text(char* destination, Size capacity, const char* source, const char* fallback);
                const char* path_basename(const char* path);
                const dll_header* header_of_backing(const void* backing);
                const dll_section* sections_of_backing(const void* backing);
                const dll_import_module* imports_of_backing(const void* backing);
                const dll_import_symbol* import_symbols_of_backing(const void* backing);
                const dll_relocation* relocations_of_backing(const void* backing);
                const char* string_table_of_backing(const void* backing);
                const char* bounded_string_at(const char* string_table, U32 string_table_size, U32 string_offset);
                LoadedModule* find_module_by_path(Process* process, const char* module_path);
                Status lookup_export_address(const LoadedModule* module, const char* export_name, VirtAddr* export_address_out);
                void free_private_image_pages(LoaderPrivatePage* pages);
                Status build_process_private_image_pages(
                    Process* process,
                    const void* backing,
                    VirtAddr canonical_base,
                    VirtAddr target_base,
                    bool clone_writable_pages,
                    U64 owner_id,
                    const char* label,
                    LoaderPrivatePage** pages_out,
                    U32* page_count_out);
                U64 section_mapping_flags(U32 section_flags);
                Status map_image_segment(Process* process, VirtAddr image_base, U64 segment_rva, const void* segment_backing, Size segment_bytes, U64 mapping_flags);

                /**
                 * Emit one loader diagnostic directly to the serial console.
                 *
                 * The boot path needs a low-friction trace point here because userspace image
                 * launch currently fails before any richer logging surface exists.
                 *
                 * @param text Null-terminated diagnostic line.
                 */
                void loader_trace(const char* text) {
                    KDEBUG(KZONE_LOADER, "%s", (text != NULL) ? text : "");
                }

                /**
                 * Emit one loader diagnostic that includes the failing image path.
                 *
                 * Path-aware traces are needed here because executable loads and dependent
                 * DLL loads currently share the same staging helper.
                 *
                 * @param prefix Null-terminated prefix written before the path.
                 * @param path Null-terminated path associated with the diagnostic.
                 */
                void loader_trace_path(const char* prefix, const char* path) {
                    KDEBUG(KZONE_LOADER, "%s%s", (prefix != NULL) ? prefix : "", (path != NULL) ? path : "");
                }

                /**
                 * Publish one loader resource to the debug accounting registry on a
                 * best-effort basis.
                 *
                 * Loader backings and metadata are already page-backed at this point.
                 * Failing a process launch only because the diagnostic registry could
                 * not allocate one heap-backed tracking node would reintroduce the same
                 * heap dependency that the page-backed rewrite was meant to remove.
                 *
                 * @param kind Resource kind for the debug registry.
                 * @param address Resource base address.
                 * @param size Resource size in bytes.
                 * @param owner_id Optional owner identifier for diagnostics.
                 * @param label Optional diagnostic label.
                 * @return Nothing.
                 */
                void loader_track_external_best_effort(
                    KernelResourceKind kind,
                    void* address,
                    Size size,
                    U64 owner_id,
                    const char* label) {
                    const Status status = KernelResourceManager::track_external(kind, address, size, owner_id, label);

                    if (status != StatusOK) {
                        KDEBUG(
                            KZONE_LOADER,
                            "[loader] resource tracking skipped kind=%u addr=%p bytes=%llu owner=%llu label=%s status=%d\n",
                            static_cast<unsigned int>(kind),
                            address,
                            static_cast<unsigned long long>(size),
                            static_cast<unsigned long long>(owner_id),
                            (label != NULL) ? label : "",
                            static_cast<int>(status));
                    }
                }

                /*
                 * Allocate one fixed-size executable or DLL backing slot.
                 *
                 * Async spawn failures showed that requiring one contiguous physical run
                 * here is too strict once the system has churned through several GUI
                 * launches. The loader only needs one stable virtual backing buffer; the
                 * later user mapping path can translate that buffer page-by-page.
                 *
                 * @return Backing pointer on success, or NULL when no slot is available.
                 */
                U8* allocate_loader_backing(Size backing_bytes) {
                    const Size rounded_bytes = (backing_bytes + (mm::PageSize - 1U)) & ~(mm::PageSize - 1U);
                    U8* backing;

                    if ((backing_bytes == 0U) || (rounded_bytes == 0U)) {
                        return NULL;
                    }

                    backing = static_cast<U8*>(KernelResourceManager::allocate(
                        KernelResourceKind::LoaderImageBacking,
                        rounded_bytes,
                        mm::PageSize,
                        0ULL,
                        "image-backing"));
                    if (backing == NULL) {
                        loader_log_backing_snapshot("alloc-failed");
                        return NULL;
                    }

                    loader_log_backing_snapshot("alloc");
                    return backing;
                }

                /*
                 * Return one image backing allocation to the global resource manager.
                 *
                 * @param backing Backing pointer being released.
                 * @return Nothing.
                 */
                void free_loader_backing(void* backing, Size backing_bytes) {
                    (void)backing_bytes;

                    if (backing == NULL) {
                        return;
                    }

                    KernelResourceManager::release(backing);
                    loader_log_backing_snapshot("free");
                }

                /*
                 * Return the current uptime used by the image-cache TTL bookkeeping.
                 *
                 * @return Scheduler-backed uptime in milliseconds.
                 */
                U64 image_cache_now_msec(void) {
                    return KernelTime::ticks_to_milliseconds(Scheduler::tick_count());
                }

                /*
                 * Measure elapsed wall-clock time from one earlier loader timestamp.
                 *
                 * The loader bottleneck investigation needs one stable, scheduler-backed
                 * duration source so EXE, DLL, and dependency phases can be compared in
                 * one log line without introducing another timer dependency.
                 *
                 * @param start_msec Earlier timestamp captured from `image_cache_now_msec`.
                 * @return Elapsed milliseconds, clamped at zero if the clock moved backwards.
                 */
                U64 loader_elapsed_msec(U64 start_msec) {
                    const U64 now_msec = image_cache_now_msec();

                    return (now_msec >= start_msec) ? (now_msec - start_msec) : 0ULL;
                }

                /*
                 * Release one cached image object and its file bytes.
                 *
                 * @param object Cached image object to clear.
                 * @return Nothing.
                 */
                void cached_image_object_destroy(CachedImageObject* object) {
                    if (object == NULL) {
                        return;
                    }

                    if (object->file_bytes != NULL) {
                        (void)KernelResourceManager::untrack(KernelResourceKind::LoaderImageCachePayload, object->file_bytes);
                        Heap::free(object->file_bytes);
                        object->file_bytes = NULL;
                    }
                    if (g_cached_image_bytes >= object->file_size) {
                        g_cached_image_bytes -= object->file_size;
                    }
                    else {
                        g_cached_image_bytes = 0U;
                    }

                    cached_image_unlink(object);
                    free_cached_image_record(object);
                    cached_image_log_snapshot("free");
                }

                /*
                 * Drop cached image objects whose TTL already elapsed.
                 *
                 * Only zero-reference objects may be evicted because active parsed views
                 * still point directly into the cached file buffer.
                 *
                 * @param now_msec Current uptime snapshot.
                 * @return Nothing.
                 */
                void cached_image_prune_expired(U64 now_msec) {
                    CachedImageObject* object = g_cached_image_head;

                    while (object != NULL) {
                        CachedImageObject* next = object->next;

                        if ((object->reference_count == 0U) && (object->expires_at_msec <= now_msec)) {
                            cached_image_object_destroy(object);
                        }

                        object = next;
                    }
                }

                /*
                 * Return the least-recently-used zero-reference cached image object.
                 *
                 * @return Evictable cached object, or NULL when none are available.
                 */
                CachedImageObject* cached_image_find_lru_evictable(void) {
                    CachedImageObject* best = NULL;

                    for (CachedImageObject* object = g_cached_image_head; object != NULL; object = object->next) {

                        if (object->reference_count != 0U) {
                            continue;
                        }
                        if ((best == NULL) || (object->last_used_msec < best->last_used_msec)) {
                            best = object;
                        }
                    }

                    return best;
                }

                /*
                 * Look up one cached image object by normalized path and take a temporary
                 * parsed-view reference on success.
                 *
                 * @param path Normalized DOS-style image path.
                 * @param now_msec Current uptime snapshot.
                 * @return Matching cached object, or NULL when the cache missed.
                 */
                CachedImageObject* cached_image_find(const char* path, U64 now_msec) {
                    cached_image_prune_expired(now_msec);

                    for (CachedImageObject* object = g_cached_image_head; object != NULL; object = object->next) {
                        if (!same_text_case_insensitive(object->path, path)) {
                            continue;
                        }

                        object->reference_count += 1U;
                        object->last_used_msec = now_msec;
                        object->expires_at_msec = now_msec + ImageCacheTtlMsec;
                        return object;
                    }

                    return NULL;
                }

                /*
                 * Drop one temporary parsed-view reference on a cached image object.
                 *
                 * Parsed views point directly into cached file bytes, so the loader keeps
                 * an explicit reference count while a caller is still walking headers or
                 * import tables. Releasing the view should refresh the TTL rather than
                 * immediately destroy the cache entry.
                 *
                 * @param object Cached image object whose view reference is ending.
                 * @return Nothing.
                 */
                void cached_image_release(CachedImageObject* object) {
                    const U64 now_msec = image_cache_now_msec();

                    if (object == NULL) {
                        return;
                    }

                    if (object->reference_count != 0U) {
                        --object->reference_count;
                    }
                    object->last_used_msec = now_msec;
                    object->expires_at_msec = now_msec + ImageCacheTtlMsec;
                }

                /*
                 * Store one freshly-read image buffer inside the global image cache.
                 *
                 * The cache takes ownership of `file_bytes` on success and publishes the
                 * new object with one active parsed-view reference.
                 *
                 * @param path Normalized DOS-style image path.
                 * @param file_bytes Heap-owned file bytes to cache.
                 * @param file_size File size in bytes.
                 * @param now_msec Current uptime snapshot.
                 * @return Cached image object, or NULL when the image was left uncached.
                 */
                CachedImageObject* cached_image_insert(const char* path, U8* file_bytes, Size file_size, U64 now_msec) {
                    CachedImageObject* object;

                    if ((path == NULL) || (file_bytes == NULL) || (file_size == 0U)) {
                        return NULL;
                    }

                    cached_image_prune_expired(now_msec);

                    object = allocate_cached_image_record("cache-record");
                    if (object == NULL) {
                        cached_image_log_snapshot("alloc-failed");
                        return NULL;
                    }

                    memzero(object, sizeof(*object));
                    object->object_id = g_next_cached_image_object_id++;
                    object->reference_count = 1U;
                    object->last_used_msec = now_msec;
                    object->expires_at_msec = now_msec + ImageCacheTtlMsec;
                    object->file_size = file_size;
                    object->file_bytes = file_bytes;
                    loader_track_external_best_effort(
                        KernelResourceKind::LoaderImageCachePayload,
                        file_bytes,
                        file_size,
                        0ULL,
                        "file-cache");
                    (void)store_loader_text(object->path, sizeof(object->path), path, path);
                    g_cached_image_bytes += file_size;
                    cached_image_link(object);
                    return object;
                }

                /*
                 * Validate heap integrity at one public DLL loader boundary.
                 *
                 * Temporary loader instrumentation should identify whether corruption
                 * first appears during image staging, dependency resolution, or module
                 * teardown, so every public DLL loader boundary reuses this helper.
                 *
                 * @param reason Phase label for the validation log.
                 * @return StatusOK when the heap remains internally consistent.
                 */
                Status dll_loader_heap_checkpoint_status(const char* reason) {
                    if (!Heap::debug_validate(reason)) {
                        KERROR("[dll-loader] heap checkpoint failed reason=%s\n", reason != NULL ? reason : "<none>");
                        return StatusFault;
                    }

                    return StatusOK;
                }

                bool add_range_overflows(U64 base, U64 length, U64 limit) {
                    return (length > limit) || (base > (limit - length));
                }

                char ascii_lower(char ch) {
                    if ((ch >= 'A') && (ch <= 'Z')) {
                        return static_cast<char>(ch - 'A' + 'a');
                    }

                    return ch;
                }

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
                 * Return whether one parsed image contains writable sections.
                 *
                 * Sharing one fully relocated backing across processes is only safe when
                 * no section remains writable after load. Writable sections would otherwise
                 * alias mutable process state across every process mapping that image.
                 *
                 * @param view Parsed image view under consideration.
                 * @return True when at least one section carries the writable flag.
                 */
                bool parsed_image_has_writable_sections(const ParsedImageView* view) {
                    if ((view == NULL) || (view->header == NULL) || (view->sections == NULL)) {
                        return true;
                    }

                    for (U32 section_index = 0U; section_index < view->header->section_count; ++section_index) {
                        if ((view->sections[section_index].flags & DLL_SEC_WRITE) != 0U) {
                            return true;
                        }
                    }

                    return false;
                }

                /*
                 * Link one shared loaded image into the global residency registry.
                 *
                 * The registry owns one canonical backing per normalized image path so new
                 * processes can reuse the same relocated bytes instead of allocating and
                 * patching another private copy.
                 *
                 * @param image Shared image record to publish.
                 * @return Nothing.
                 */
                void link_shared_image(SharedLoadedImage* image) {
                    if (image == NULL) {
                        return;
                    }

                    image->prev = g_shared_image_tail;
                    image->next = NULL;
                    if (g_shared_image_tail != NULL) {
                        g_shared_image_tail->next = image;
                    }
                    else {
                        g_shared_image_head = image;
                    }

                    g_shared_image_tail = image;
                }

                /*
                 * Unlink one shared loaded image from the global residency registry.
                 *
                 * @param image Shared image record being removed.
                 * @return Nothing.
                 */
                void unlink_shared_image(SharedLoadedImage* image) {
                    if (image == NULL) {
                        return;
                    }

                    if (image->prev != NULL) {
                        image->prev->next = image->next;
                    }
                    else {
                        g_shared_image_head = image->next;
                    }

                    if (image->next != NULL) {
                        image->next->prev = image->prev;
                    }
                    else {
                        g_shared_image_tail = image->prev;
                    }

                    image->next = NULL;
                    image->prev = NULL;
                }

                /*
                 * Find one shared loaded image by normalized path.
                 *
                 * @param normalized_path Canonical DOS-style image path.
                 * @return Matching shared image, or NULL when the image is not resident.
                 */
                SharedLoadedImage* find_shared_image_by_path(const char* normalized_path) {
                    for (SharedLoadedImage* image = g_shared_image_head; image != NULL; image = image->next) {
                        if (same_text_case_insensitive(image->path, normalized_path)) {
                            return image;
                        }
                    }

                    return NULL;
                }

                /*
                 * Find one shared loaded image by its backing allocation.
                 *
                 * Executable teardown currently stores only the backing pointer in the
                 * process object, so backing-based lookup lets process exit drop the shared
                 * image reference without teaching the process object about loader internals.
                 *
                 * @param backing Canonical loaded-image backing pointer.
                 * @return Matching shared image, or NULL when the backing is untracked.
                 */
                SharedLoadedImage* find_shared_image_by_backing(const void* backing) {
                    for (SharedLoadedImage* image = g_shared_image_head; image != NULL; image = image->next) {
                        if (image->backing == backing) {
                            return image;
                        }
                    }

                    return NULL;
                }

                void refresh_module_snapshot(LoadedModule* module);
                void refresh_shared_image_module_snapshots(SharedLoadedImage* image);

                /*
                 * Increment the live reference count of one shared loaded image.
                 *
                 * @param image Shared image being reused by another process object.
                 * @return Nothing.
                 */
                void retain_shared_image(SharedLoadedImage* image) {
                    if (image != NULL) {
                        image->reference_count += 1U;
                        refresh_shared_image_module_snapshots(image);
                    }
                }

                /*
                 * Release one shared loaded image and free its backing at the last user.
                 *
                 * @param image Shared image whose residency reference should be dropped.
                 * @return Nothing.
                 */
                void release_shared_image(SharedLoadedImage* image) {
                    if (image == NULL) {
                        return;
                    }

                    if (image->reference_count != 0U) {
                        image->reference_count -= 1U;
                    }
                    if (image->reference_count != 0U) {
                        refresh_shared_image_module_snapshots(image);
                        return;
                    }

                    unlink_shared_image(image);
                    if (image->backing != NULL) {
                        free_loader_backing(image->backing, image->image_bytes);
                        image->backing = NULL;
                    }
                    free_shared_image_record(image);
                }

                /*
                 * Create one shared loaded-image record for a fully prepared backing.
                 *
                 * The new record starts with one live reference owned by the caller that is
                 * about to attach the image to either a process executable or a module
                 * object.
                 *
                 * @param image_path Normalized image path.
                 * @param backing Canonical loaded image backing.
                 * @param image_bytes Page-rounded backing size.
                 * @param image_base Stable virtual base used in every process.
                 * @param image_type EXE, DLL, or driver image type.
                 * @param image_out Receives the shared-image record.
                 * @return StatusOK on success, or StatusNoMemory when allocation fails.
                 */
                Status create_shared_image_record(
                    const char* image_path,
                    void* backing,
                    Size image_bytes,
                    U16 image_type,
                    VirtAddr image_base,
                    SharedLoadedImage** image_out) {
                    SharedLoadedImage* image;

                    if ((image_path == NULL) || (backing == NULL) || (image_out == NULL)) {
                        return StatusInvalidArgument;
                    }

                    image = allocate_shared_image_record(static_cast<U64>(image_base), image_path);
                    if (image == NULL) {
                        return StatusNoMemory;
                    }

                    memzero(image, sizeof(*image));
                    image->reference_count = 1U;
                    image->image_type = image_type;
                    image->backing = backing;
                    image->image_bytes = image_bytes;
                    image->image_base = image_base;
                    if ((store_loader_text(image->path, sizeof(image->path), image_path, image_path) != StatusOK)
                        || (store_loader_text(image->module_name, sizeof(image->module_name), path_basename(image_path), path_basename(image_path)) != StatusOK)) {
                        free_shared_image_record(image);
                        return StatusNoSpace;
                    }

                    link_shared_image(image);
                    *image_out = image;
                    return StatusOK;
                }

                /*
                 * Copy one loader-owned text field into fixed inline record storage.
                 *
                 * Shared-image and loaded-module metadata now keep their normalized path
                 * and basename inline so one persistent record does not fan out into extra
                 * long-lived heap string allocations.
                 *
                 * @param destination Fixed-capacity text buffer inside one loader record.
                 * @param capacity Destination capacity in bytes.
                 * @param source Preferred source text.
                 * @param fallback Replacement text when `source` is empty.
                 * @return StatusOK on success, or StatusNoSpace when the text would truncate.
                 */
                Status store_loader_text(char* destination, Size capacity, const char* source, const char* fallback) {
                    Size index = 0U;
                    const char* active = ((source != NULL) && (source[0] != '\0')) ? source : fallback;

                    if ((destination == NULL) || (capacity == 0U) || (active == NULL)) {
                        return StatusInvalidArgument;
                    }

                    while (active[index] != '\0') {
                        if ((index + 1U) >= capacity) {
                            destination[0] = '\0';
                            return StatusNoSpace;
                        }

                        destination[index] = active[index];
                        ++index;
                    }

                    destination[index] = '\0';
                    return StatusOK;
                }

                const char* path_basename(const char* path) {
                    const char* basename = path;

                    if (path == NULL) {
                        return NULL;
                    }

                    while (*path != '\0') {
                        if ((*path == '/') || (*path == '\\')) {
                            basename = path + 1;
                        }
                        ++path;
                    }

                    return basename;
                }

                /*
                 * Return a readable image-type label for loader mapping traces.
                 *
                 * The raw `DLL_IMAGE_TYPE_*` constants are useful in switch logic but
                 * not in serial diagnostics. A short stable label makes it easier to
                 * grep the boot log for one EXE or DLL and compare image layouts across
                 * runs.
                 *
                 * @param image_type Parsed image-type field from the DLL container.
                 * @return Static label describing the image kind.
                 */
                const char* image_type_trace_name(U16 image_type) {
                    switch (image_type) {
                    case DLL_IMAGE_TYPE_EXE:
                        return "exe";
                    case DLL_IMAGE_TYPE_DLL:
                        return "dll";
                    case DLL_IMAGE_TYPE_DRIVER:
                        return "driver";
                    default:
                        return "image";
                    }
                }

                /*
                 * Return a semantic section-kind label for mapping traces.
                 *
                 * The user question is about code, data, and global-data placement in
                 * the process VA space. Translating raw section flags into those higher-
                 * level buckets makes the trace immediately useful without needing to
                 * decode PE-like attributes by hand.
                 *
                 * @param section_flags Packed `DLL_SEC_*` mask from one image section.
                 * @return Static label describing the section's runtime role.
                 */
                const char* image_section_kind_name(U32 section_flags) {
                    if ((section_flags & DLL_SEC_EXEC) != 0U) {
                        return "code";
                    }
                    if ((section_flags & DLL_SEC_WRITE) != 0U) {
                        if ((section_flags & DLL_SEC_BSS) != 0U) {
                            return "global-bss";
                        }

                        return "global-data";
                    }

                    return "rodata";
                }

                /*
                 * Render one section-flag bitmask into a short printable token.
                 *
                 * Serial traces need a compact permission string because every loaded
                 * module can contribute several sections. Keeping the format short makes
                 * it practical to scan for RX text or RW global ranges in a live boot
                 * log.
                 *
                 * @param section_flags Packed `DLL_SEC_*` mask from one image section.
                 * @param destination Output buffer receiving the textual flags.
                 * @param capacity Size of `destination` in bytes.
                 * @return Nothing.
                 */
                void format_image_section_flags(U32 section_flags, char* destination, Size capacity) {
                    Size write_index = 0U;

                    if ((destination == NULL) || (capacity == 0U)) {
                        return;
                    }

                    if ((write_index + 1U) < capacity) {
                        destination[write_index++] = ((section_flags & DLL_SEC_READ) != 0U) ? 'R' : '-';
                    }
                    if ((write_index + 1U) < capacity) {
                        destination[write_index++] = ((section_flags & DLL_SEC_WRITE) != 0U) ? 'W' : '-';
                    }
                    if ((write_index + 1U) < capacity) {
                        destination[write_index++] = ((section_flags & DLL_SEC_EXEC) != 0U) ? 'X' : '-';
                    }
                    if ((section_flags & DLL_SEC_BSS) != 0U) {
                        if ((write_index + 1U) < capacity) {
                            destination[write_index++] = 'B';
                        }
                    }

                    destination[write_index] = '\0';
                }

                /*
                 * Copy one fixed-width section name into a C string.
                 *
                 * DLL section names occupy an eight-byte field that is not required to
                 * end with NUL. Normalizing the field once here prevents every trace call
                 * site from repeating the same truncation logic.
                 *
                 * @param section Parsed image section whose name should be copied.
                 * @param destination Output buffer receiving the normalized section name.
                 * @param capacity Size of `destination` in bytes.
                 * @return Nothing.
                 */
                void copy_image_section_name(const dll_section* section, char* destination, Size capacity) {
                    Size index = 0U;

                    if ((section == NULL) || (destination == NULL) || (capacity == 0U)) {
                        return;
                    }

                    while ((index < sizeof(section->name)) && ((index + 1U) < capacity) && (section->name[index] != '\0')) {
                        destination[index] = section->name[index];
                        ++index;
                    }
                    destination[index] = '\0';
                }

                /*
                 * Emit one section-by-section virtual layout trace for a loaded image.
                 *
                 * The loader already knows the final image base while the parsed section
                 * table is still live. Logging that information here answers the common
                 * debugging question "where did code and globals land in this process?"
                 * without adding any new syscall or userspace ABI.
                 *
                 * @param path Display path for the image being traced.
                 * @param view Parsed image metadata that still owns the section table.
                 * @param image_base Final virtual base selected for the image.
                 * @param mapping_model Short text describing how the image is mapped.
                 * @return Nothing.
                 */
                void trace_image_section_layout(
                    const char* path,
                    const ParsedImageView* view,
                    VirtAddr image_base,
                    const char* mapping_model) {
                    const char* display_path = ((path != NULL) && (path[0] != '\0')) ? path : "(unknown)";

                    if ((view == NULL) || (view->header == NULL) || (view->sections == NULL)) {
                        return;
                    }

                    KRETAIL(
                        "[loader-map] %s path=%s base=0x%llx image_bytes=0x%llx sections=%u model=%s\n",
                        image_type_trace_name(view->header->image_type),
                        display_path,
                        static_cast<unsigned long long>(image_base),
                        static_cast<unsigned long long>(view->header->image_size),
                        static_cast<unsigned>(view->header->section_count),
                        (mapping_model != NULL) ? mapping_model : "default");

                    for (U32 section_index = 0U; section_index < view->header->section_count; ++section_index) {
                        const dll_section* section = &view->sections[section_index];
                        char section_name[9] = {};
                        char section_flags[5] = {};
                        const U64 section_span = (section->virtual_size >= section->raw_data_size)
                            ? section->virtual_size
                            : section->raw_data_size;
                        const VirtAddr section_base = image_base + section->virtual_address;
                        const VirtAddr section_limit = section_base + section_span;

                        copy_image_section_name(section, section_name, sizeof(section_name));
                        format_image_section_flags(section->flags, section_flags, sizeof(section_flags));

                        KRETAIL(
                            "[loader-map]   section=%s kind=%s range=0x%llx-0x%llx rva=0x%llx raw=0x%llx virt=0x%llx flags=%s backing=%s\n",
                            (section_name[0] != '\0') ? section_name : "(anon)",
                            image_section_kind_name(section->flags),
                            static_cast<unsigned long long>(section_base),
                            static_cast<unsigned long long>(section_limit),
                            static_cast<unsigned long long>(section->virtual_address),
                            static_cast<unsigned long long>(section->raw_data_size),
                            static_cast<unsigned long long>(section->virtual_size),
                            section_flags,
                            ((section->flags & DLL_SEC_WRITE) != 0U) ? "private/global" : "shared/read-only");
                    }
                }

                inline constexpr unsigned long UserTaskModuleFlagSharedBacking = 1UL;
                inline constexpr unsigned long UserTaskModuleFlagPendingAttach = 2UL;
                inline constexpr unsigned long UserTaskModuleFlagPendingDetach = 4UL;
                inline constexpr unsigned long UserTaskModuleFlagPrivateWritable = 8UL;
                inline constexpr unsigned long UserTaskModuleFlagSectionTruncated = 16UL;

                void copy_snapshot_text(char* destination, Size capacity, const char* source) {
                    Size index = 0U;

                    if ((destination == NULL) || (capacity == 0U)) {
                        return;
                    }

                    if (source == NULL) {
                        destination[0] = '\0';
                        return;
                    }

                    while ((source[index] != '\0') && ((index + 1U) < capacity)) {
                        destination[index] = source[index];
                        ++index;
                    }

                    destination[index] = '\0';
                }

                unsigned long module_private_backing_bytes(const LoadedModule* module) {
                    if ((module == NULL) || (module->private_page_count == 0U)) {
                        return 0UL;
                    }

                    return static_cast<unsigned long>(module->private_page_count) * static_cast<unsigned long>(mm::PageSize);
                }

                /*
                 * Capture one compact section-layout snapshot from a mapped image backing.
                 *
                 * rcman needs section address ranges in the target process address space, but
                 * recomputing that layout in the syscall path would duplicate loader parsing
                 * logic. The loader already owns the validated section table, so it caches a
                 * compact copy inside each module snapshot.
                 *
                 * @param backing Resident image backing whose section table should be copied.
                 * @param image_base Final process virtual base for that image.
                 * @param sections Destination fixed-capacity section snapshot array.
                 * @param capacity Number of elements available in `sections`.
                 * @param copied_count_out Receives the number of copied section records.
                 * @param truncated_out Receives whether the image had more sections than fit.
                 * @return Nothing.
                 */
                void capture_image_section_snapshot(
                    const void* backing,
                    VirtAddr image_base,
                    DllLoaderSectionSnapshot* sections,
                    unsigned long capacity,
                    unsigned long* copied_count_out,
                    bool* truncated_out) {
                    const dll_header* header = header_of_backing(backing);
                    const dll_section* section_table = sections_of_backing(backing);
                    unsigned long copied_count = 0UL;
                    bool truncated = false;

                    if (copied_count_out != NULL) {
                        *copied_count_out = 0UL;
                    }
                    if (truncated_out != NULL) {
                        *truncated_out = false;
                    }
                    if ((header == NULL) || (section_table == NULL) || ((sections == NULL) && (capacity != 0UL))) {
                        return;
                    }

                    for (U32 section_index = 0U; section_index < header->section_count; ++section_index) {
                        const dll_section* section = &section_table[section_index];
                        const U64 section_span = (section->virtual_size >= section->raw_data_size)
                            ? section->virtual_size
                            : section->raw_data_size;

                        if (section_span == 0U) {
                            continue;
                        }
                        if (copied_count >= capacity) {
                            truncated = true;
                            continue;
                        }

                        memzero(&sections[copied_count], sizeof(sections[copied_count]));
                        sections[copied_count].start_address = static_cast<unsigned long>(image_base + section->virtual_address);
                        sections[copied_count].end_address = static_cast<unsigned long>(image_base + section->virtual_address + section_span);
                        sections[copied_count].flags = static_cast<unsigned long>(section->flags);
                        copy_image_section_name(section, sections[copied_count].name, sizeof(sections[copied_count].name));
                        copied_count += 1UL;
                    }

                    if (copied_count_out != NULL) {
                        *copied_count_out = copied_count;
                    }
                    if (truncated_out != NULL) {
                        *truncated_out = truncated;
                    }
                }

                /*
                 * Rebuild one cached module snapshot from the live loader record.
                 *
                 * rcman and the task-module syscall should observe loader state but not
                 * derive it on the fly from backing pointers or writable-section arrays.
                 * Keeping the exported view cached inside the module record turns module
                 * enumeration into a plain registry copy path.
                 *
                 * @param module Loader module whose cached snapshot should be refreshed.
                 * @return Nothing.
                 */
                void refresh_module_snapshot(LoadedModule* module) {
                    bool sections_truncated = false;

                    if (module == NULL) {
                        return;
                    }

                    memzero(&module->snapshot, sizeof(module->snapshot));
                    module->snapshot.image_base = static_cast<unsigned long>(module->image_base);
                    module->snapshot.image_bytes = static_cast<unsigned long>(module->image_bytes);
                    module->snapshot.shared_backing_bytes = static_cast<unsigned long>(module->image_bytes);
                    module->snapshot.private_backing_bytes = module_private_backing_bytes(module);
                    module->snapshot.shared_reference_count = (module->shared_image != NULL)
                        ? static_cast<unsigned long>(module->shared_image->reference_count)
                        : 0UL;
                    if (module->shared_image != NULL) {
                        module->snapshot.flags |= UserTaskModuleFlagSharedBacking;
                    }
                    if (module->pending_process_attach) {
                        module->snapshot.flags |= UserTaskModuleFlagPendingAttach;
                    }
                    if (module->pending_process_detach) {
                        module->snapshot.flags |= UserTaskModuleFlagPendingDetach;
                    }
                    if (module->private_page_count != 0U) {
                        module->snapshot.flags |= UserTaskModuleFlagPrivateWritable;
                    }

                    copy_snapshot_text(module->snapshot.module_name, sizeof(module->snapshot.module_name), module->module_name);
                    copy_snapshot_text(module->snapshot.path, sizeof(module->snapshot.path), module->path);
                    capture_image_section_snapshot(
                        module->backing,
                        module->image_base,
                        module->snapshot.sections,
                        COUNT_OF(module->snapshot.sections),
                        &module->snapshot.section_count,
                        &sections_truncated);
                    if (sections_truncated) {
                        module->snapshot.flags |= UserTaskModuleFlagSectionTruncated;
                    }
                }

                /*
                 * Refresh cached snapshots for every module backed by one shared image.
                 *
                 * Shared reference counts live on the shared-image record, so any retain or
                 * release must update every module snapshot that exports that count.
                 *
                 * @param image Shared image whose dependent module snapshots should refresh.
                 * @return Nothing.
                 */
                void refresh_shared_image_module_snapshots(SharedLoadedImage* image) {
                    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

                    if (image != NULL) {
                        for (LoadedModule* module = g_module_head; module != NULL; module = module->next) {
                            if (module->shared_image == image) {
                                refresh_module_snapshot(module);
                            }
                        }
                    }

                    arch::Arch::restore_interrupts(interrupts_enabled);
                }

                /*
                 * Return the union of section flags that touch one image page.
                 *
                 * Packed DLL sections are not guaranteed to start or end on page
                 * boundaries, so the mapper must aggregate permissions page-by-page. This
                 * keeps execute permission on mixed text/rodata pages and forces any page
                 * with writable bytes to use a private per-process clone.
                 *
                 * @param backing Resident image backing.
                 * @param page_rva Page-aligned image-relative page offset.
                 * @return Union of `DLL_SEC_*` flags for overlapping sections.
                 */
                U32 page_flags_in_backing(const void* backing, U64 page_rva) {
                    const dll_header* header = header_of_backing(backing);
                    const dll_section* sections = sections_of_backing(backing);
                    U32 page_flags = 0U;

                    if ((header == NULL) || (sections == NULL)) {
                        return 0U;
                    }

                    for (U32 section_index = 0U; section_index < header->section_count; ++section_index) {
                        const dll_section* section = &sections[section_index];

                        if ((section->virtual_size == 0U)
                            || !ranges_overlap(
                                static_cast<VirtAddr>(page_rva),
                                mm::PageSize,
                                static_cast<VirtAddr>(section->virtual_address),
                                static_cast<Size>(section->virtual_size))) {
                            continue;
                        }

                        page_flags |= section->flags;
                    }

                    return page_flags;
                }

                /*
                 * Round one image-relative offset down to the owning page boundary.
                 *
                 * Rebasing and import patching operate on individual absolute locations, but
                 * the MMU can only swap whole pages between the shared backing and a
                 * process-private clone. This helper canonicalizes every patch site onto the
                 * page-sized key used by the private-page list.
                 *
                 * @param image_rva Image-relative byte offset.
                 * @return Page-aligned image-relative offset.
                 */
                U64 align_down_image_page(U64 image_rva) {
                    return image_rva & ~(static_cast<U64>(mm::PageSize) - 1ULL);
                }

                /*
                 * Find one private page record by image-relative page offset.
                 *
                 * The page list stays sorted by `virtual_address` so the mapper can walk it
                 * linearly while it scans image pages in ascending order.
                 *
                 * @param pages Head of the private-page list.
                 * @param page_rva Page-aligned image-relative offset.
                 * @param previous_out Receives the insertion predecessor when requested.
                 * @return Matching private page, or NULL when none exists yet.
                 */
                LoaderPrivatePage* find_private_image_page(
                    LoaderPrivatePage* pages,
                    U64 page_rva,
                    LoaderPrivatePage** previous_out) {
                    LoaderPrivatePage* previous = NULL;
                    LoaderPrivatePage* current = pages;

                    while ((current != NULL) && (current->virtual_address < page_rva)) {
                        previous = current;
                        current = current->next;
                    }

                    if (previous_out != NULL) {
                        *previous_out = previous;
                    }
                    if ((current != NULL) && (current->virtual_address == page_rva)) {
                        return current;
                    }

                    return NULL;
                }

                /*
                 * Release one process-private image-page list.
                 *
                 * Every cloned page owns both a one-page backing allocation and a pooled
                 * metadata record. Releasing the list therefore needs to free the backing
                 * first and then return the record to the page-backed pool.
                 *
                 * @param pages Head of the page list to destroy.
                 * @return Nothing.
                 */
                void free_private_image_pages(LoaderPrivatePage* pages) {
                    while (pages != NULL) {
                        LoaderPrivatePage* next = pages->next;

                        if (pages->backing != NULL) {
                            free_loader_backing(pages->backing, pages->mapped_bytes);
                            pages->backing = NULL;
                        }

                        free_private_page_record(pages);
                        pages = next;
                    }
                }

                /*
                 * Ensure one image page has a process-private clone.
                 *
                 * A page may need cloning for several reasons: writable state, import slot
                 * patching, or base-delta relocation. The first caller creates the clone and
                 * later callers simply OR in their additional patch reason.
                 *
                 * @param backing Canonical resident image backing.
                 * @param page_rva Page-aligned image-relative offset to clone.
                 * @param page_flags Aggregated section flags for that page.
                 * @param patch_flags Reason bits that require the clone.
                 * @param pages_inout Sorted private-page list head.
                 * @param page_count_inout Private-page count to increment on first clone.
                 * @param page_out Receives the existing or newly cloned page record.
                 * @return StatusOK on success, or an allocation failure.
                 */
                Status ensure_private_image_page(
                    const void* backing,
                    U64 page_rva,
                    U32 page_flags,
                    U32 patch_flags,
                    LoaderPrivatePage** pages_inout,
                    U32* page_count_inout,
                    LoaderPrivatePage** page_out) {
                    LoaderPrivatePage* previous = NULL;
                    LoaderPrivatePage* page;
                    void* private_backing;

                    if ((backing == NULL) || (pages_inout == NULL) || (page_count_inout == NULL)) {
                        return StatusInvalidArgument;
                    }

                    page = find_private_image_page(*pages_inout, page_rva, &previous);
                    if (page != NULL) {
                        page->patch_flags |= patch_flags;
                        if (page_out != NULL) {
                            *page_out = page;
                        }
                        return StatusOK;
                    }

                    page = allocate_private_page_record();
                    if (page == NULL) {
                        return StatusNoMemory;
                    }

                    private_backing = allocate_loader_backing(mm::PageSize);
                    if (private_backing == NULL) {
                        free_private_page_record(page);
                        return StatusNoMemory;
                    }

                    memcopy(private_backing, static_cast<const U8*>(backing) + page_rva, mm::PageSize);
                    page->backing = private_backing;
                    page->virtual_address = page_rva;
                    page->mapped_bytes = mm::PageSize;
                    page->section_flags = page_flags;
                    page->patch_flags = patch_flags;
                    if (previous != NULL) {
                        page->next = previous->next;
                        previous->next = page;
                    }
                    else {
                        page->next = *pages_inout;
                        *pages_inout = page;
                    }

                    *page_count_inout += 1U;
                    if (page_out != NULL) {
                        *page_out = page;
                    }
                    return StatusOK;
                }

                /*
                 * Clone every writable image page for one process.
                 *
                 * Writable globals and BSS can never stay shared between unrelated process
                 * address spaces, so those pages always become process-private before any
                 * later rebasing or IAT patching is applied.
                 *
                 * @param backing Canonical resident image backing.
                 * @param pages_inout Sorted private-page list head.
                 * @param page_count_inout Private-page count.
                 * @return StatusOK on success, or an allocation failure.
                 */
                Status clone_writable_image_pages(
                    const void* backing,
                    LoaderPrivatePage** pages_inout,
                    U32* page_count_inout) {
                    const dll_header* header = header_of_backing(backing);

                    if ((backing == NULL) || (pages_inout == NULL) || (page_count_inout == NULL)) {
                        return StatusInvalidArgument;
                    }
                    if (header == NULL) {
                        return StatusFault;
                    }

                    for (U64 page_rva = 0U; page_rva < align_up_image_bytes(static_cast<Size>(header->image_size)); page_rva += mm::PageSize) {
                        const U32 page_flags = page_flags_in_backing(backing, page_rva);

                        if ((page_flags & DLL_SEC_WRITE) == 0U) {
                            continue;
                        }

                        const Status status = ensure_private_image_page(
                            backing,
                            page_rva,
                            page_flags,
                            LoaderPrivatePagePatchWritable,
                            pages_inout,
                            page_count_inout,
                            NULL);
                        if (status != StatusOK) {
                            return status;
                        }
                    }

                    return StatusOK;
                }

                /*
                 * Apply one process-specific base delta into cloned image pages.
                 *
                 * Shared DLL backings remain relocated to one canonical base. When a target
                 * process chooses a different module base, only the pages that contain
                 * absolute relocations are cloned and adjusted by the new base delta.
                 *
                 * @param backing Canonical resident image backing.
                 * @param canonical_base Base encoded into the shared backing.
                 * @param target_base Base selected for the target process.
                 * @param pages_inout Sorted private-page list head.
                 * @param page_count_inout Private-page count.
                 * @return StatusOK on success, StatusNotSupported for unknown relocation types,
                 * or an allocation failure.
                 */
                Status apply_private_image_relocations(
                    const void* backing,
                    VirtAddr canonical_base,
                    VirtAddr target_base,
                    LoaderPrivatePage** pages_inout,
                    U32* page_count_inout) {
                    const dll_header* header = header_of_backing(backing);
                    const dll_relocation* relocations = relocations_of_backing(backing);
                    const I64 base_delta = static_cast<I64>(target_base) - static_cast<I64>(canonical_base);

                    if ((backing == NULL) || (pages_inout == NULL) || (page_count_inout == NULL)) {
                        return StatusInvalidArgument;
                    }
                    if ((header == NULL) || (relocations == NULL)) {
                        return StatusFault;
                    }
                    if ((base_delta == 0) || (header->relocation_count == 0U)) {
                        return StatusOK;
                    }

                    for (U32 relocation_index = 0U; relocation_index < header->relocation_count; ++relocation_index) {
                        const dll_relocation* relocation = &relocations[relocation_index];
                        const U64 page_rva = align_down_image_page(relocation->target_rva);
                        const U32 page_flags = page_flags_in_backing(backing, page_rva);
                        LoaderPrivatePage* page = NULL;
                        U8* patch_bytes;
                        Status status;

                        status = ensure_private_image_page(
                            backing,
                            page_rva,
                            page_flags,
                            LoaderPrivatePagePatchRelocation,
                            pages_inout,
                            page_count_inout,
                            &page);
                        if (status != StatusOK) {
                            return status;
                        }

                        patch_bytes = static_cast<U8*>(page->backing) + (relocation->target_rva - page_rva);
                        if (relocation->type == DLL_RELOC_ABS64) {
                            *reinterpret_cast<U64*>(patch_bytes) = static_cast<U64>(static_cast<I64>(*reinterpret_cast<U64*>(patch_bytes)) + base_delta);
                            continue;
                        }
                        if (relocation->type == DLL_RELOC_ABS32) {
                            *reinterpret_cast<U32*>(patch_bytes) = static_cast<U32>(static_cast<I32>(*reinterpret_cast<U32*>(patch_bytes)) + static_cast<I32>(base_delta));
                            continue;
                        }

                        return StatusNotSupported;
                    }

                    return StatusOK;
                }

                /*
                 * Patch one image IAT against the modules loaded in the target process.
                 *
                 * Once DLLs may land at different bases in different processes, shared image
                 * backings can no longer keep one canonical import table. Instead, the loader
                 * clones only the pages that contain IAT slots and writes export addresses
                 * resolved from the modules already loaded in the target process.
                 *
                 * @param process Target process whose module bases should drive import binding.
                 * @param backing Canonical resident image backing.
                 * @param pages_inout Sorted private-page list head.
                 * @param page_count_inout Private-page count.
                 * @return StatusOK on success, or the dependency/export lookup failure.
                 */
                Status apply_private_image_imports(
                    Process* process,
                    const void* backing,
                    LoaderPrivatePage** pages_inout,
                    U32* page_count_inout) {
                    const dll_header* header = header_of_backing(backing);
                    const dll_import_module* import_modules = imports_of_backing(backing);
                    const dll_import_symbol* import_symbols = import_symbols_of_backing(backing);
                    const char* string_table = string_table_of_backing(backing);

                    if ((process == NULL) || (backing == NULL) || (pages_inout == NULL) || (page_count_inout == NULL)) {
                        return StatusInvalidArgument;
                    }
                    if ((header == NULL) || (import_modules == NULL) || (import_symbols == NULL) || (string_table == NULL)) {
                        return StatusFault;
                    }
                    if ((header->import_module_count == 0U) || (header->import_symbol_count == 0U)) {
                        return StatusOK;
                    }

                    for (U32 module_index = 0U; module_index < header->import_module_count; ++module_index) {
                        const dll_import_module* import_module = &import_modules[module_index];
                        const char* import_module_name = bounded_string_at(string_table, header->string_table_size, import_module->module_name_offset);
                        LoadedModule* dependency = find_module_by_path(process, import_module_name);

                        if (dependency == NULL) {
                            return StatusNotFound;
                        }

                        for (U32 symbol_offset = 0U; symbol_offset < import_module->symbol_count; ++symbol_offset) {
                            const dll_import_symbol* import_symbol = &import_symbols[import_module->first_symbol_index + symbol_offset];
                            const char* import_symbol_name = bounded_string_at(string_table, header->string_table_size, import_symbol->symbol_name_offset);
                            const U64 page_rva = align_down_image_page(import_symbol->iat_rva);
                            const U32 page_flags = page_flags_in_backing(backing, page_rva);
                            LoaderPrivatePage* page = NULL;
                            VirtAddr export_address = 0U;
                            Status status;

                            status = lookup_export_address(dependency, import_symbol_name, &export_address);
                            if (status != StatusOK) {
                                return status;
                            }

                            status = ensure_private_image_page(
                                backing,
                                page_rva,
                                page_flags,
                                LoaderPrivatePagePatchImport,
                                pages_inout,
                                page_count_inout,
                                &page);
                            if (status != StatusOK) {
                                return status;
                            }

                            *reinterpret_cast<U64*>(static_cast<U8*>(page->backing) + (import_symbol->iat_rva - page_rva)) = export_address;
                        }
                    }

                    return StatusOK;
                }

                /*
                 * Build the full process-private page set for one mapped image.
                 *
                 * This helper centralizes the three reasons an image page may stop being
                 * shared: writable data, base-delta relocations, and process-specific IAT
                 * patching. The resulting sorted list can be consumed directly by the final
                 * mapper for either DLLs or shared executables.
                 *
                 * @param process Target process receiving the image.
                 * @param backing Canonical resident image backing.
                 * @param canonical_base Base encoded into the shared backing.
                 * @param target_base Base selected for the target process.
                 * @param clone_writable_pages Whether writable pages must always be cloned.
                 * @param owner_id Diagnostic owner identifier reserved for future accounting.
                 * @param label Diagnostic label reserved for future accounting.
                 * @param pages_out Receives the built private-page list.
                 * @param page_count_out Receives the number of private pages.
                 * @return StatusOK on success, or the first clone/patch failure.
                 */
                Status build_process_private_image_pages(
                    Process* process,
                    const void* backing,
                    VirtAddr canonical_base,
                    VirtAddr target_base,
                    bool clone_writable_pages,
                    U64 owner_id,
                    const char* label,
                    LoaderPrivatePage** pages_out,
                    U32* page_count_out) {
                    LoaderPrivatePage* pages = NULL;
                    U32 page_count = 0U;
                    Status status = StatusOK;

                    (void)owner_id;
                    (void)label;

                    if ((process == NULL) || (backing == NULL) || (pages_out == NULL) || (page_count_out == NULL)) {
                        return StatusInvalidArgument;
                    }

                    *pages_out = NULL;
                    *page_count_out = 0U;

                    if (clone_writable_pages) {
                        status = clone_writable_image_pages(backing, &pages, &page_count);
                    }
                    if (status == StatusOK) {
                        status = apply_private_image_relocations(backing, canonical_base, target_base, &pages, &page_count);
                    }
                    if (status == StatusOK) {
                        status = apply_private_image_imports(process, backing, &pages, &page_count);
                    }
                    if (status != StatusOK) {
                        free_private_image_pages(pages);
                        return status;
                    }

                    *pages_out = pages;
                    *page_count_out = page_count;
                    return StatusOK;
                }

                /*
                 * Convert DLL section flags into MMU mapping flags.
                 *
                 * Section-level mappings let the loader preserve execute permissions for
                 * shared text while keeping mutable pages writable only in private clones.
                 *
                 * @param section_flags Packed `DLL_SEC_*` mask.
                 * @return Page-table mapping flags.
                 */
                U64 section_mapping_flags(U32 section_flags) {
                    U64 mapping_flags = PagePresent | PageUser;

                    if ((section_flags & DLL_SEC_WRITE) != 0U) {
                        mapping_flags |= PageWritable;
                    }
                    if ((section_flags & DLL_SEC_EXEC) != 0U) {
                        mapping_flags |= PageExecutable;
                    }

                    return mapping_flags;
                }

                /*
                 * Map one page-aligned image segment into a process address space.
                 *
                 * DLL mappings now combine shared canonical segments and per-process
                 * writable clones, so each image segment is installed independently.
                 *
                 * @param process Target process receiving the segment.
                 * @param image_base Module base in the target process.
                 * @param segment_rva Page-aligned segment RVA.
                 * @param segment_backing Backing pointer for the segment.
                 * @param segment_bytes Page-rounded segment length.
                 * @param mapping_flags Page-table flags for the segment.
                 * @return StatusOK on success, or the MMU mapping error.
                 */
                Status map_image_segment(
                    Process* process,
                    VirtAddr image_base,
                    U64 segment_rva,
                    const void* segment_backing,
                    Size segment_bytes,
                    U64 mapping_flags) {
                    const VmMapping mapping = {
                        image_base + segment_rva,
                        mm::MemoryManager::kernel_to_physical(reinterpret_cast<VirtAddr>(segment_backing)),
                        segment_bytes,
                        mapping_flags,
                    };

                    if ((process == NULL) || (segment_backing == NULL) || (segment_bytes == 0U)) {
                        return StatusInvalidArgument;
                    }

                    return mm::MemoryManager::map(&process->process_address_space, &mapping);
                }

                /*
                 * Map one image backing while honoring any process-private page clones.
                 *
                 * The loader scans image pages in ascending RVA order. Keeping the clone list
                 * sorted by the same key lets the mapper swap shared pages for private ones in
                 * one linear pass without an auxiliary lookup table.
                 *
                 * @param process Target process receiving the mapping.
                 * @param image_base Virtual base chosen for the image in that process.
                 * @param image_backing Canonical resident backing.
                 * @param private_pages Sorted list of process-private page clones.
                 * @return StatusOK on success, or StatusFault when the clone list is malformed.
                 */
                Status map_image_with_private_pages(
                    Process* process,
                    VirtAddr image_base,
                    const void* image_backing,
                    LoaderPrivatePage* private_pages) {
                    const dll_header* header = header_of_backing(image_backing);
                    const Size image_bytes = align_up_image_bytes((header != NULL) ? static_cast<Size>(header->image_size) : 0U);
                    LoaderPrivatePage* next_private_page = private_pages;
                    Status status;

                    if ((process == NULL) || (image_backing == NULL) || (header == NULL) || (image_bytes == 0U)) {
                        return StatusInvalidArgument;
                    }

                    status = map_image_segment(
                        process,
                        image_base,
                        0U,
                        image_backing,
                        align_up_image_bytes(static_cast<Size>(header->header_size)),
                        PagePresent | PageUser);
                    if (status != StatusOK) {
                        return status;
                    }

                    for (U64 page_rva = align_up_image_bytes(static_cast<Size>(header->header_size)); page_rva < image_bytes; page_rva += mm::PageSize) {
                        const U32 page_flags = page_flags_in_backing(image_backing, page_rva);
                        const void* segment_backing = static_cast<const U8*>(image_backing) + page_rva;
                        U32 mapping_section_flags = page_flags;

                        if (page_flags == 0U) {
                            continue;
                        }
                        if ((next_private_page != NULL) && (next_private_page->virtual_address < page_rva)) {
                            return StatusFault;
                        }
                        if ((next_private_page != NULL) && (next_private_page->virtual_address == page_rva)) {
                            if ((next_private_page->backing == NULL) || (next_private_page->mapped_bytes != mm::PageSize)) {
                                return StatusFault;
                            }

                            segment_backing = next_private_page->backing;
                            mapping_section_flags = next_private_page->section_flags;
                            next_private_page = next_private_page->next;
                        }

                        status = map_image_segment(
                            process,
                            image_base,
                            page_rva,
                            segment_backing,
                            mm::PageSize,
                            section_mapping_flags(mapping_section_flags));
                        if (status != StatusOK) {
                            return status;
                        }
                    }

                    return (next_private_page == NULL) ? StatusOK : StatusFault;
                }

                bool path_has_drive_prefix(const char* path) {
                    char letter;

                    if ((path == NULL) || (path[0] == '\0') || (path[1] != ':')) {
                        return false;
                    }

                    letter = ascii_lower(path[0]);
                    return (letter >= 'a') && (letter <= 'z');
                }

                Status normalize_path(const char* module_name, char* normalized_path, Size capacity) {
                    const char* prefix = "";
                    Size prefix_length = 0U;
                    Size source_length = 0U;
                    Size write_index = 0U;
                    const char* cursor = module_name;

                    if ((module_name == NULL) || (module_name[0] == '\0') || (normalized_path == NULL) || (capacity == 0U)) {
                        return StatusInvalidArgument;
                    }

                    while (module_name[source_length] != '\0') {
                        ++source_length;
                    }

                    if (path_has_drive_prefix(module_name)) {
                        if ((source_length + 1U) > capacity) {
                            return StatusNoSpace;
                        }

                        while (cursor[write_index] != '\0') {
                            normalized_path[write_index] = (cursor[write_index] == '\\') ? '/' : cursor[write_index];
                            ++write_index;
                        }
                        normalized_path[write_index] = '\0';
                        return StatusOK;
                    }

                    if ((module_name[0] == '/') || (module_name[0] == '\\')) {
                        prefix = "C:";
                        prefix_length = 2U;
                    }
                    else {
                        prefix = "C:/lib/";
                        prefix_length = 7U;
                    }

                    if ((prefix_length + source_length + 1U) > capacity) {
                        return StatusNoSpace;
                    }

                    for (Size index = 0U; index < prefix_length; ++index) {
                        normalized_path[write_index++] = prefix[index];
                    }

                    while (*cursor != '\0') {
                        normalized_path[write_index++] = (*cursor == '\\') ? '/' : *cursor;
                        ++cursor;
                    }

                    normalized_path[write_index] = '\0';
                    return StatusOK;
                }

                /*
                 * Release a parsed image view and the underlying file-byte ownership.
                 *
                 * Views sourced from the global image cache release one shared cache
                 * reference. Uncached views continue to free their private staging buffer.
                 *
                 * @param view Parsed image view to release.
                 * @return Nothing.
                 */
                void release_view(ParsedImageView* view) {
                    if (view == NULL) {
                        return;
                    }

                    if (view->cache_object != NULL) {
                        cached_image_release(view->cache_object);
                    }
                    else if (view->file_bytes != NULL) {
                        Heap::free(const_cast<U8*>(view->file_bytes));
                    }

                    memzero(view, sizeof(*view));
                }

                const dll_header* header_of_backing(const void* backing) {
                    return (backing == NULL) ? NULL : reinterpret_cast<const dll_header*>(backing);
                }

                /*
                 * Return the resident section table for one loaded image backing.
                 *
                 * Shared-image reuse and per-process writable clones both operate after the
                 * staging file buffer is gone, so they need the section table directly from
                 * the resident backing.
                 *
                 * @param backing Resident EXE or DLL backing.
                 * @return Section-table pointer, or NULL when the backing is invalid.
                 */
                const dll_section* sections_of_backing(const void* backing) {
                    const dll_header* header = header_of_backing(backing);

                    if ((header == NULL) || (backing == NULL)) {
                        return NULL;
                    }

                    return reinterpret_cast<const dll_section*>(static_cast<const U8*>(backing) + header->section_table_offset);
                }

                const dll_header* header_of(const LoadedModule* module) {
                    return (module == NULL) ? NULL : header_of_backing(module->backing);
                }

                const dll_export_symbol* exports_of_backing(const void* backing) {
                    const dll_header* header = header_of_backing(backing);

                    if ((header == NULL) || (backing == NULL)) {
                        return NULL;
                    }

                    return reinterpret_cast<const dll_export_symbol*>(static_cast<const U8*>(backing) + header->export_table_offset);
                }

                const dll_export_symbol* exports_of(const LoadedModule* module) {
                    return (module == NULL) ? NULL : exports_of_backing(module->backing);
                }

                const dll_import_module* imports_of_backing(const void* backing) {
                    const dll_header* header = header_of_backing(backing);

                    if ((header == NULL) || (backing == NULL)) {
                        return NULL;
                    }

                    return reinterpret_cast<const dll_import_module*>(static_cast<const U8*>(backing) + header->import_module_table_offset);
                }

                const dll_import_module* imports_of(const LoadedModule* module) {
                    return (module == NULL) ? NULL : imports_of_backing(module->backing);
                }

                /*
                 * Return the resident import-symbol table for one loaded image backing.
                 *
                 * Process-specific IAT patching happens after the staging file buffer is
                 * gone, so the rebasing path must recover the import-symbol table from the
                 * resident backing itself.
                 *
                 * @param backing Resident EXE or DLL backing.
                 * @return Import-symbol table pointer, or NULL when the backing is invalid.
                 */
                const dll_import_symbol* import_symbols_of_backing(const void* backing) {
                    const dll_header* header = header_of_backing(backing);

                    if ((header == NULL) || (backing == NULL)) {
                        return NULL;
                    }

                    return reinterpret_cast<const dll_import_symbol*>(static_cast<const U8*>(backing) + header->import_symbol_table_offset);
                }

                /*
                 * Return the resident relocation table for one loaded image backing.
                 *
                 * Shared DLL backings now stay at one canonical base, and each process adds
                 * its own base delta by cloning only the pages that contain absolute
                 * relocations. That rebasing logic needs direct access to the relocation
                 * table from the resident backing.
                 *
                 * @param backing Resident EXE or DLL backing.
                 * @return Relocation-table pointer, or NULL when the backing is invalid.
                 */
                const dll_relocation* relocations_of_backing(const void* backing) {
                    const dll_header* header = header_of_backing(backing);

                    if ((header == NULL) || (backing == NULL)) {
                        return NULL;
                    }

                    return reinterpret_cast<const dll_relocation*>(static_cast<const U8*>(backing) + header->relocation_table_offset);
                }

                const char* string_table_of_backing(const void* backing) {
                    const dll_header* header = header_of_backing(backing);

                    if ((header == NULL) || (backing == NULL)) {
                        return NULL;
                    }

                    return reinterpret_cast<const char*>(static_cast<const U8*>(backing) + header->string_table_offset);
                }

                const char* string_table_of(const LoadedModule* module) {
                    return (module == NULL) ? NULL : string_table_of_backing(module->backing);
                }

                const char* bounded_string_at(const char* string_table, U32 string_table_size, U32 string_offset) {
                    U32 index;

                    if ((string_table == NULL) || (string_offset >= string_table_size)) {
                        return NULL;
                    }

                    for (index = string_offset; index < string_table_size; ++index) {
                        if (string_table[index] == '\0') {
                            return string_table + string_offset;
                        }
                    }

                    return NULL;
                }

                const char* string_at(const LoadedModule* module, U32 string_offset) {
                    const dll_header* header = header_of(module);

                    if (header == NULL) {
                        return NULL;
                    }

                    return bounded_string_at(string_table_of(module), header->string_table_size, string_offset);
                }

                const char* view_string_at(const ParsedImageView* view, U32 string_offset) {
                    if ((view == NULL) || (view->header == NULL)) {
                        return NULL;
                    }

                    return bounded_string_at(view->string_table, view->header->string_table_size, string_offset);
                }

                bool matches_path(const LoadedModule* module, const char* module_path) {
                    char normalized_path[VFS_PATH_CAPACITY];
                    const char* basename;
                    Status status;

                    if ((module == NULL) || (module_path == NULL)) {
                        return false;
                    }

                    status = normalize_path(module_path, normalized_path, sizeof(normalized_path));
                    if (status != StatusOK) {
                        return false;
                    }
                    basename = path_basename(normalized_path);
                    const bool matches = same_text_case_insensitive(module->path, normalized_path)
                        || same_text_case_insensitive(module->module_name, module_path)
                        || same_text_case_insensitive(module->module_name, basename);
                    return matches;
                }

                void link_module(LoadedModule* module) {
                    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

                    module->prev = g_module_tail;
                    module->next = NULL;

                    if (g_module_tail != NULL) {
                        g_module_tail->next = module;
                    }
                    else {
                        g_module_head = module;
                    }

                    g_module_tail = module;
                    arch::Arch::restore_interrupts(interrupts_enabled);
                }

                void unlink_module(LoadedModule* module) {
                    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

                    if (module->prev != NULL) {
                        module->prev->next = module->next;
                    }
                    else {
                        g_module_head = module->next;
                    }

                    if (module->next != NULL) {
                        module->next->prev = module->prev;
                    }
                    else {
                        g_module_tail = module->prev;
                    }

                    module->prev = NULL;
                    module->next = NULL;
                    arch::Arch::restore_interrupts(interrupts_enabled);
                }

                LoadedModule* find_module_by_handle(Process* process, VirtAddr module_handle) {
                    LoadedModule* found = NULL;
                    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

                    for (LoadedModule* module = g_module_head; module != NULL; module = module->next) {
                        if ((module->owner_process == process) && (module->image_base == module_handle)) {
                            found = module;
                            break;
                        }
                    }

                    arch::Arch::restore_interrupts(interrupts_enabled);
                    return found;
                }

                LoadedModule* find_module_by_path(Process* process, const char* module_path) {
                    LoadedModule* found = NULL;
                    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

                    for (LoadedModule* module = g_module_head; module != NULL; module = module->next) {
                        if ((module->owner_process == process) && matches_path(module, module_path)) {
                            found = module;
                            break;
                        }
                    }

                    arch::Arch::restore_interrupts(interrupts_enabled);
                    return found;
                }

                bool slot_in_use(Process* process, VirtAddr image_base) {
                    return find_module_by_handle(process, image_base) != NULL;
                }

                /*
                 * Return whether two virtual ranges overlap.
                 *
                 * @param first_base First range base.
                 * @param first_bytes First range length.
                 * @param second_base Second range base.
                 * @param second_bytes Second range length.
                 * @return True when the ranges intersect.
                 */
                bool ranges_overlap(VirtAddr first_base, Size first_bytes, VirtAddr second_base, Size second_bytes) {
                    const VirtAddr first_end = first_base + first_bytes;
                    const VirtAddr second_end = second_base + second_bytes;

                    return (first_base < second_end) && (second_base < first_end);
                }

                /*
                 * Round one module base up to page granularity.
                 *
                 * Page-sized placement removes the old 2 MiB slot tax while still keeping
                 * every module mapping aligned to the current MMU page size.
                 *
                 * @param address Candidate module base.
                 * @return Page-aligned candidate base.
                 */
                VirtAddr align_up_module_base(VirtAddr address) {
                    const VirtAddr mask = mm::PageSize - 1U;

                    return (address + mask) & ~mask;
                }

                /*
                 * Report whether one module range collides with a live module in a process.
                 *
                 * @param process Process that would own the range.
                 * @param image_base Candidate base address.
                 * @param image_bytes Candidate length.
                 * @return True when the range overlaps one live module mapping.
                 */
                bool module_range_in_use(Process* process, VirtAddr image_base, Size image_bytes) {
                    bool in_use = false;
                    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

                    for (LoadedModule* module = g_module_head; module != NULL; module = module->next) {
                        if (module->owner_process != process) {
                            continue;
                        }
                        if (ranges_overlap(image_base, image_bytes, module->image_base, module->image_bytes)) {
                            in_use = true;
                            break;
                        }
                    }

                    arch::Arch::restore_interrupts(interrupts_enabled);
                    return in_use;
                }

                VirtAddr choose_base(Process* process, U64 preferred_base, Size image_bytes) {
                    VirtAddr candidate_base = align_up_module_base(static_cast<VirtAddr>(preferred_base));

                    if ((process == NULL) || (image_bytes == 0U)) {
                        return 0U;
                    }

                    if ((candidate_base >= user_address_space::ModuleRegionBase)
                        && ((candidate_base + image_bytes) <= user_address_space::ModuleRegionLimit)
                        && !module_range_in_use(process, candidate_base, image_bytes)) {
                        return candidate_base;
                    }

                    for (candidate_base = user_address_space::ModuleRegionBase;
                        (candidate_base + image_bytes) <= user_address_space::ModuleRegionLimit;
                        candidate_base += mm::PageSize) {
                        if (!module_range_in_use(process, candidate_base, image_bytes)) {
                            return candidate_base;
                        }
                    }

                    return 0U;
                }

                Status read_file_into_staging(const char* path, U8** file_bytes_out, Size* size_out) {
                    VfsNode node;
                    U8* file_bytes;
                    SSize read_result;
                    U64 poll_progress = 0U;
                    Status status;

                    if ((path == NULL) || (file_bytes_out == NULL) || (size_out == NULL)) {
                        return StatusInvalidArgument;
                    }

                    memzero(&node, sizeof(node));
                    status = VirtualFileSystem::resolve(path, &node);
                    if (status != StatusOK) {
                        loader_trace_path("[loader] resolve failed: ", path);
                        return status;
                    }
                    if (node.type != VfsNodeTypeFile) {
                        loader_trace("[loader] resolved node is not a file\n");
                        return StatusInvalidArgument;
                    }
                    if (node.size_bytes == 0U) {
                        loader_trace("[loader] file is empty\n");
                        return StatusInvalidArgument;
                    }

                    file_bytes = static_cast<U8*>(Heap::alloc(node.size_bytes, alignof(U8)));
                    if (file_bytes == NULL) {
                        loader_trace("[loader] file staging allocation failed\n");
                        return StatusNoMemory;
                    }

                    if ((kernel_debug_zone_mask() == 0U) || (node.size_bytes < VfsReadProgressMinimumBytes)) {
                        read_result = VirtualFileSystem::read(&node, 0U, file_bytes, node.size_bytes);
                        if (read_result < 0) {
                            loader_trace("[loader] read failed\n");
                            Heap::free(file_bytes);
                            return static_cast<Status>(read_result);
                        }
                        if (static_cast<U64>(read_result) != node.size_bytes) {
                            loader_trace("[loader] short read from filesystem\n");
                            Heap::free(file_bytes);
                            return StatusIoError;
                        }
                    }
                    else {
                        U64 total_read = 0U;
                        unsigned long next_progress = 20UL;
                        const Size progress_chunk = static_cast<Size>((node.size_bytes + 4U) / 5U);

                        while (total_read < node.size_bytes) {
                            const Size remaining = static_cast<Size>(node.size_bytes - total_read);
                            const Size chunk_bytes = (remaining < progress_chunk) ? remaining : progress_chunk;

                            read_result = VirtualFileSystem::read(&node, total_read, file_bytes + total_read, chunk_bytes);
                            if (read_result < 0) {
                                loader_trace("[loader] read failed\n");
                                Heap::free(file_bytes);
                                return static_cast<Status>(read_result);
                            }
                            if (read_result == 0) {
                                loader_trace("[loader] short read from filesystem\n");
                                Heap::free(file_bytes);
                                return StatusIoError;
                            }

                            total_read += static_cast<U64>(read_result);
                            loader_poll_after_progress(&poll_progress, static_cast<U64>(read_result), VfsReadProgressMinimumBytes);
                            while ((next_progress <= 100UL) && ((total_read * 100ULL) >= (node.size_bytes * next_progress))) {
                                KRETAIL(
                                    "[vfs] read %s %lu%% (%llu/%llu)\n",
                                    path,
                                    next_progress,
                                    static_cast<unsigned long long>(total_read),
                                    static_cast<unsigned long long>(node.size_bytes));
                                next_progress += 20UL;
                            }
                        }
                    }

                    *file_bytes_out = file_bytes;
                    *size_out = node.size_bytes;
                    return StatusOK;
                }

                /**
                 * Resolve one loader image path into a non-empty file node.
                 *
                 * Both the classic staged-read path and the direct-to-backing path need the
                 * same validation before reading bytes. Keeping that logic in one helper
                 * prevents those two loader strategies from drifting apart.
                 *
                 * @param path DOS-style loader path.
                 * @param node_out Receives the resolved file node.
                 * @return StatusOK on success, or the resolve/validation failure.
                 */
                Status resolve_loader_file_node(const char* path, VfsNode* node_out) {
                    Status status;

                    if ((path == NULL) || (node_out == NULL)) {
                        return StatusInvalidArgument;
                    }

                    memzero(node_out, sizeof(*node_out));
                    status = VirtualFileSystem::resolve(path, node_out);
                    if (status != StatusOK) {
                        loader_trace_path("[loader] resolve failed: ", path);
                        return status;
                    }
                    if (node_out->type != VfsNodeTypeFile) {
                        loader_trace("[loader] resolved node is not a file\n");
                        return StatusInvalidArgument;
                    }
                    if (node_out->size_bytes == 0U) {
                        loader_trace("[loader] file is empty\n");
                        return StatusInvalidArgument;
                    }

                    return StatusOK;
                }

                /**
                 * Read one exact byte range from a resolved loader file.
                 *
                 * Direct large-image population streams raw section bytes into the final
                 * image backing, so the loader needs an exact range reader that still emits
                 * cooperative polls during long filesystem transfers.
                 *
                 * @param node Resolved VFS file node.
                 * @param offset Starting byte offset inside the file.
                 * @param destination Caller-owned output buffer.
                 * @param size Exact byte count to read.
                 * @param poll_progress Optional running progress accumulator.
                 * @return StatusOK on success, or the read failure.
                 */
                Status read_resolved_file_range_exact(
                    const VfsNode* node,
                    U64 offset,
                    U8* destination,
                    Size size,
                    U64* poll_progress) {
                    U64 total_read = 0U;

                    if ((node == NULL) || ((destination == NULL) && (size != 0U))) {
                        return StatusInvalidArgument;
                    }
                    if (size == 0U) {
                        return StatusOK;
                    }

                    while (total_read < size) {
                        const Size remaining = static_cast<Size>(size - total_read);
                        const Size chunk_bytes = (remaining > VfsReadProgressMinimumBytes) ? VfsReadProgressMinimumBytes : remaining;
                        const SSize read_result = VirtualFileSystem::read(
                            node,
                            offset + total_read,
                            destination + total_read,
                            chunk_bytes);

                        if (read_result < 0) {
                            loader_trace("[loader] read failed\n");
                            return static_cast<Status>(read_result);
                        }
                        if (read_result == 0) {
                            loader_trace("[loader] short read from filesystem\n");
                            return StatusIoError;
                        }

                        total_read += static_cast<U64>(read_result);
                        if (poll_progress != NULL) {
                            loader_poll_after_progress(poll_progress, static_cast<U64>(read_result), VfsReadProgressMinimumBytes);
                        }
                    }

                    return StatusOK;
                }

                /**
                 * Read only the packed image header region into staging.
                 *
                 * The loader only needs the header tables to validate the image layout and
                 * discover section offsets. For larger DLL cache misses, loading just that
                 * prefix avoids a redundant whole-file staging read before section bytes are
                 * copied into the final image backing.
                 *
                 * @param node Resolved VFS file node.
                 * @param file_bytes_out Receives the allocated header buffer.
                 * @param header_bytes_out Receives the number of staged header bytes.
                 * @return StatusOK on success, or the read/validation failure.
                 */
                Status read_file_header_into_staging(const VfsNode* node, U8** file_bytes_out, Size* header_bytes_out) {
                    dll_header header = {};
                    U8* file_bytes;
                    Size header_bytes;
                    Status status;

                    if ((node == NULL) || (file_bytes_out == NULL) || (header_bytes_out == NULL)) {
                        return StatusInvalidArgument;
                    }
                    if (node->size_bytes < sizeof(dll_header)) {
                        loader_trace("[loader] file is too small for image header\n");
                        return StatusInvalidArgument;
                    }

                    status = read_resolved_file_range_exact(node, 0U, reinterpret_cast<U8*>(&header), sizeof(header), NULL);
                    if (status != StatusOK) {
                        return status;
                    }

                    header_bytes = header.header_size;
                    if ((header_bytes < sizeof(dll_header)) || (header_bytes > node->size_bytes)) {
                        loader_trace("[loader] invalid packed image header size\n");
                        return StatusInvalidArgument;
                    }

                    file_bytes = static_cast<U8*>(Heap::alloc(header_bytes, alignof(U8)));
                    if (file_bytes == NULL) {
                        loader_trace("[loader] file staging allocation failed\n");
                        return StatusNoMemory;
                    }

                    status = read_resolved_file_range_exact(node, 0U, file_bytes, header_bytes, NULL);
                    if (status != StatusOK) {
                        Heap::free(file_bytes);
                        return status;
                    }

                    *file_bytes_out = file_bytes;
                    *header_bytes_out = header_bytes;
                    return StatusOK;
                }

                Status parse_image_view(const U8* file_bytes, Size file_size, ParsedImageView* view_out) {
                    ParsedImageView view = {};
                    const dll_header* header;

                    if ((file_bytes == NULL) || (view_out == NULL) || (file_size < sizeof(dll_header))) {
                        return StatusInvalidArgument;
                    }

                    header = reinterpret_cast<const dll_header*>(file_bytes);
                    if (header->magic != DLL_IMAGE_MAGIC) {
                        loader_trace("[loader] invalid image magic\n");
                        return StatusInvalidArgument;
                    }
                    if ((header->version_major != DLL_IMAGE_VERSION_MAJOR) || (header->machine != DLL_MACHINE_AARCH64)) {
                        loader_trace("[loader] unsupported image version or machine\n");
                        return StatusNotSupported;
                    }
                    if ((header->image_size == 0U) || (header->image_size > (user_address_space::ModuleRegionLimit - user_address_space::ModuleRegionBase))) {
                        loader_trace("[loader] unsupported image size\n");
                        return StatusNotSupported;
                    }
                    if ((header->header_size < sizeof(dll_header)) || (header->header_size > file_size) || (header->header_size > header->image_size)) {
                        loader_trace("[loader] invalid image header size\n");
                        return StatusInvalidArgument;
                    }

                    view.file_bytes = file_bytes;
                    view.file_size = file_size;
                    view.header = header;

                    if (add_range_overflows(header->section_table_offset, static_cast<U64>(header->section_count) * sizeof(dll_section), header->header_size)) {
                        return StatusInvalidArgument;
                    }
                    if (add_range_overflows(header->import_module_table_offset, static_cast<U64>(header->import_module_count) * sizeof(dll_import_module), header->header_size)) {
                        return StatusInvalidArgument;
                    }
                    if (add_range_overflows(header->import_symbol_table_offset, static_cast<U64>(header->import_symbol_count) * sizeof(dll_import_symbol), header->header_size)) {
                        return StatusInvalidArgument;
                    }
                    if (add_range_overflows(header->export_table_offset, static_cast<U64>(header->export_count) * sizeof(dll_export_symbol), header->header_size)) {
                        return StatusInvalidArgument;
                    }
                    if (add_range_overflows(header->relocation_table_offset, static_cast<U64>(header->relocation_count) * sizeof(dll_relocation), header->header_size)) {
                        return StatusInvalidArgument;
                    }
                    if (add_range_overflows(header->string_table_offset, header->string_table_size, header->header_size)) {
                        return StatusInvalidArgument;
                    }

                    view.sections = reinterpret_cast<const dll_section*>(file_bytes + header->section_table_offset);
                    view.import_modules = reinterpret_cast<const dll_import_module*>(file_bytes + header->import_module_table_offset);
                    view.import_symbols = reinterpret_cast<const dll_import_symbol*>(file_bytes + header->import_symbol_table_offset);
                    view.exports = reinterpret_cast<const dll_export_symbol*>(file_bytes + header->export_table_offset);
                    view.relocations = reinterpret_cast<const dll_relocation*>(file_bytes + header->relocation_table_offset);
                    view.string_table = reinterpret_cast<const char*>(file_bytes + header->string_table_offset);

                    for (U32 section_index = 0U; section_index < header->section_count; ++section_index) {
                        const dll_section* section = &view.sections[section_index];

                        if (section->raw_data_size > section->virtual_size) {
                            return StatusInvalidArgument;
                        }
                        if (add_range_overflows(section->virtual_address, section->virtual_size, header->image_size)) {
                            return StatusInvalidArgument;
                        }
                        if ((section->flags & DLL_SEC_BSS) == 0U) {
                            if (add_range_overflows(section->raw_data_offset, section->raw_data_size, file_size)) {
                                return StatusInvalidArgument;
                            }
                        }
                    }

                    for (U32 module_index = 0U; module_index < header->import_module_count; ++module_index) {
                        const dll_import_module* import_module = &view.import_modules[module_index];

                        if (view_string_at(&view, import_module->module_name_offset) == NULL) {
                            return StatusInvalidArgument;
                        }
                        if ((import_module->first_symbol_index > header->import_symbol_count)
                            || (import_module->symbol_count > (header->import_symbol_count - import_module->first_symbol_index))) {
                            return StatusInvalidArgument;
                        }
                    }

                    for (U32 symbol_index = 0U; symbol_index < header->import_symbol_count; ++symbol_index) {
                        const dll_import_symbol* import_symbol = &view.import_symbols[symbol_index];

                        if (view_string_at(&view, import_symbol->symbol_name_offset) == NULL) {
                            return StatusInvalidArgument;
                        }
                        if (add_range_overflows(import_symbol->iat_rva, sizeof(U64), header->image_size)) {
                            return StatusInvalidArgument;
                        }
                    }

                    for (U32 export_index = 0U; export_index < header->export_count; ++export_index) {
                        const dll_export_symbol* export_symbol = &view.exports[export_index];

                        if (view_string_at(&view, export_symbol->symbol_name_offset) == NULL) {
                            return StatusInvalidArgument;
                        }
                        if (export_symbol->symbol_rva >= header->image_size) {
                            return StatusInvalidArgument;
                        }
                    }

                    for (U32 relocation_index = 0U; relocation_index < header->relocation_count; ++relocation_index) {
                        const dll_relocation* relocation = &view.relocations[relocation_index];

                        if (add_range_overflows(relocation->target_rva, sizeof(U64), header->image_size)) {
                            return StatusInvalidArgument;
                        }
                    }

                    *view_out = view;
                    return StatusOK;
                }

                Status copy_image_to_backing(const ParsedImageView* view, U8* image_memory, Size capacity) {
                    U64 poll_progress = 0U;

                    if ((view == NULL) || (view->header == NULL) || (image_memory == NULL)) {
                        return StatusInvalidArgument;
                    }
                    if (view->header->image_size > capacity) {
                        loader_trace("[loader] image backing is too small\n");
                        return StatusNotSupported;
                    }

                    memzero(image_memory, capacity);
                    memcopy(image_memory, view->file_bytes, view->header->header_size);
                    loader_poll_after_progress(&poll_progress, view->header->header_size, VfsReadProgressMinimumBytes);

                    for (U32 section_index = 0U; section_index < view->header->section_count; ++section_index) {
                        const dll_section* section = &view->sections[section_index];
                        U8* destination = image_memory + section->virtual_address;

                        if ((section->flags & DLL_SEC_BSS) != 0U) {
                            memzero(destination, section->virtual_size);
                            loader_poll_after_progress(&poll_progress, section->virtual_size, VfsReadProgressMinimumBytes);
                            continue;
                        }

                        if (section->raw_data_size != 0U) {
                            memcopy(destination, view->file_bytes + section->raw_data_offset, section->raw_data_size);
                            loader_poll_after_progress(&poll_progress, section->raw_data_size, VfsReadProgressMinimumBytes);
                        }
                        if (section->virtual_size > section->raw_data_size) {
                            memzero(destination + section->raw_data_size, section->virtual_size - section->raw_data_size);
                            loader_poll_after_progress(
                                &poll_progress,
                                static_cast<U64>(section->virtual_size - section->raw_data_size),
                                VfsReadProgressMinimumBytes);
                        }
                    }

                    return StatusOK;
                }

                /**
                 * Populate one image backing directly from the source VFS file.
                 *
                 * When a large DLL was parsed from header-only staging, the raw section
                 * bytes are not resident in memory. Reading those sections directly into the
                 * final image backing removes the second full-image memcpy from the cold-load
                 * path without changing relocation or import-resolution behavior.
                 *
                 * @param path Normalized image path.
                 * @param view Parsed header view.
                 * @param image_memory Destination image backing.
                 * @param capacity Size of the destination backing.
                 * @return StatusOK on success, or the read/validation failure.
                 */
                Status populate_image_backing_from_file(const char* path, const ParsedImageView* view, U8* image_memory, Size capacity) {
                    VfsNode node;
                    U64 poll_progress = 0U;
                    Status status;

                    if ((path == NULL) || (view == NULL) || (view->header == NULL) || (image_memory == NULL)) {
                        return StatusInvalidArgument;
                    }
                    if (view->header->image_size > capacity) {
                        loader_trace("[loader] image backing is too small\n");
                        return StatusNotSupported;
                    }

                    status = resolve_loader_file_node(path, &node);
                    if (status != StatusOK) {
                        return status;
                    }

                    memzero(image_memory, capacity);
                    memcopy(image_memory, view->file_bytes, view->header->header_size);
                    loader_poll_after_progress(&poll_progress, view->header->header_size, VfsReadProgressMinimumBytes);

                    for (U32 section_index = 0U; section_index < view->header->section_count; ++section_index) {
                        const dll_section* section = &view->sections[section_index];
                        U8* destination = image_memory + section->virtual_address;

                        if ((section->flags & DLL_SEC_BSS) != 0U) {
                            memzero(destination, section->virtual_size);
                            loader_poll_after_progress(&poll_progress, section->virtual_size, VfsReadProgressMinimumBytes);
                            continue;
                        }

                        if (section->raw_data_size != 0U) {
                            status = read_resolved_file_range_exact(
                                &node,
                                section->raw_data_offset,
                                destination,
                                section->raw_data_size,
                                &poll_progress);
                            if (status != StatusOK) {
                                return status;
                            }
                        }
                        if (section->virtual_size > section->raw_data_size) {
                            memzero(destination + section->raw_data_size, section->virtual_size - section->raw_data_size);
                            loader_poll_after_progress(
                                &poll_progress,
                                static_cast<U64>(section->virtual_size - section->raw_data_size),
                                VfsReadProgressMinimumBytes);
                        }
                    }

                    return StatusOK;
                }

                Status apply_image_relocations(const ParsedImageView* view, U8* image_memory, Size capacity, VirtAddr actual_base) {
                    I64 base_delta;

                    if ((view == NULL) || (view->header == NULL) || (image_memory == NULL)) {
                        return StatusInvalidArgument;
                    }
                    if (view->header->image_size > capacity) {
                        loader_trace("[loader] relocation backing is too small\n");
                        return StatusNotSupported;
                    }

                    base_delta = static_cast<I64>(actual_base) - static_cast<I64>(view->header->image_base);
                    if (base_delta == 0) {
                        return StatusOK;
                    }

                    for (U32 relocation_index = 0U; relocation_index < view->header->relocation_count; ++relocation_index) {
                        const dll_relocation* relocation = &view->relocations[relocation_index];
                        U8* patch_bytes = image_memory + relocation->target_rva;

                        if ((relocation_index != 0U) && ((relocation_index % LoaderRelocationPollStride) == 0U)) {
                            loader_cooperative_poll();
                        }

                        if (relocation->type == DLL_RELOC_ABS64) {
                            *reinterpret_cast<U64*>(patch_bytes) = static_cast<U64>(static_cast<I64>(*reinterpret_cast<U64*>(patch_bytes)) + base_delta);
                            continue;
                        }
                        if (relocation->type == DLL_RELOC_ABS32) {
                            *reinterpret_cast<U32*>(patch_bytes) = static_cast<U32>(static_cast<I32>(*reinterpret_cast<U32*>(patch_bytes)) + static_cast<I32>(base_delta));
                            continue;
                        }

                        loader_trace("[loader] unsupported relocation type\n");
                        return StatusNotSupported;
                    }

                    return StatusOK;
                }

                Status lookup_export_address(const LoadedModule* module, const char* export_name, VirtAddr* export_address_out) {
                    const dll_header* header;
                    const dll_export_symbol* exports;

                    if ((module == NULL) || (export_name == NULL) || (export_address_out == NULL)) {
                        return StatusInvalidArgument;
                    }

                    header = header_of(module);
                    exports = exports_of(module);
                    if ((header == NULL) || (exports == NULL)) {
                        return StatusFault;
                    }

                    for (U32 export_index = 0U; export_index < header->export_count; ++export_index) {
                        const char* candidate_name = string_at(module, exports[export_index].symbol_name_offset);

                        if ((candidate_name != NULL) && same_text_case_insensitive(candidate_name, export_name)) {
                            *export_address_out = module->image_base + exports[export_index].symbol_rva;
                            return StatusOK;
                        }
                    }

                    return StatusNotFound;
                }

                Status map_module(Process* process, LoadedModule* module) {
                    if ((process == NULL) || (module == NULL) || (header_of(module) == NULL)) {
                        return StatusInvalidArgument;
                    }

                    return map_image_with_private_pages(process, module->image_base, module->backing, module->private_pages);
                }

                Status release_dependencies_on_failure(const HeapList<LoadedModule*>& dependencies) {
                    Status status = StatusOK;

                    for (Size index = 0U; index < dependencies.count(); ++index) {
                        if (dependencies[index] == NULL) {
                            continue;
                        }

                        status = KernelObjectManager::dereference_object(&dependencies[index]->header);
                        if (status != StatusOK) {
                            return status;
                        }
                    }

                    return StatusOK;
                }

                Status ensure_library_loaded(Process* process, const char* module_path, bool explicit_open, LoadedModule** module_out, bool* needs_process_attach_out);
                Status destroy_module(LoadedModule* module, bool process_address_space_alive);

                Status resolve_image_imports(
                    Process* process,
                    const ParsedImageView* view,
                    U8* image_memory,
                    HeapList<LoadedModule*>* imported_modules) {
                    const dll_header* resident_header;
                    const dll_import_module* resident_import_modules;
                    const dll_import_symbol* resident_import_symbols;
                    const char* resident_string_table;

                    if ((process == NULL) || (view == NULL) || (image_memory == NULL)) {
                        return StatusInvalidArgument;
                    }

                    resident_header = reinterpret_cast<const dll_header*>(image_memory);
                    resident_import_modules = reinterpret_cast<const dll_import_module*>(image_memory + resident_header->import_module_table_offset);
                    resident_import_symbols = reinterpret_cast<const dll_import_symbol*>(image_memory + resident_header->import_symbol_table_offset);
                    resident_string_table = reinterpret_cast<const char*>(image_memory + resident_header->string_table_offset);

                    for (U32 module_index = 0U; module_index < resident_header->import_module_count; ++module_index) {
                        const dll_import_module* import_module = &resident_import_modules[module_index];
                        const char* import_module_name = bounded_string_at(resident_string_table, resident_header->string_table_size, import_module->module_name_offset);
                        LoadedModule* dependency = NULL;
                        Status status;

                        loader_cooperative_poll();

                        status = ensure_library_loaded(process, import_module_name, false, &dependency, NULL);
                        if (status != StatusOK) {
                            KERROR(
                                "[loader] import module load failed module=%s status=%d\n",
                                (import_module_name != NULL) ? import_module_name : "<null>",
                                static_cast<int>(status));
                            if (imported_modules != NULL) {
                                (void)release_dependencies_on_failure(*imported_modules);
                            }
                            return status;
                        }

                        if (imported_modules != NULL) {
                            bool known_dependency = false;

                            for (Size dependency_index = 0U; dependency_index < imported_modules->count(); ++dependency_index) {
                                if ((*imported_modules)[dependency_index] == dependency) {
                                    known_dependency = true;
                                    break;
                                }
                            }

                            if (!known_dependency) {
                                status = imported_modules->append(dependency);
                                if (status != StatusOK) {
                                    (void)KernelObjectManager::dereference_object(&dependency->header);
                                    (void)release_dependencies_on_failure(*imported_modules);
                                    return status;
                                }
                            }
                        }

                        for (U32 symbol_offset = 0U; symbol_offset < import_module->symbol_count; ++symbol_offset) {
                            const dll_import_symbol* import_symbol = &resident_import_symbols[import_module->first_symbol_index + symbol_offset];
                            const char* import_symbol_name = bounded_string_at(resident_string_table, resident_header->string_table_size, import_symbol->symbol_name_offset);
                            VirtAddr export_address = 0U;

                            if ((symbol_offset != 0U) && ((symbol_offset % LoaderImportPollStride) == 0U)) {
                                loader_cooperative_poll();
                            }

                            status = lookup_export_address(dependency, import_symbol_name, &export_address);
                            if (status != StatusOK) {
                                KERROR(
                                    "[loader] import symbol resolve failed module=%s symbol=%s status=%d\n",
                                    (import_module_name != NULL) ? import_module_name : "<null>",
                                    (import_symbol_name != NULL) ? import_symbol_name : "<null>",
                                    static_cast<int>(status));
                                if (imported_modules != NULL) {
                                    (void)release_dependencies_on_failure(*imported_modules);
                                }
                                return status;
                            }

                            *reinterpret_cast<U64*>(image_memory + import_symbol->iat_rva) = export_address;
                        }
                    }

                    return StatusOK;
                }

                /*
                 * Ensure that every imported module for one resident shared backing is
                 * present in the target process.
                 *
                 * Shared images already carry a fully patched IAT, so reuse only needs the
                 * dependent modules mapped into the new process at their canonical shared
                 * bases.
                 *
                 * @param process Target process receiving the shared image mapping.
                 * @param backing Canonical shared loaded-image backing.
                 * @param imported_modules Optional temporary dependency ref list.
                 * @return StatusOK on success, or the dependency load failure.
                 */
                Status ensure_backing_dependencies_loaded(
                    Process* process,
                    const void* backing,
                    HeapList<LoadedModule*>* imported_modules) {
                    const dll_header* header;
                    const dll_import_module* import_modules;
                    const char* string_table;

                    if ((process == NULL) || (backing == NULL)) {
                        return StatusInvalidArgument;
                    }

                    header = header_of_backing(backing);
                    import_modules = imports_of_backing(backing);
                    string_table = string_table_of_backing(backing);
                    if ((header == NULL) || (import_modules == NULL) || (string_table == NULL)) {
                        return StatusFault;
                    }

                    for (U32 module_index = 0U; module_index < header->import_module_count; ++module_index) {
                        const dll_import_module* import_module = &import_modules[module_index];
                        const char* import_module_name = bounded_string_at(string_table, header->string_table_size, import_module->module_name_offset);
                        LoadedModule* dependency = NULL;
                        Status status;

                        loader_cooperative_poll();

                        status = ensure_library_loaded(process, import_module_name, false, &dependency, NULL);
                        if (status != StatusOK) {
                            if (imported_modules != NULL) {
                                (void)release_dependencies_on_failure(*imported_modules);
                            }
                            return status;
                        }

                        if (imported_modules != NULL) {
                            bool known_dependency = false;

                            for (Size dependency_index = 0U; dependency_index < imported_modules->count(); ++dependency_index) {
                                if ((*imported_modules)[dependency_index] == dependency) {
                                    known_dependency = true;
                                    break;
                                }
                            }

                            if (!known_dependency) {
                                status = imported_modules->append(dependency);
                                if (status != StatusOK) {
                                    (void)KernelObjectManager::dereference_object(&dependency->header);
                                    (void)release_dependencies_on_failure(*imported_modules);
                                    return status;
                                }
                            }
                        }
                    }

                    return StatusOK;
                }

                Status create_loaded_module(
                    Process* process,
                    const char* module_path,
                    void* backing,
                    Size image_bytes,
                    VirtAddr image_base,
                    SharedLoadedImage* shared_image,
                    LoadedModule** module_out) {
                    LoadedModule* module;
                    const char* active_path;
                    const char* active_name;
                    Status status;

                    if ((process == NULL) || (module_out == NULL)) {
                        return StatusInvalidArgument;
                    }
                    if ((shared_image == NULL) && ((module_path == NULL) || (backing == NULL))) {
                        return StatusInvalidArgument;
                    }

                    active_path = (shared_image != NULL) ? shared_image->path : module_path;
                    active_name = (shared_image != NULL) ? shared_image->module_name : path_basename(module_path);

                    // 2026-04-14: keep LoadedModule metadata in the loader's page-backed pool
                    // so long-lived process/module bookkeeping stops consuming general heap space.
                    module = allocate_loaded_module_record(process->id, active_path);
                    if (module == NULL) {
                        if (shared_image != NULL) {
                            release_shared_image(shared_image);
                        }
                        return StatusNoMemory;
                    }

                    memzero(module, sizeof(*module));
                    module->header.type = ObjectType::Module;
                    module->header.flags = ObjectFlags::None;
                    module->header.object_id = g_next_module_id++;
                    module->header.ref_count = 1U;
                    module->owner_process = process;
                    module->shared_image = shared_image;
                    module->backing = (shared_image != NULL) ? shared_image->backing : backing;
                    module->image_bytes = (shared_image != NULL) ? shared_image->image_bytes : image_bytes;
                    module->image_base = image_base;
                    module->pending_process_attach = true;
                    module->pending_process_detach = false;
                    if ((store_loader_text(module->path, sizeof(module->path), active_path, "C:/lib/unknown.dll") != StatusOK)
                        || (store_loader_text(module->module_name, sizeof(module->module_name), active_name, "unknown.dll") != StatusOK)) {
                        if (shared_image != NULL) {
                            release_shared_image(shared_image);
                        }
                        free_loaded_module_record(module);
                        return StatusNoSpace;
                    }

                    if (shared_image != NULL) {
                        status = build_process_private_image_pages(
                            process,
                            module->backing,
                            shared_image->image_base,
                            module->image_base,
                            true,
                            process->id,
                            active_path,
                            &module->private_pages,
                            &module->private_page_count);
                        if (status != StatusOK) {
                            release_shared_image(shared_image);
                            free_loaded_module_record(module);
                            return status;
                        }
                    }

                    status = KernelObjectManager::register_object(&module->header);
                    if (status != StatusOK) {
                        free_private_image_pages(module->private_pages);
                        module->private_pages = NULL;
                        module->private_page_count = 0U;
                        if (shared_image != NULL) {
                            release_shared_image(shared_image);
                        }
                        free_loaded_module_record(module);
                        return status;
                    }

                    refresh_module_snapshot(module);
                    link_module(module);
                    *module_out = module;
                    return StatusOK;
                }

                /*
                 * Load an executable or DLL image file into a parsed view.
                 *
                 * The loader first consults the global cached-image table so repeated loads
                 * can reuse one shared immutable file buffer. Misses fall back to a normal
                 * staging read and may be inserted into the cache when the image size fits
                 * the cache budget.
                 *
                 * @param image_path DOS-style image path.
                 * @param view_out Parsed image view output.
                 * @return Status code from the load or parse operation.
                 */
                Status load_image_file(const char* image_path, ParsedImageView* view_out) {
                    U8* image_bytes = NULL;
                    Size image_size = 0U;
                    CachedImageObject* cached_object = NULL;
                    char normalized_path[VFS_PATH_CAPACITY];
                    VfsNode node;
                    U64 now_msec = image_cache_now_msec();
                    U64 stage_start_msec = now_msec;
                    U64 normalize_msec = 0U;
                    U64 cache_lookup_msec = 0U;
                    U64 read_msec = 0U;
                    U64 parse_msec = 0U;
                    U64 cache_insert_msec = 0U;
                    bool normalized_ok = false;
                    Status status;

                    if ((image_path == NULL) || (view_out == NULL)) {
                        return StatusInvalidArgument;
                    }

                    status = normalize_path(image_path, normalized_path, sizeof(normalized_path));
                    normalize_msec = loader_elapsed_msec(stage_start_msec);
                    if (status == StatusOK) {
                        normalized_ok = true;

                        stage_start_msec = image_cache_now_msec();
                        cached_object = cached_image_find(normalized_path, now_msec);
                        cache_lookup_msec = loader_elapsed_msec(stage_start_msec);
                        if (cached_object != NULL) {
                            stage_start_msec = image_cache_now_msec();
                            status = parse_image_view(cached_object->file_bytes, cached_object->file_size, view_out);
                            parse_msec = loader_elapsed_msec(stage_start_msec);
                            if (status == StatusOK) {
                                view_out->cache_object = cached_object;
                                KRETAIL(
                                    "[loader-prof] image-file path=%s cache=hit bytes=%llu normalize_ms=%llu lookup_ms=%llu parse_ms=%llu total_ms=%llu\n",
                                    normalized_path,
                                    static_cast<unsigned long long>(cached_object->file_size),
                                    static_cast<unsigned long long>(normalize_msec),
                                    static_cast<unsigned long long>(cache_lookup_msec),
                                    static_cast<unsigned long long>(parse_msec),
                                    static_cast<unsigned long long>(loader_elapsed_msec(now_msec)));
                                return StatusOK;
                            }

                            cached_image_release(cached_object);
                            return status;
                        }
                    }

                    status = resolve_loader_file_node(image_path, &node);
                    if (status != StatusOK) {
                        return status;
                    }

                    /*
                     * Validate every uncached image from a header-only staging buffer.
                     *
                     * The packed header already contains the section table, imports,
                     * exports, relocations, and string table ranges needed for structural
                     * validation. Keeping only those bytes resident avoids the older
                     * small-image path that staged the whole file and then copied it into
                     * the final backing a second time.
                     *
                     * @param node Resolved file node used for the staged header read.
                     * @param image_bytes Receives the private header staging buffer.
                     * @param image_size Receives the staged header byte count.
                     * @return Continues through the direct-population path on success.
                     */
                    stage_start_msec = image_cache_now_msec();
                    status = read_file_header_into_staging(&node, &image_bytes, &image_size);
                    read_msec = loader_elapsed_msec(stage_start_msec);
                    if (status != StatusOK) {
                        return status;
                    }

                    stage_start_msec = image_cache_now_msec();
                    status = parse_image_view(image_bytes, node.size_bytes, view_out);
                    parse_msec = loader_elapsed_msec(stage_start_msec);
                    if (status != StatusOK) {
                        Heap::free(image_bytes);
                        return status;
                    }

                    view_out->direct_file_population = true;

                    KRETAIL(
                        "[loader-prof] image-file path=%s cache=direct bytes=%llu normalize_ms=%llu lookup_ms=%llu read_ms=%llu parse_ms=%llu cache_insert_ms=%llu total_ms=%llu\n",
                        normalized_ok ? normalized_path : image_path,
                        static_cast<unsigned long long>(node.size_bytes),
                        static_cast<unsigned long long>(normalize_msec),
                        static_cast<unsigned long long>(cache_lookup_msec),
                        static_cast<unsigned long long>(read_msec),
                        static_cast<unsigned long long>(parse_msec),
                        static_cast<unsigned long long>(cache_insert_msec),
                        static_cast<unsigned long long>(loader_elapsed_msec(now_msec)));

                    return StatusOK;
                }

                Status load_new_module(
                    Process* process,
                    const char* module_path,
                    bool explicit_open,
                    LoadedModule** module_out,
                    bool* needs_process_attach_out) {
                    ParsedImageView view = {};
                    SharedLoadedImage* shared_image = NULL;
                    U8* backing = NULL;
                    VirtAddr image_base = 0U;
                    U64 preferred_base;
                    U16 image_type = 0U;
                    char normalized_module_path[VFS_PATH_CAPACITY];
                    LoadedModule* module = NULL;
                    HeapList<LoadedModule*> dependencies;
                    U64 total_start_msec = image_cache_now_msec();
                    U64 stage_start_msec = total_start_msec;
                    U64 shared_dependencies_msec = 0U;
                    U64 image_file_msec = 0U;
                    U64 copy_msec = 0U;
                    U64 import_msec = 0U;
                    U64 choose_base_msec = 0U;
                    U64 relocate_msec = 0U;
                    U64 shared_record_msec = 0U;
                    U64 create_module_msec = 0U;
                    U64 map_module_msec = 0U;
                    Status status;

                    if ((process == NULL) || (module_path == NULL) || (module_out == NULL)) {
                        return StatusInvalidArgument;
                    }

                    status = normalize_path(module_path, normalized_module_path, sizeof(normalized_module_path));
                    if (status != StatusOK) {
                        return status;
                    }

                    shared_image = find_shared_image_by_path(normalized_module_path);
                    if (shared_image != NULL) {
                        if ((shared_image->image_type != DLL_IMAGE_TYPE_DLL) && (shared_image->image_type != DLL_IMAGE_TYPE_DRIVER)) {
                            return StatusFault;
                        }

                        retain_shared_image(shared_image);
                        stage_start_msec = image_cache_now_msec();
                        status = ensure_backing_dependencies_loaded(process, shared_image->backing, &dependencies);
                        shared_dependencies_msec = loader_elapsed_msec(stage_start_msec);
                        if (status != StatusOK) {
                            dependencies.clear();
                            release_shared_image(shared_image);
                            return status;
                        }

                        stage_start_msec = image_cache_now_msec();
                        image_base = choose_base(process, shared_image->image_base, shared_image->image_bytes);
                        choose_base_msec = loader_elapsed_msec(stage_start_msec);
                        if (image_base == 0U) {
                            (void)release_dependencies_on_failure(dependencies);
                            dependencies.clear();
                            release_shared_image(shared_image);
                            return StatusNoSpace;
                        }

                        stage_start_msec = image_cache_now_msec();
                        status = create_loaded_module(
                            process,
                            normalized_module_path,
                            shared_image->backing,
                            shared_image->image_bytes,
                            image_base,
                            shared_image,
                            &module);
                        create_module_msec = loader_elapsed_msec(stage_start_msec);
                        if (status != StatusOK) {
                            (void)release_dependencies_on_failure(dependencies);
                            dependencies.clear();
                            return status;
                        }

                        stage_start_msec = image_cache_now_msec();
                        status = map_module(process, module);
                        map_module_msec = loader_elapsed_msec(stage_start_msec);
                        if (status != StatusOK) {
                            (void)destroy_module(module, false);
                            (void)release_dependencies_on_failure(dependencies);
                            dependencies.clear();
                            return status;
                        }
                        dependencies.clear();

                        if (!explicit_open) {
                            if (needs_process_attach_out != NULL) {
                                *needs_process_attach_out = false;
                            }
                        }
                        else if (needs_process_attach_out != NULL) {
                            module->pending_process_attach = false;
                            refresh_module_snapshot(module);
                            *needs_process_attach_out = true;
                        }

                        *module_out = module;
                        KRETAIL(
                            "[loader-prof] module path=%s source=shared deps_ms=%llu choose_base_ms=%llu create_ms=%llu map_ms=%llu total_ms=%llu\n",
                            normalized_module_path,
                            static_cast<unsigned long long>(shared_dependencies_msec),
                            static_cast<unsigned long long>(choose_base_msec),
                            static_cast<unsigned long long>(create_module_msec),
                            static_cast<unsigned long long>(map_module_msec),
                            static_cast<unsigned long long>(loader_elapsed_msec(total_start_msec)));
                        return StatusOK;
                    }

                    stage_start_msec = image_cache_now_msec();
                    status = load_image_file(normalized_module_path, &view);
                    image_file_msec = loader_elapsed_msec(stage_start_msec);
                    if (status != StatusOK) {
                        return status;
                    }
                    if ((view.header->image_type != DLL_IMAGE_TYPE_DLL) && (view.header->image_type != DLL_IMAGE_TYPE_DRIVER)) {
                        release_view(&view);
                        return StatusInvalidArgument;
                    }

                    image_type = view.header->image_type;
                    preferred_base = view.header->image_base;
                    if (preferred_base == 0U) {
                        preferred_base = (image_type == DLL_IMAGE_TYPE_DRIVER) ? DefaultDriverPreferredBase : DefaultDllPreferredBase;
                    }

                    const Size image_bytes = align_up_image_bytes(view.header->image_size);

                    backing = allocate_loader_backing(image_bytes);
                    if (backing == NULL) {
                        loader_trace_path("[loader] module backing allocation failed: ", normalized_module_path);
                        release_view(&view);
                        return StatusNoMemory;
                    }
                    KRETAIL("[loader] module backing path=%s backing=%llx bytes=%llu\n", normalized_module_path, static_cast<unsigned long long>(reinterpret_cast<Uptr>(backing)), static_cast<unsigned long long>(image_bytes));

                    stage_start_msec = image_cache_now_msec();
                    if (view.direct_file_population) {
                        status = populate_image_backing_from_file(normalized_module_path, &view, backing, image_bytes);
                    }
                    else {
                        status = copy_image_to_backing(&view, backing, image_bytes);
                    }
                    copy_msec = loader_elapsed_msec(stage_start_msec);
                    if (status == StatusOK) {
                        stage_start_msec = image_cache_now_msec();
                        status = ensure_backing_dependencies_loaded(process, backing, &dependencies);
                        import_msec = loader_elapsed_msec(stage_start_msec);
                    }
                    if (status == StatusOK) {
                        stage_start_msec = image_cache_now_msec();
                        image_base = choose_base(process, preferred_base, image_bytes);
                        choose_base_msec = loader_elapsed_msec(stage_start_msec);
                        if (image_base == 0U) {
                            status = StatusNoSpace;
                        }
                    }
                    if (status == StatusOK) {
                        stage_start_msec = image_cache_now_msec();
                        status = apply_image_relocations(&view, backing, image_bytes, preferred_base);
                        relocate_msec = loader_elapsed_msec(stage_start_msec);
                    }
                    if (status == StatusOK) {
                        trace_image_section_layout(
                            normalized_module_path,
                            &view,
                            image_base,
                            "dll-page-segments");
                    }
                    release_view(&view);
                    if (status != StatusOK) {
                        dependencies.clear();
                        free_loader_backing(backing, image_bytes);
                        return status;
                    }

                    stage_start_msec = image_cache_now_msec();
                    status = create_shared_image_record(
                        normalized_module_path,
                        backing,
                        image_bytes,
                        image_type,
                        preferred_base,
                        &shared_image);
                    shared_record_msec = loader_elapsed_msec(stage_start_msec);
                    if (status != StatusOK) {
                        (void)release_dependencies_on_failure(dependencies);
                        dependencies.clear();
                        free_loader_backing(backing, image_bytes);
                        return status;
                    }

                    stage_start_msec = image_cache_now_msec();
                    status = create_loaded_module(
                        process,
                        normalized_module_path,
                        backing,
                        image_bytes,
                        image_base,
                        shared_image,
                        &module);
                    create_module_msec = loader_elapsed_msec(stage_start_msec);
                    if (status != StatusOK) {
                        (void)release_dependencies_on_failure(dependencies);
                        dependencies.clear();
                        return status;
                    }

                    stage_start_msec = image_cache_now_msec();
                    status = map_module(process, module);
                    map_module_msec = loader_elapsed_msec(stage_start_msec);
                    if (status != StatusOK) {
                        (void)destroy_module(module, false);
                        (void)release_dependencies_on_failure(dependencies);
                        dependencies.clear();
                        return status;
                    }
                    dependencies.clear();

                    if (!explicit_open) {
                        if (needs_process_attach_out != NULL) {
                            *needs_process_attach_out = false;
                        }
                    }
                    else if (needs_process_attach_out != NULL) {
                        module->pending_process_attach = false;
                        refresh_module_snapshot(module);
                        *needs_process_attach_out = true;
                    }

                    *module_out = module;
                    KRETAIL(
                        "[loader-prof] module path=%s source=file image_ms=%llu copy_ms=%llu imports_ms=%llu choose_base_ms=%llu relocate_ms=%llu share_ms=%llu create_ms=%llu map_ms=%llu total_ms=%llu\n",
                        normalized_module_path,
                        static_cast<unsigned long long>(image_file_msec),
                        static_cast<unsigned long long>(copy_msec),
                        static_cast<unsigned long long>(import_msec),
                        static_cast<unsigned long long>(choose_base_msec),
                        static_cast<unsigned long long>(relocate_msec),
                        static_cast<unsigned long long>(shared_record_msec),
                        static_cast<unsigned long long>(create_module_msec),
                        static_cast<unsigned long long>(map_module_msec),
                        static_cast<unsigned long long>(loader_elapsed_msec(total_start_msec)));
                    return StatusOK;
                }

                Status ensure_library_loaded(Process* process, const char* module_path, bool explicit_open, LoadedModule** module_out, bool* needs_process_attach_out) {
                    LoadedModule* module;
                    Status status;

                    if ((process == NULL) || (module_path == NULL) || (module_out == NULL)) {
                        return StatusInvalidArgument;
                    }

                    module = find_module_by_path(process, module_path);
                    if (module != NULL) {
                        if (explicit_open) {
                            if (module->pending_process_attach) {
                                module->pending_process_attach = false;
                                refresh_module_snapshot(module);
                                if (needs_process_attach_out != NULL) {
                                    *needs_process_attach_out = true;
                                }
                            }
                            else {
                                status = KernelObjectManager::reference_object(&module->header);
                                if (status != StatusOK) {
                                    return status;
                                }
                                if (needs_process_attach_out != NULL) {
                                    *needs_process_attach_out = false;
                                }
                            }
                        }
                        else {
                            status = KernelObjectManager::reference_object(&module->header);
                            if (status != StatusOK) {
                                return status;
                            }
                        }

                        *module_out = module;
                        return StatusOK;
                    }

                    return load_new_module(process, module_path, explicit_open, module_out, needs_process_attach_out);
                }

                Status destroy_module(LoadedModule* module, bool process_address_space_alive) {
                    Process* process;

                    if (module == NULL) {
                        return StatusInvalidArgument;
                    }

                    process = module->owner_process;
                    unlink_module(module);
                    module->pending_process_attach = false;
                    module->pending_process_detach = false;

                    if ((process != NULL) && process_address_space_alive) {
                        (void)mm::MemoryManager::unmap(&process->process_address_space, module->image_base, module->image_bytes);
                    }

                    (void)KernelObjectManager::unregister_object(&module->header);
                    free_private_image_pages(module->private_pages);
                    module->private_pages = NULL;
                    module->private_page_count = 0U;
                    if (module->shared_image != NULL) {
                        release_shared_image(module->shared_image);
                        module->shared_image = NULL;
                        module->backing = NULL;
                    }
                    else if (module->backing != NULL) {
                        free_loader_backing(module->backing, module->image_bytes);
                        module->backing = NULL;
                    }
                    free_loaded_module_record(module);
                    return StatusOK;
                }

            } // namespace

            Status DllLoader::init(void) {
                Status status = dll_loader_heap_checkpoint_status("dll-loader:init:entry");

                if (status != StatusOK) {
                    return status;
                }

                if (!g_dll_loader_initialized) {
                    g_dll_loader_initialized = true;
                    (void)KernelResourceManager::init();
                    g_module_head = NULL;
                    g_module_tail = NULL;
                    g_shared_image_head = NULL;
                    g_shared_image_tail = NULL;
                    g_next_module_id = 1ULL;
                    g_cached_image_head = NULL;
                    g_cached_image_tail = NULL;
                    g_cached_image_count = 0U;
                    g_cached_image_peak_count = 0U;
                    g_cached_image_bytes = 0U;
                    g_cached_image_peak_bytes = 0U;
                    g_next_cached_image_object_id = 1ULL;
                }

                return dll_loader_heap_checkpoint_status("dll-loader:init:success");
            }

            bool DllLoader::is_module_handle(Process * process, VirtAddr module_handle) {
                if ((process == NULL) || (module_handle == 0U)) {
                    return false;
                }

                return find_module_by_handle(process, module_handle) != NULL;
            }

            Status DllLoader::load_user_executable(
                Process * process,
                const char* path,
                void** image_backing_out,
                Size * image_bytes_out,
                VirtAddr * entry_out) {
                ParsedImageView view = {};
                SharedLoadedImage* shared_image = NULL;
                LoaderPrivatePage* private_pages = NULL;
                U8* image_backing;
                HeapList<LoadedModule*> dependencies;
                U64 entry_point_rva;
                Size image_bytes;
                U16 image_type = 0U;
                char normalized_path[VFS_PATH_CAPACITY];
                bool shareable_image;
                U32 private_page_count = 0U;
                U64 total_start_msec = image_cache_now_msec();
                U64 stage_start_msec = total_start_msec;
                U64 shared_dependencies_msec = 0U;
                U64 image_file_msec = 0U;
                U64 copy_msec = 0U;
                U64 relocate_msec = 0U;
                U64 import_msec = 0U;
                U64 share_record_msec = 0U;
                Status status;

                if ((process == NULL) || (path == NULL) || (image_backing_out == NULL) || (image_bytes_out == NULL) || (entry_out == NULL)) {
                    return StatusInvalidArgument;
                }

                status = dll_loader_heap_checkpoint_status("dll-loader:load-exe:entry");
                if (status != StatusOK) {
                    return status;
                }

                status = init();
                if (status != StatusOK) {
                    return status;
                }

                status = normalize_path(path, normalized_path, sizeof(normalized_path));
                if (status != StatusOK) {
                    return status;
                }

                shared_image = find_shared_image_by_path(normalized_path);
                if (shared_image != NULL) {
                    const dll_header* shared_header = header_of_backing(shared_image->backing);

                    if ((shared_header == NULL) || (shared_image->image_type != DLL_IMAGE_TYPE_EXE)) {
                        return StatusFault;
                    }

                    retain_shared_image(shared_image);
                    stage_start_msec = image_cache_now_msec();
                    status = ensure_backing_dependencies_loaded(process, shared_image->backing, &dependencies);
                    shared_dependencies_msec = loader_elapsed_msec(stage_start_msec);
                    if (status != StatusOK) {
                        dependencies.clear();
                        release_shared_image(shared_image);
                        return status;
                    }

                    status = build_process_private_image_pages(
                        process,
                        shared_image->backing,
                        shared_image->image_base,
                        shared_image->image_base,
                        false,
                        process->id,
                        normalized_path,
                        &private_pages,
                        &private_page_count);
                    if (status != StatusOK) {
                        (void)release_dependencies_on_failure(dependencies);
                        dependencies.clear();
                        release_shared_image(shared_image);
                        return status;
                    }

                    status = dll_loader_heap_checkpoint_status("dll-loader:load-exe:success");
                    if (status != StatusOK) {
                        (void)release_dependencies_on_failure(dependencies);
                        dependencies.clear();
                        free_private_image_pages(private_pages);
                        release_shared_image(shared_image);
                        return status;
                    }

                    dependencies.clear();
                    process->loader_image_private_pages = private_pages;
                    process->loader_image_private_page_count = private_page_count;

                    *image_backing_out = shared_image->backing;
                    *image_bytes_out = shared_image->image_bytes;
                    *entry_out = shared_image->image_base + shared_header->entry_point_rva;
                    KRETAIL(
                        "[loader-prof] executable path=%s source=shared deps_ms=%llu total_ms=%llu\n",
                        normalized_path,
                        static_cast<unsigned long long>(shared_dependencies_msec),
                        static_cast<unsigned long long>(loader_elapsed_msec(total_start_msec)));
                    return StatusOK;
                }

                stage_start_msec = image_cache_now_msec();
                status = load_image_file(normalized_path, &view);
                image_file_msec = loader_elapsed_msec(stage_start_msec);
                if (status != StatusOK) {
                    loader_trace("[loader] executable image load failed\n");
                    return status;
                }
                if (view.header->image_type != DLL_IMAGE_TYPE_EXE) {
                    loader_trace("[loader] image is not an executable\n");
                    release_view(&view);
                    return StatusInvalidArgument;
                }
                status = dll_loader_heap_checkpoint_status("dll-loader:load-exe:view-ready");
                if (status != StatusOK) {
                    release_view(&view);
                    return status;
                }

                // `view` points into the shared staging buffer. Import resolution can load
                // dependent DLLs recursively, and each nested load reuses that buffer.
                // Snapshot the EXE entry RVA now so the final thread starts at the EXE
                // `_start` instead of whichever DLL header happened to be parsed last.
                entry_point_rva = view.header->entry_point_rva;
                image_bytes = align_up_image_bytes(view.header->image_size);
                image_type = view.header->image_type;
                shareable_image = !parsed_image_has_writable_sections(&view);

                image_backing = allocate_loader_backing(image_bytes);
                if (image_backing == NULL) {
                    loader_trace_path("[loader] executable backing allocation failed: ", path);
                    release_view(&view);
                    return StatusNoMemory;
                }
                KRETAIL("[loader] executable backing path=%s backing=%llx bytes=%llu\n", path, static_cast<unsigned long long>(reinterpret_cast<Uptr>(image_backing)), static_cast<unsigned long long>(image_bytes));
                status = dll_loader_heap_checkpoint_status("dll-loader:load-exe:backing-ready");
                if (status != StatusOK) {
                    free_loader_backing(image_backing, image_bytes);
                    release_view(&view);
                    return status;
                }

                stage_start_msec = image_cache_now_msec();
                if (view.direct_file_population) {
                    status = populate_image_backing_from_file(normalized_path, &view, image_backing, image_bytes);
                }
                else {
                    status = copy_image_to_backing(&view, image_backing, image_bytes);
                }
                copy_msec = loader_elapsed_msec(stage_start_msec);
                if (status == StatusOK) {
                    stage_start_msec = image_cache_now_msec();
                    status = apply_image_relocations(&view, image_backing, image_bytes, user_address_space::ExecutableBase);
                    relocate_msec = loader_elapsed_msec(stage_start_msec);
                }
                if (status == StatusOK) {
                    stage_start_msec = image_cache_now_msec();
                    if (shareable_image) {
                        status = ensure_backing_dependencies_loaded(process, image_backing, &dependencies);
                    }
                    else {
                        status = resolve_image_imports(process, &view, image_backing, &dependencies);
                    }
                    import_msec = loader_elapsed_msec(stage_start_msec);
                }
                if (status == StatusOK) {
                    trace_image_section_layout(
                        normalized_path,
                        &view,
                        user_address_space::ExecutableBase,
                        "exe-page-segments");
                }
                release_view(&view);
                if (status != StatusOK) {
                    KERROR("[loader] executable load failed path=%s status=%d\n", normalized_path, static_cast<int>(status));
                    free_loader_backing(image_backing, image_bytes);
                    dependencies.clear();
                    (void)dll_loader_heap_checkpoint_status("dll-loader:load-exe:copy-failure");
                    return status;
                }
                status = dll_loader_heap_checkpoint_status("dll-loader:load-exe:resolved");
                if (status != StatusOK) {
                    (void)release_dependencies_on_failure(dependencies);
                    dependencies.clear();
                    free_loader_backing(image_backing, image_bytes);
                    return status;
                }

                if (shareable_image) {
                    stage_start_msec = image_cache_now_msec();
                    status = create_shared_image_record(
                        normalized_path,
                        image_backing,
                        image_bytes,
                        image_type,
                        user_address_space::ExecutableBase,
                        &shared_image);
                    share_record_msec = loader_elapsed_msec(stage_start_msec);
                    if (status != StatusOK) {
                        (void)release_dependencies_on_failure(dependencies);
                        dependencies.clear();
                        free_loader_backing(image_backing, image_bytes);
                        return status;
                    }

                    status = build_process_private_image_pages(
                        process,
                        shared_image->backing,
                        user_address_space::ExecutableBase,
                        user_address_space::ExecutableBase,
                        false,
                        process->id,
                        normalized_path,
                        &private_pages,
                        &private_page_count);
                    if (status != StatusOK) {
                        (void)release_dependencies_on_failure(dependencies);
                        dependencies.clear();
                        release_shared_image(shared_image);
                        return status;
                    }
                }

                status = dll_loader_heap_checkpoint_status("dll-loader:load-exe:success");
                if (status != StatusOK) {
                    (void)release_dependencies_on_failure(dependencies);
                    dependencies.clear();
                    free_private_image_pages(private_pages);
                    if (shared_image != NULL) {
                        release_shared_image(shared_image);
                    }
                    else {
                        free_loader_backing(image_backing, image_bytes);
                    }
                    return status;
                }

                dependencies.clear();
                process->loader_image_private_pages = private_pages;
                process->loader_image_private_page_count = private_page_count;

                *image_backing_out = (shared_image != NULL) ? shared_image->backing : image_backing;
                *image_bytes_out = (shared_image != NULL) ? shared_image->image_bytes : image_bytes;
                *entry_out = user_address_space::ExecutableBase + entry_point_rva;
                KRETAIL(
                    "[loader-prof] executable path=%s source=file image_ms=%llu copy_ms=%llu relocate_ms=%llu imports_ms=%llu share_ms=%llu total_ms=%llu\n",
                    normalized_path,
                    static_cast<unsigned long long>(image_file_msec),
                    static_cast<unsigned long long>(copy_msec),
                    static_cast<unsigned long long>(relocate_msec),
                    static_cast<unsigned long long>(import_msec),
                    static_cast<unsigned long long>(share_record_msec),
                    static_cast<unsigned long long>(loader_elapsed_msec(total_start_msec)));
                return StatusOK;
            }

            void DllLoader::release_image_backing(void* backing, Size backing_bytes) {
                SharedLoadedImage* shared_image = find_shared_image_by_backing(backing);

                if (shared_image != NULL) {
                    release_shared_image(shared_image);
                    return;
                }

                free_loader_backing(backing, backing_bytes);
            }

            void DllLoader::release_process_image_private_pages(Process * process) {
                if (process == NULL) {
                    return;
                }

                free_private_image_pages(process->loader_image_private_pages);
                process->loader_image_private_pages = NULL;
                process->loader_image_private_page_count = 0U;
            }

            /**
             * Map one executable image into a process using section-derived page permissions.
             *
             * DLLs already land as page-by-page mappings so text can be RX while writable
             * globals stay RW. Applying the same policy to EXEs removes the last whole-image
             * RWX mapping from normal userspace launch.
             *
             * @param process Target process receiving the executable mapping.
             * @param image_backing Resident executable backing produced by the loader.
             * @return StatusOK on success, or the MMU mapping error.
             */
            Status DllLoader::map_user_executable(
                Process * process,
                const void* image_backing) {
                if ((process == NULL) || (image_backing == NULL) || (header_of_backing(image_backing) == NULL)) {
                    return StatusInvalidArgument;
                }

                return map_image_with_private_pages(
                    process,
                    user_address_space::ExecutableBase,
                    image_backing,
                    process->loader_image_private_pages);
            }

            Status DllLoader::load_library(
                Process * process,
                const char* module_path,
                VirtAddr * module_handle_out,
                bool* needs_process_attach_out) {
                LoadedModule* module;
                Status status;

                if ((process == NULL) || (module_path == NULL) || (module_handle_out == NULL)) {
                    return StatusInvalidArgument;
                }

                status = dll_loader_heap_checkpoint_status("dll-loader:load-library:entry");
                if (status != StatusOK) {
                    return status;
                }

                status = init();
                if (status != StatusOK) {
                    return status;
                }

                status = ensure_library_loaded(process, module_path, true, &module, needs_process_attach_out);
                if (status != StatusOK) {
                    return status;
                }

                *module_handle_out = module->image_base;
                return dll_loader_heap_checkpoint_status("dll-loader:load-library:success");
            }

            Status DllLoader::get_proc_address(
                Process * process,
                VirtAddr module_handle,
                const char* module_path,
                const char* export_name,
                VirtAddr * export_address_out) {
                LoadedModule* module;
                Status status;

                if ((process == NULL) || (export_name == NULL) || (export_address_out == NULL)) {
                    return StatusInvalidArgument;
                }

                status = dll_loader_heap_checkpoint_status("dll-loader:get-proc:entry");
                if (status != StatusOK) {
                    return status;
                }

                if (module_handle != 0U) {
                    module = find_module_by_handle(process, module_handle);
                }
                else {
                    module = find_module_by_path(process, module_path);
                }
                if (module == NULL) {
                    return StatusNotFound;
                }

                status = lookup_export_address(module, export_name, export_address_out);
                if (status != StatusOK) {
                    return status;
                }

                return dll_loader_heap_checkpoint_status("dll-loader:get-proc:success");
            }

            Status DllLoader::free_library(
                Process * process,
                VirtAddr module_handle,
                const char* module_path,
                VirtAddr * action_handle_out) {
                LoadedModule* module;
                const dll_header* header;
                Status status;

                if (process == NULL) {
                    return StatusInvalidArgument;
                }

                status = dll_loader_heap_checkpoint_status("dll-loader:free-library:entry");
                if (status != StatusOK) {
                    return status;
                }

                if (module_handle != 0U) {
                    module = find_module_by_handle(process, module_handle);
                }
                else {
                    module = find_module_by_path(process, module_path);
                }
                if (module == NULL) {
                    return StatusNotFound;
                }

                header = header_of(module);
                if (header == NULL) {
                    return StatusFault;
                }

                if (module->pending_process_detach) {
                    module->pending_process_detach = false;
                    refresh_module_snapshot(module);
                    if (action_handle_out != NULL) {
                        *action_handle_out = 0U;
                    }
                    status = KernelObjectManager::dereference_object(&module->header);
                    if (status != StatusOK) {
                        return status;
                    }
                    return dll_loader_heap_checkpoint_status("dll-loader:free-library:detach-complete");
                }

                if (module->header.ref_count > 1U) {
                    if (action_handle_out != NULL) {
                        *action_handle_out = 0U;
                    }
                    status = KernelObjectManager::dereference_object(&module->header);
                    if (status != StatusOK) {
                        return status;
                    }
                    return dll_loader_heap_checkpoint_status("dll-loader:free-library:ref-drop");
                }

                if (header->entry_point_rva != 0U) {
                    module->pending_process_detach = true;
                    refresh_module_snapshot(module);
                    if (action_handle_out != NULL) {
                        *action_handle_out = module->image_base;
                    }
                    return StatusOK;
                }

                if (action_handle_out != NULL) {
                    *action_handle_out = 0U;
                }
                status = KernelObjectManager::dereference_object(&module->header);
                if (status != StatusOK) {
                    return status;
                }

                return dll_loader_heap_checkpoint_status("dll-loader:free-library:success");
            }

            Status DllLoader::snapshot_process_modules_window(
                Process * process,
                Size start_index,
                void* modules,
                Size capacity,
                Size * copied_count_out,
                Size * total_count_out) {
                Size total_count = 0U;
                Size copied_count = 0U;
                Status status;
                const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();
                static bool g_snapshot_window_trace_emitted = false;

                if (copied_count_out != NULL) {
                    *copied_count_out = 0U;
                }
                if (total_count_out != NULL) {
                    *total_count_out = 0U;
                }
                if (process == NULL) {
                    arch::Arch::restore_interrupts(interrupts_enabled);
                    return StatusInvalidArgument;
                }

                status = dll_loader_heap_checkpoint_status("dll-loader:snapshot-modules:entry");
                if (status != StatusOK) {
                    arch::Arch::restore_interrupts(interrupts_enabled);
                    return status;
                }

                if (!g_snapshot_window_trace_emitted) {
                    KRETAIL(
                        "[loader] module snapshot registry path enabled item_bytes=%llu\n",
                        static_cast<unsigned long long>(sizeof(LoaderModuleSnapshot)));
                    g_snapshot_window_trace_emitted = true;
                }

                for (const LoadedModule* module = g_module_head; module != NULL; module = module->next) {
                    if (module->owner_process != process) {
                        continue;
                    }

                    if ((modules != NULL) && (total_count >= start_index) && (copied_count < capacity)) {
                        LoaderModuleSnapshot* info = &static_cast<LoaderModuleSnapshot*>(modules)[copied_count];

                        memcopy(info, &module->snapshot, sizeof(*info));
                        copied_count += 1U;
                    }
                    total_count += 1U;
                }

                if (copied_count_out != NULL) {
                    *copied_count_out = copied_count;
                }
                if (total_count_out != NULL) {
                    *total_count_out = total_count;
                }

                status = dll_loader_heap_checkpoint_status("dll-loader:snapshot-modules:success");
                arch::Arch::restore_interrupts(interrupts_enabled);
                if (status != StatusOK) {
                    return status;
                }
                if ((modules != NULL) && ((start_index + copied_count) < total_count)) {
                    return StatusNoSpace;
                }

                return StatusOK;
            }

            Status DllLoader::snapshot_process_modules(
                Process * process,
                void* modules,
                Size capacity,
                Size * count_out) {
                Size copied_count = 0U;
                Size total_count = 0U;
                const Status status = snapshot_process_modules_window(
                    process,
                    0U,
                    modules,
                    capacity,
                    &copied_count,
                    &total_count);

                (void)copied_count;
                if (count_out != NULL) {
                    *count_out = total_count;
                }
                return status;
            }

            Status DllLoader::release_process_modules(Process * process) {
                LoadedModule* module;
                LoadedModule* next_module;
                Status status;

                if (process == NULL) {
                    return StatusInvalidArgument;
                }

                status = dll_loader_heap_checkpoint_status("dll-loader:release-modules:entry");
                if (status != StatusOK) {
                    return status;
                }

                for (module = g_module_head; module != NULL; module = next_module) {
                    next_module = module->next;
                    if (module->owner_process != process) {
                        continue;
                    }

                    (void)destroy_module(module, false);
                }

                return dll_loader_heap_checkpoint_status("dll-loader:release-modules:success");
            }

            Status DllLoader::destroy_module_object(LoadedModule * module) {
                bool process_address_space_alive = false;
                Status status;

                if (module == NULL) {
                    return StatusInvalidArgument;
                }

                status = dll_loader_heap_checkpoint_status("dll-loader:destroy-module:entry");
                if (status != StatusOK) {
                    return status;
                }

                if ((module->owner_process != NULL) && (module->owner_process->process_address_space.page_table_root != 0U)) {
                    process_address_space_alive = true;
                }

                status = destroy_module(module, process_address_space_alive);
                if (status != StatusOK) {
                    return status;
                }

                return dll_loader_heap_checkpoint_status("dll-loader:destroy-module:success");
            }
