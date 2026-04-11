#include "dll_loader.h"

#include "debug-message.h"
#include "dll_image.h"
#include "heap.h"
#include "heap_list.h"
#include "kernel_time.h"
#include "mm.h"
#include "object.h"
#include "process.h"
#include "resource_manager.h"
#include "scheduler.h"
#include "serial.h"
#include "vfs.h"

struct LoadedModule {
    ObjectHeader header;
    Process* owner_process;
    void* backing;
    Size image_bytes;
    VirtAddr image_base;
    char* path;
    char* module_name;
    bool pending_process_attach;
    bool pending_process_detach;
    LoadedModule* next;
    LoadedModule* prev;
};

namespace {

    // Keep the executable image, user stack, and dynamically loaded modules on
    // distinct L2-sized boundaries because the current MMU path maps only full
    // L2 blocks into EL0 address spaces.
    inline constexpr VirtAddr UserExecutableBase = mm::backend::L2BlockSize;
    inline constexpr VirtAddr UserModuleRegionBase = 3ULL * mm::backend::L2BlockSize;
    inline constexpr VirtAddr UserModuleRegionLimit = 64ULL * mm::backend::L2BlockSize;
    inline constexpr Size VfsReadProgressMinimumBytes = 4UL * 1024UL;
    inline constexpr U64 ImageCacheTtlMsec = 5ULL * 60ULL * 1000ULL;
    inline constexpr U64 DefaultDllPreferredBase = UserModuleRegionBase;
    inline constexpr U64 DefaultDriverPreferredBase = UserModuleRegionBase + (8ULL * mm::backend::L2BlockSize);

    typedef struct CachedImageObject CachedImageObject;

    typedef struct ParsedImageView {
        const U8* file_bytes;
        Size file_size;
        CachedImageObject* cache_object;
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

    bool g_dll_loader_initialized;
    U64 g_next_module_id = 1ULL;
    LoadedModule* g_module_head;
    LoadedModule* g_module_tail;
    CachedImageObject* g_cached_image_head;
    CachedImageObject* g_cached_image_tail;
    Size g_cached_image_count;
    Size g_cached_image_peak_count;
    Size g_cached_image_bytes;
    Size g_cached_image_peak_bytes;
    U64 g_next_cached_image_object_id = 1ULL;

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

    /*
     * Compare two null-terminated strings without ASCII case sensitivity.
     *
     * @param lhs Left string.
     * @param rhs Right string.
     * @return True when both strings match ignoring ASCII case.
     */
    bool same_text_case_insensitive(const char* lhs, const char* rhs);

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

    /*
     * Allocate one fixed-size executable or DLL backing slot.
     *
     * Backing now grows directly through the resource manager. The loader no
     * longer reserves a fixed slot bank up front, which removes the previous
     * twenty-image boot ceiling from repeated process launches.
     *
     * @return Backing pointer on success, or NULL when no slot is available.
     */
    U8* allocate_loader_backing(Size backing_bytes) {
        if (backing_bytes == 0U) {
            return NULL;
        }

        U8* backing = static_cast<U8*>(KernelResourceManager::allocate(
            KernelResourceKind::LoaderImageBacking,
            backing_bytes,
            mm::PageSize,
            0ULL,
            "image-backing"));
        if (backing != NULL) {
            loader_log_backing_snapshot("alloc");
        }
        else {
            loader_log_backing_snapshot("alloc-failed");
        }

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
        KernelResourceManager::release(object);
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
        Status status;

        if ((path == NULL) || (file_bytes == NULL) || (file_size == 0U)) {
            return NULL;
        }

        cached_image_prune_expired(now_msec);

        object = static_cast<CachedImageObject*>(KernelResourceManager::allocate(
            KernelResourceKind::LoaderImageCacheRecord,
            sizeof(CachedImageObject),
            alignof(CachedImageObject),
            0ULL,
            "cache-record"));
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
        status = KernelResourceManager::track_external(
            KernelResourceKind::LoaderImageCachePayload,
            file_bytes,
            file_size,
            0ULL,
            path);
        if (status != StatusOK) {
            KernelResourceManager::release(object);
            return NULL;
        }
        g_cached_image_bytes += file_size;
        for (Size index = 0U; (index + 1U) < sizeof(object->path) && path[index] != '\0'; ++index) {
            object->path[index] = path[index];
        }
        cached_image_link(object);
        return object;
    }

    /*
     * Release one parsed-view reference on a cached image object.
     *
     * @param object Cached image object.
     * @return Nothing.
     */
    void cached_image_release(CachedImageObject* object) {
        if (object == NULL) {
            return;
        }
        if (object->reference_count != 0U) {
            object->reference_count -= 1U;
        }

        object->last_used_msec = image_cache_now_msec();
        object->expires_at_msec = object->last_used_msec + ImageCacheTtlMsec;
    }

    /**
     * Validate heap integrity at one DLL loader subsystem boundary.
     *
     * Temporary loader instrumentation should identify whether corruption first
     * appears during image staging, dependency resolution, or module teardown,
     * so every public DLL loader boundary reuses this helper.
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

    char* duplicate_text(const char* source, const char* fallback) {
        Size index = 0U;
        const char* active = ((source != NULL) && (source[0] != '\0')) ? source : fallback;
        char* text;

        while (active[index] != '\0') {
            ++index;
        }

        text = static_cast<char*>(Heap::alloc(index + 1U, alignof(char)));
        if (text == NULL) {
            return NULL;
        }

        for (Size copy_index = 0U; copy_index < index; ++copy_index) {
            text[copy_index] = active[copy_index];
        }
        text[index] = '\0';
        return text;
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

    const dll_header* header_of(const LoadedModule* module) {
        return (module == NULL) ? NULL : reinterpret_cast<const dll_header*>(module->backing);
    }

    const dll_export_symbol* exports_of(const LoadedModule* module) {
        const dll_header* header = header_of(module);

        if ((header == NULL) || (module->backing == NULL)) {
            return NULL;
        }

        return reinterpret_cast<const dll_export_symbol*>(static_cast<const U8*>(module->backing) + header->export_table_offset);
    }

    const dll_import_module* imports_of(const LoadedModule* module) {
        const dll_header* header = header_of(module);

        if ((header == NULL) || (module->backing == NULL)) {
            return NULL;
        }

        return reinterpret_cast<const dll_import_module*>(static_cast<const U8*>(module->backing) + header->import_module_table_offset);
    }

    const char* string_table_of(const LoadedModule* module) {
        const dll_header* header = header_of(module);

        if ((header == NULL) || (module->backing == NULL)) {
            return NULL;
        }

        return reinterpret_cast<const char*>(static_cast<const U8*>(module->backing) + header->string_table_offset);
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
        module->prev = g_module_tail;
        module->next = NULL;

        if (g_module_tail != NULL) {
            g_module_tail->next = module;
        }
        else {
            g_module_head = module;
        }

        g_module_tail = module;
    }

    void unlink_module(LoadedModule* module) {
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
    }

    LoadedModule* find_module_by_handle(Process* process, VirtAddr module_handle) {
        for (LoadedModule* module = g_module_head; module != NULL; module = module->next) {
            if ((module->owner_process == process) && (module->image_base == module_handle)) {
                return module;
            }
        }

        return NULL;
    }

    LoadedModule* find_module_by_path(Process* process, const char* module_path) {
        for (LoadedModule* module = g_module_head; module != NULL; module = module->next) {
            if ((module->owner_process == process) && matches_path(module, module_path)) {
                return module;
            }
        }

        return NULL;
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
        for (LoadedModule* module = g_module_head; module != NULL; module = module->next) {
            if (module->owner_process != process) {
                continue;
            }
            if (ranges_overlap(image_base, image_bytes, module->image_base, module->image_bytes)) {
                return true;
            }
        }

        return false;
    }

    VirtAddr choose_base(Process* process, U64 preferred_base, Size image_bytes) {
        VirtAddr candidate_base = align_up_module_base(static_cast<VirtAddr>(preferred_base));

        if ((process == NULL) || (image_bytes == 0U)) {
            return 0U;
        }

        if ((candidate_base >= UserModuleRegionBase)
            && ((candidate_base + image_bytes) <= UserModuleRegionLimit)
            && !module_range_in_use(process, candidate_base, image_bytes)) {
            return candidate_base;
        }

        for (candidate_base = UserModuleRegionBase;
            (candidate_base + image_bytes) <= UserModuleRegionLimit;
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
        if ((header->image_size == 0U) || (header->image_size > (UserModuleRegionLimit - UserModuleRegionBase))) {
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
        if ((view == NULL) || (view->header == NULL) || (image_memory == NULL)) {
            return StatusInvalidArgument;
        }
        if (view->header->image_size > capacity) {
            loader_trace("[loader] image backing is too small\n");
            return StatusNotSupported;
        }

        memzero(image_memory, capacity);
        memcopy(image_memory, view->file_bytes, view->header->header_size);

        for (U32 section_index = 0U; section_index < view->header->section_count; ++section_index) {
            const dll_section* section = &view->sections[section_index];
            U8* destination = image_memory + section->virtual_address;

            if ((section->flags & DLL_SEC_BSS) != 0U) {
                memzero(destination, section->virtual_size);
                continue;
            }

            if (section->raw_data_size != 0U) {
                memcopy(destination, view->file_bytes + section->raw_data_offset, section->raw_data_size);
            }
            if (section->virtual_size > section->raw_data_size) {
                memzero(destination + section->raw_data_size, section->virtual_size - section->raw_data_size);
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
        const dll_header* header = header_of(module);
        U64 mapping_flags = PagePresent | PageUser;

        if ((process == NULL) || (module == NULL) || (header == NULL)) {
            return StatusInvalidArgument;
        }

        for (U32 section_index = 0U; section_index < header->section_count; ++section_index) {
            const dll_section* section = reinterpret_cast<const dll_section*>(static_cast<const U8*>(module->backing) + header->section_table_offset) + section_index;

            if ((section->flags & DLL_SEC_WRITE) != 0U) {
                mapping_flags |= PageWritable;
            }
            if ((section->flags & DLL_SEC_EXEC) != 0U) {
                mapping_flags |= PageExecutable;
            }
        }

        const VmMapping mapping = {
            module->image_base,
            mm::MemoryManager::kernel_to_physical(reinterpret_cast<VirtAddr>(module->backing)),
            module->image_bytes,
            mapping_flags,
        };

        return mm::MemoryManager::map(&process->process_address_space, &mapping);
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

            for (U32 symbol_offset = 0U; symbol_offset < import_module->symbol_count; ++symbol_offset) {
                const dll_import_symbol* import_symbol = &resident_import_symbols[import_module->first_symbol_index + symbol_offset];
                const char* import_symbol_name = bounded_string_at(resident_string_table, resident_header->string_table_size, import_symbol->symbol_name_offset);
                VirtAddr export_address = 0U;

                status = lookup_export_address(dependency, import_symbol_name, &export_address);
                if (status != StatusOK) {
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

    Status create_loaded_module(
        Process* process,
        const char* module_path,
        void* backing,
        Size image_bytes,
        VirtAddr image_base,
        LoadedModule** module_out) {
        LoadedModule* module;
        Status status;

        if ((process == NULL) || (module_path == NULL) || (backing == NULL) || (module_out == NULL)) {
            return StatusInvalidArgument;
        }

        module = static_cast<LoadedModule*>(KernelResourceManager::allocate(
            KernelResourceKind::LoaderModuleRecord,
            sizeof(LoadedModule),
            alignof(LoadedModule),
            process->id,
            module_path));
        if (module == NULL) {
            return StatusNoMemory;
        }

        memzero(module, sizeof(*module));
        module->header.type = ObjectType::Module;
        module->header.flags = ObjectFlags::None;
        module->header.object_id = g_next_module_id++;
        module->header.ref_count = 1U;
        module->owner_process = process;
        module->backing = backing;
        module->image_bytes = image_bytes;
        module->image_base = image_base;
        module->pending_process_attach = true;
        module->pending_process_detach = false;
        module->path = duplicate_text(module_path, "C:/lib/unknown.dll");
        module->module_name = duplicate_text(path_basename(module_path), "unknown.dll");
        if ((module->path == NULL) || (module->module_name == NULL)) {
            if (module->path != NULL) {
                Heap::free(module->path);
            }
            if (module->module_name != NULL) {
                Heap::free(module->module_name);
            }
            KernelResourceManager::release(module);
            return StatusNoMemory;
        }

        status = KernelObjectManager::register_object(&module->header);
        if (status != StatusOK) {
            Heap::free(module->module_name);
            Heap::free(module->path);
            KernelResourceManager::release(module);
            return status;
        }

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
        U64 now_msec = image_cache_now_msec();
        bool normalized_ok = false;
        Status status;

        if ((image_path == NULL) || (view_out == NULL)) {
            return StatusInvalidArgument;
        }

        status = normalize_path(image_path, normalized_path, sizeof(normalized_path));
        if (status == StatusOK) {
            normalized_ok = true;
            cached_object = cached_image_find(normalized_path, now_msec);
            if (cached_object != NULL) {
                status = parse_image_view(cached_object->file_bytes, cached_object->file_size, view_out);
                if (status == StatusOK) {
                    view_out->cache_object = cached_object;
                    return StatusOK;
                }

                cached_image_release(cached_object);
                return status;
            }
        }

        status = read_file_into_staging(image_path, &image_bytes, &image_size);
        if (status != StatusOK) {
            return status;
        }

        status = parse_image_view(image_bytes, image_size, view_out);
        if (status != StatusOK) {
            Heap::free(image_bytes);
            return status;
        }

        if (normalized_ok) {
            cached_object = cached_image_insert(normalized_path, image_bytes, image_size, now_msec);
            if (cached_object != NULL) {
                view_out->cache_object = cached_object;
            }
        }

        return StatusOK;
    }

    Status load_new_module(
        Process* process,
        const char* module_path,
        bool explicit_open,
        LoadedModule** module_out,
        bool* needs_process_attach_out) {
        ParsedImageView view = {};
        U8* backing = NULL;
        VirtAddr image_base = 0U;
        U64 preferred_base;
        char normalized_module_path[VFS_PATH_CAPACITY];
        LoadedModule* module = NULL;
        HeapList<LoadedModule*> dependencies;
        Status status;

        if ((process == NULL) || (module_path == NULL) || (module_out == NULL)) {
            return StatusInvalidArgument;
        }

        status = normalize_path(module_path, normalized_module_path, sizeof(normalized_module_path));
        if (status != StatusOK) {
            return status;
        }

        status = load_image_file(normalized_module_path, &view);
        if (status != StatusOK) {
            return status;
        }
        if ((view.header->image_type != DLL_IMAGE_TYPE_DLL) && (view.header->image_type != DLL_IMAGE_TYPE_DRIVER)) {
            release_view(&view);
            return StatusInvalidArgument;
        }

        preferred_base = view.header->image_base;
        if (preferred_base == 0U) {
            preferred_base = (view.header->image_type == DLL_IMAGE_TYPE_DRIVER) ? DefaultDriverPreferredBase : DefaultDllPreferredBase;
        }

        const Size image_bytes = align_up_image_bytes(view.header->image_size);

        backing = allocate_loader_backing(image_bytes);
        if (backing == NULL) {
            loader_trace_path("[loader] module backing allocation failed: ", normalized_module_path);
            release_view(&view);
            return StatusNoMemory;
        }
        KRETAIL("[loader] module backing path=%s backing=%llx bytes=%llu\n", normalized_module_path, static_cast<unsigned long long>(reinterpret_cast<Uptr>(backing)), static_cast<unsigned long long>(image_bytes));

        status = copy_image_to_backing(&view, backing, image_bytes);
        if (status == StatusOK) {
            status = resolve_image_imports(process, &view, backing, &dependencies);
        }
        if (status == StatusOK) {
            image_base = choose_base(process, preferred_base, image_bytes);
            if (image_base == 0U) {
                status = StatusNoSpace;
            }
        }
        if (status == StatusOK) {
            status = apply_image_relocations(&view, backing, image_bytes, image_base);
        }
        release_view(&view);
        if (status != StatusOK) {
            dependencies.clear();
            free_loader_backing(backing, image_bytes);
            return status;
        }

        status = create_loaded_module(process, normalized_module_path, backing, image_bytes, image_base, &module);
        if (status != StatusOK) {
            (void)release_dependencies_on_failure(dependencies);
            dependencies.clear();
            free_loader_backing(backing, image_bytes);
            return status;
        }

        status = map_module(process, module);
        if (status != StatusOK) {
            unlink_module(module);
            (void)KernelObjectManager::unregister_object(&module->header);
            Heap::free(module->module_name);
            Heap::free(module->path);
            KernelResourceManager::release(module);
            (void)release_dependencies_on_failure(dependencies);
            dependencies.clear();
            free_loader_backing(backing, image_bytes);
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
            *needs_process_attach_out = true;
        }

        *module_out = module;
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
        if (module->module_name != NULL) {
            Heap::free(module->module_name);
            module->module_name = NULL;
        }
        if (module->path != NULL) {
            Heap::free(module->path);
            module->path = NULL;
        }
        if (module->backing != NULL) {
            free_loader_backing(module->backing, module->image_bytes);
            module->backing = NULL;
        }
        KernelResourceManager::release(module);
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

bool DllLoader::is_module_handle(Process* process, VirtAddr module_handle) {
    if ((process == NULL) || (module_handle == 0U)) {
        return false;
    }

    return find_module_by_handle(process, module_handle) != NULL;
}

Status DllLoader::load_user_executable(
    Process* process,
    const char* path,
    void** image_backing_out,
    Size* image_bytes_out,
    VirtAddr* entry_out) {
    ParsedImageView view = {};
    U8* image_backing;
    HeapList<LoadedModule*> dependencies;
    U64 entry_point_rva;
    Size image_bytes;
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

    status = load_image_file(path, &view);
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

    status = copy_image_to_backing(&view, image_backing, image_bytes);
    if (status == StatusOK) {
        status = apply_image_relocations(&view, image_backing, image_bytes, UserExecutableBase);
    }
    if (status == StatusOK) {
        status = resolve_image_imports(process, &view, image_backing, &dependencies);
    }
    release_view(&view);
    dependencies.clear();
    if (status != StatusOK) {
        loader_trace("[loader] executable copy or relocation failed\n");
        free_loader_backing(image_backing, image_bytes);
        (void)dll_loader_heap_checkpoint_status("dll-loader:load-exe:copy-failure");
        return status;
    }
    status = dll_loader_heap_checkpoint_status("dll-loader:load-exe:resolved");
    if (status != StatusOK) {
        free_loader_backing(image_backing, image_bytes);
        return status;
    }

    *image_backing_out = image_backing;
    *image_bytes_out = image_bytes;
    *entry_out = UserExecutableBase + entry_point_rva;
    return dll_loader_heap_checkpoint_status("dll-loader:load-exe:success");
}

void DllLoader::release_image_backing(void* backing, Size backing_bytes) {
    free_loader_backing(backing, backing_bytes);
}

Status DllLoader::load_library(
    Process* process,
    const char* module_path,
    VirtAddr* module_handle_out,
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
    Process* process,
    VirtAddr module_handle,
    const char* module_path,
    const char* export_name,
    VirtAddr* export_address_out) {
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
    Process* process,
    VirtAddr module_handle,
    const char* module_path,
    VirtAddr* action_handle_out) {
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

Status DllLoader::release_process_modules(Process* process) {
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

Status DllLoader::destroy_module_object(LoadedModule* module) {
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
