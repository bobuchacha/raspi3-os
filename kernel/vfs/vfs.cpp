#include "vfs.h"

#include "heap.h"
#include "kernel_time.h"
#include "kernel_event_broker.h"
#include "scheduler.h"

namespace {

    inline constexpr U64 VfsFileCacheTtlMsec = 5ULL * 60ULL * 1000ULL;
    inline constexpr Size VfsFileCacheEntryCount = 32U;
    inline constexpr Size VfsFileCacheMaxBytes = 24U * 1024U * 1024U;
    inline constexpr Size VfsFileCacheMaxFileBytes = 4U * 1024U * 1024U;

    typedef struct VfsVolumeEntry {
        bool mounted;
        VfsMount mount;
    } VfsVolumeEntry;

    typedef struct VfsDeviceAliasEntry {
        bool used;
        char name[VFS_DEVICE_NAME_CAPACITY];
        Device* device;
    } VfsDeviceAliasEntry;

    typedef struct VfsFileCacheEntry {
        bool used;
        char volume_letter;
        U16 reserved0;
        U32 reserved1;
        U64 inode;
        U64 size_bytes;
        U64 expires_at_msec;
        U64 last_used_msec;
        U8* data;
    } VfsFileCacheEntry;

    VfsVolumeEntry g_volume_table[VFS_VOLUME_SLOT_COUNT];
    VfsDeviceAliasEntry g_device_aliases[VFS_DEVICE_ALIAS_CAPACITY];
    VfsFileCacheEntry g_vfs_file_cache[VfsFileCacheEntryCount];
    Size g_vfs_file_cache_bytes;

    /*
     * Return the current uptime used by the VFS cache TTL bookkeeping.
     *
     * @return Scheduler-backed uptime in milliseconds.
     */
    U64 vfs_cache_now_msec(void) {
        return KernelTime::ticks_to_milliseconds(Scheduler::tick_count());
    }

    /*
     * Release one cached file image and clear its metadata.
     *
     * @param entry Cache entry to release.
     * @return Nothing.
     */
    void vfs_file_cache_clear_entry(VfsFileCacheEntry* entry) {
        if ((entry == NULL) || !entry->used) {
            return;
        }

        if (entry->data != NULL) {
            Heap::free(entry->data);
            entry->data = NULL;
        }
        if (g_vfs_file_cache_bytes >= static_cast<Size>(entry->size_bytes)) {
            g_vfs_file_cache_bytes -= static_cast<Size>(entry->size_bytes);
        }
        else {
            g_vfs_file_cache_bytes = 0U;
        }

        memzero(entry, sizeof(*entry));
    }

    /*
     * Drop every cached file image.
     *
     * VFS mutation paths use whole-cache invalidation so the read cache stays
     * coherent without maintaining per-entry path bookkeeping.
     *
     * @return Nothing.
     */
    void vfs_file_cache_invalidate_all(void) {
        for (Size index = 0U; index < COUNT_OF(g_vfs_file_cache); ++index) {
            vfs_file_cache_clear_entry(&g_vfs_file_cache[index]);
        }

        g_vfs_file_cache_bytes = 0U;
    }

    /*
     * Release every cache entry whose TTL already elapsed.
     *
     * @param now_msec Current uptime snapshot.
     * @return Nothing.
     */
    void vfs_file_cache_prune_expired(U64 now_msec) {
        for (Size index = 0U; index < COUNT_OF(g_vfs_file_cache); ++index) {
            VfsFileCacheEntry* entry = &g_vfs_file_cache[index];

            if (!entry->used) {
                continue;
            }
            if (entry->expires_at_msec > now_msec) {
                continue;
            }

            vfs_file_cache_clear_entry(entry);
        }
    }

    /*
     * Report whether one file node is eligible for whole-file content caching.
     *
     * @param node Resolved VFS node.
     * @return True when the node describes a small filesystem-backed file.
     */
    bool vfs_file_cacheable(const VfsNode* node) {
        return (node != NULL)
            && (node->backend_kind == VfsBackendKindFilesystem)
            && (node->type == VfsNodeTypeFile)
            && (node->size_bytes != 0ULL)
            && (node->size_bytes <= VfsFileCacheMaxFileBytes)
            && (node->filesystem != NULL)
            && (node->filesystem->ops != NULL)
            && (node->filesystem->ops->read != NULL);
    }

    /*
     * Report whether one cache entry still matches the resolved file node.
     *
     * Matching by volume and inode lets independently resolved callers reuse
     * the same cached bytes without threading path strings through every read.
     *
     * @param entry Cache entry candidate.
     * @param node Resolved VFS node.
     * @return True when `entry` can satisfy reads for `node`.
     */
    bool vfs_file_cache_entry_matches_node(const VfsFileCacheEntry* entry, const VfsNode* node) {
        return (entry != NULL)
            && entry->used
            && (node != NULL)
            && (entry->volume_letter == node->volume_letter)
            && (entry->inode == node->inode)
            && (entry->size_bytes == node->size_bytes)
            && (entry->data != NULL);
    }

    /*
     * Find one live cached file image for the resolved node.
     *
     * @param node Resolved VFS node.
     * @param now_msec Current uptime snapshot.
     * @return Matching cache entry, or NULL when the cache missed.
     */
    VfsFileCacheEntry* vfs_file_cache_find(const VfsNode* node, U64 now_msec) {
        vfs_file_cache_prune_expired(now_msec);

        for (Size index = 0U; index < COUNT_OF(g_vfs_file_cache); ++index) {
            VfsFileCacheEntry* entry = &g_vfs_file_cache[index];

            if (!vfs_file_cache_entry_matches_node(entry, node)) {
                continue;
            }

            entry->last_used_msec = now_msec;
            entry->expires_at_msec = now_msec + VfsFileCacheTtlMsec;
            return entry;
        }

        return NULL;
    }

    /*
     * Return the least-recently-used live cache entry.
     *
     * @return Oldest cache entry, or NULL when the cache is empty.
     */
    VfsFileCacheEntry* vfs_file_cache_find_lru(void) {
        VfsFileCacheEntry* best = NULL;

        for (Size index = 0U; index < COUNT_OF(g_vfs_file_cache); ++index) {
            VfsFileCacheEntry* entry = &g_vfs_file_cache[index];

            if (!entry->used) {
                continue;
            }
            if ((best == NULL) || (entry->last_used_msec < best->last_used_msec)) {
                best = entry;
            }
        }

        return best;
    }

    /*
     * Make room for one additional cached file image.
     *
     * @param required_bytes Bytes the caller wants to add to the cache.
     * @param now_msec Current uptime snapshot.
     * @return True when the cache budget can accommodate the request.
     */
    bool vfs_file_cache_make_room(Size required_bytes, U64 now_msec) {
        vfs_file_cache_prune_expired(now_msec);
        while ((g_vfs_file_cache_bytes + required_bytes) > VfsFileCacheMaxBytes) {
            VfsFileCacheEntry* victim = vfs_file_cache_find_lru();

            if (victim == NULL) {
                break;
            }

            vfs_file_cache_clear_entry(victim);
        }

        return (g_vfs_file_cache_bytes + required_bytes) <= VfsFileCacheMaxBytes;
    }

    /*
     * Read one file directly from its filesystem backend.
     *
     * Cache fill paths must bypass `VirtualFileSystem::read` so they do not
     * recurse back into the cache layer while satisfying a miss.
     *
     * @param node Resolved filesystem file node.
     * @param offset Starting byte offset.
     * @param buffer Destination buffer.
     * @param length Requested byte count.
     * @return Backend read result.
     */
    SSize vfs_read_filesystem_backend(const VfsNode* node, U64 offset, void* buffer, Size length) {
        if ((node == NULL) || (node->filesystem == NULL) || (node->filesystem->ops == NULL) || (node->filesystem->ops->read == NULL)) {
            return StatusNotSupported;
        }

        return node->filesystem->ops->read(node->filesystem_state, const_cast<FilesystemNode*>(&node->backing_node), offset, buffer, length);
    }

    /*
     * Copy one caller-requested byte range from a cached file image.
     *
     * @param entry Cache entry holding the full file contents.
     * @param offset Starting file offset.
     * @param buffer Destination buffer.
     * @param length Requested byte count.
     * @return Bytes copied, or zero at EOF.
     */
    SSize vfs_file_cache_copy_range(const VfsFileCacheEntry* entry, U64 offset, void* buffer, Size length) {
        Size available;
        Size copy_bytes;

        if ((entry == NULL) || ((buffer == NULL) && (length != 0U)) || !entry->used || (entry->data == NULL)) {
            return StatusInvalidArgument;
        }
        if ((length == 0U) || (offset >= entry->size_bytes)) {
            return 0;
        }

        available = static_cast<Size>(entry->size_bytes - offset);
        copy_bytes = (length < available) ? length : available;
        memcopy(buffer, entry->data + offset, copy_bytes);
        return static_cast<SSize>(copy_bytes);
    }

    /*
     * Populate one cache entry with the complete contents of a filesystem file.
     *
     * @param node Resolved filesystem file node.
     * @param now_msec Current uptime snapshot.
     * @return Cache entry on success, or NULL when the file was not cached.
     */
    VfsFileCacheEntry* vfs_file_cache_populate(const VfsNode* node, U64 now_msec) {
        VfsFileCacheEntry* slot = NULL;
        U8* bytes = NULL;
        SSize read_result;

        if (!vfs_file_cacheable(node)) {
            return NULL;
        }
        if (!vfs_file_cache_make_room(static_cast<Size>(node->size_bytes), now_msec)) {
            return NULL;
        }

        for (;;) {
            bytes = static_cast<U8*>(Heap::alloc(static_cast<Size>(node->size_bytes), alignof(U8)));
            if (bytes != NULL) {
                break;
            }

            slot = vfs_file_cache_find_lru();
            if (slot == NULL) {
                return NULL;
            }

            vfs_file_cache_clear_entry(slot);
        }

        read_result = vfs_read_filesystem_backend(node, 0U, bytes, static_cast<Size>(node->size_bytes));
        if ((read_result < 0) || (static_cast<U64>(read_result) != node->size_bytes)) {
            Heap::free(bytes);
            return NULL;
        }

        for (Size index = 0U; index < COUNT_OF(g_vfs_file_cache); ++index) {
            if (!g_vfs_file_cache[index].used) {
                slot = &g_vfs_file_cache[index];
                break;
            }
        }
        if (slot == NULL) {
            slot = vfs_file_cache_find_lru();
        }
        if (slot == NULL) {
            Heap::free(bytes);
            return NULL;
        }

        vfs_file_cache_clear_entry(slot);
        slot->used = true;
        slot->volume_letter = node->volume_letter;
        slot->inode = node->inode;
        slot->size_bytes = node->size_bytes;
        slot->expires_at_msec = now_msec + VfsFileCacheTtlMsec;
        slot->last_used_msec = now_msec;
        slot->data = bytes;
        g_vfs_file_cache_bytes += static_cast<Size>(slot->size_bytes);
        return slot;
    }

    int ascii_upper(int ch) {
        if ((ch >= 'a') && (ch <= 'z')) {
            return ch - ('a' - 'A');
        }

        return ch;
    }

    bool same_text_case_insensitive(const char* lhs, const char* rhs) {
        if (lhs == rhs) {
            return true;
        }
        if ((lhs == NULL) || (rhs == NULL)) {
            return false;
        }

        while ((*lhs != '\0') && (*rhs != '\0')) {
            if (ascii_upper(*lhs) != ascii_upper(*rhs)) {
                return false;
            }
            ++lhs;
            ++rhs;
        }

        return *lhs == *rhs;
    }

    bool is_path_separator(char ch) {
        return (ch == '\\') || (ch == '/');
    }

    bool is_drive_letter(char ch) {
        int upper = ascii_upper(ch);

        return (upper >= VFS_DRIVE_LETTER_FIRST) && (upper <= VFS_DRIVE_LETTER_LAST);
    }

    void copy_upper_text(char* destination, Size capacity, const char* source) {
        Size index = 0U;

        if ((destination == NULL) || (capacity == 0U)) {
            return;
        }

        while ((source != NULL) && (source[index] != '\0') && ((index + 1U) < capacity)) {
            destination[index] = (char)ascii_upper(source[index]);
            ++index;
        }

        destination[index] = '\0';
    }

    int volume_index_for_letter(char drive_letter) {
        int upper = ascii_upper(drive_letter);

        if (!is_drive_letter((char)upper)) {
            return -1;
        }

        return upper - 'A';
    }

    VfsDeviceAliasEntry* find_device_alias(const char* name) {
        for (Size index = 0; index < COUNT_OF(g_device_aliases); ++index) {
            if (g_device_aliases[index].used && same_text_case_insensitive(g_device_aliases[index].name, name)) {
                return &g_device_aliases[index];
            }
        }

        return NULL;
    }

    bool parse_volume_path(const char* path, char* drive_letter_out, const char** suffix_out) {
        if ((path == NULL) || (drive_letter_out == NULL) || (suffix_out == NULL)) {
            return false;
        }
        if (!is_drive_letter(path[0]) || (path[1] != ':')) {
            return false;
        }
        if ((path[2] != '\0') && !is_path_separator(path[2])) {
            return false;
        }

        *drive_letter_out = (char)ascii_upper(path[0]);
        *suffix_out = path + 2;
        return true;
    }

    Status parse_device_name(const char* path, char normalized_name[VFS_DEVICE_NAME_CAPACITY]) {
        Size colon_index = 0U;
        bool found_colon = false;

        if ((path == NULL) || (normalized_name == NULL) || (path[0] == '\0')) {
            return StatusInvalidArgument;
        }

        while (path[colon_index] != '\0') {
            if (path[colon_index] == ':') {
                found_colon = true;
                break;
            }
            if (is_path_separator(path[colon_index])) {
                return StatusInvalidArgument;
            }
            ++colon_index;
        }

        if (!found_colon || (path[colon_index + 1U] != '\0') || (colon_index == 0U) || (colon_index >= VFS_DEVICE_NAME_CAPACITY)) {
            return StatusInvalidArgument;
        }

        for (Size index = 0; index < colon_index; ++index) {
            normalized_name[index] = (char)ascii_upper(path[index]);
        }
        normalized_name[colon_index] = '\0';
        return StatusOK;
    }

    Status normalize_volume_relative_path(const char* suffix, char normalized_path[VFS_PATH_CAPACITY]) {
        Size write_index = 0U;
        bool last_was_separator = false;

        if ((suffix == NULL) || (normalized_path == NULL)) {
            return StatusInvalidArgument;
        }

        while (is_path_separator(*suffix)) {
            ++suffix;
        }

        while (*suffix != '\0') {
            if (write_index + 1U >= VFS_PATH_CAPACITY) {
                return StatusNoSpace;
            }
            if (*suffix == ':') {
                return StatusInvalidArgument;
            }
            if (is_path_separator(*suffix)) {
                if (!last_was_separator) {
                    normalized_path[write_index++] = '/';
                    last_was_separator = true;
                }
            }
            else {
                normalized_path[write_index++] = *suffix;
                last_was_separator = false;
            }
            ++suffix;
        }

        if ((write_index != 0U) && (normalized_path[write_index - 1U] == '/')) {
            --write_index;
        }

        normalized_path[write_index] = '\0';
        return StatusOK;
    }

    void populate_filesystem_node(VfsNode* node, const VfsVolumeEntry* volume, const FilesystemNode* filesystem_node) {
        memzero(node, sizeof(*node));
        node->inode = filesystem_node->identifier;
        node->size_bytes = filesystem_node->size_bytes;
        node->flags = filesystem_node->mode;
        if (filesystem_node->type == FilesystemNodeTypeDirectory) {
            node->type = VfsNodeTypeDirectory;
        }
        else if (filesystem_node->type == FilesystemNodeTypeDevice) {
            node->type = VfsNodeTypeDevice;
        }
        else {
            node->type = VfsNodeTypeFile;
        }
        node->backend_kind = VfsBackendKindFilesystem;
        node->volume_letter = volume->mount.drive_letter;
        node->filesystem = volume->mount.filesystem;
        node->filesystem_state = volume->mount.filesystem_state;
        node->backing_node = *filesystem_node;
    }

    void populate_device_node(VfsNode* node, const char* device_name, Device* device) {
        memzero(node, sizeof(*node));
        node->type = VfsNodeTypeDevice;
        node->backend_kind = VfsBackendKindDevice;
        node->device = device;
        copy_upper_text(node->device_name, COUNT_OF(node->device_name), device_name);
    }

    Status vfs_mount_volume_impl(const VfsMount* mount) {
        int volume_index;
        Status status;

        if (mount == NULL) {
            return StatusInvalidArgument;
        }
        if (mount->filesystem == NULL) {
            return StatusInvalidArgument;
        }

        volume_index = volume_index_for_letter(mount->drive_letter);
        if (volume_index < 0) {
            return StatusInvalidArgument;
        }
        if (g_volume_table[volume_index].mounted) {
            return StatusAlreadyExists;
        }
        if ((mount->filesystem->ops == NULL) || (mount->filesystem->ops->mount == NULL)) {
            return StatusNotSupported;
        }

        status = mount->filesystem->ops->mount(mount->filesystem_state, mount->device_state);
        if ((status != StatusOK) && (status != StatusAlreadyExists)) {
            return status;
        }

        g_volume_table[volume_index].mounted = true;
        g_volume_table[volume_index].mount = *mount;
        g_volume_table[volume_index].mount.drive_letter = (char)ascii_upper(mount->drive_letter);
        return StatusOK;
    }

    Status vfs_register_device_name_impl(const char* name, Device* device) {
        char normalized_name[VFS_DEVICE_NAME_CAPACITY];

        if ((name == NULL) || (device == NULL)) {
            return StatusInvalidArgument;
        }

        memzero(normalized_name, sizeof(normalized_name));
        copy_upper_text(normalized_name, sizeof(normalized_name), name);
        if (normalized_name[0] == '\0') {
            return StatusInvalidArgument;
        }
        if (find_device_alias(normalized_name) != NULL) {
            return StatusAlreadyExists;
        }

        for (Size index = 0; index < COUNT_OF(g_device_aliases); ++index) {
            if (!g_device_aliases[index].used) {
                g_device_aliases[index].used = true;
                g_device_aliases[index].device = device;
                copy_upper_text(g_device_aliases[index].name, COUNT_OF(g_device_aliases[index].name), normalized_name);
                return StatusOK;
            }
        }

        return StatusNoSpace;
    }

    Status vfs_resolve_impl(const char* path, VfsNode* node) {
        char drive_letter;
        const char* volume_suffix;
        char normalized_path[VFS_PATH_CAPACITY];
        char normalized_name[VFS_DEVICE_NAME_CAPACITY];
        int volume_index;
        FilesystemNode filesystem_node;
        VfsDeviceAliasEntry* alias;

        if ((path == NULL) || (node == NULL)) {
            return StatusInvalidArgument;
        }

        memzero(node, sizeof(*node));
        if (parse_volume_path(path, &drive_letter, &volume_suffix)) {
            volume_index = volume_index_for_letter(drive_letter);
            if ((volume_index < 0) || !g_volume_table[volume_index].mounted) {
                return StatusNotFound;
            }
            if ((g_volume_table[volume_index].mount.filesystem->ops == NULL) ||
                (g_volume_table[volume_index].mount.filesystem->ops->lookup == NULL)) {
                return StatusNotSupported;
            }

            memzero(normalized_path, sizeof(normalized_path));
            {
                Status status = normalize_volume_relative_path(volume_suffix, normalized_path);

                if (status != StatusOK) {
                    return status;
                }
            }

            memzero(&filesystem_node, sizeof(filesystem_node));
            {
                Status status = g_volume_table[volume_index].mount.filesystem->ops->lookup(
                    g_volume_table[volume_index].mount.filesystem_state,
                    normalized_path,
                    &filesystem_node);

                if (status != StatusOK) {
                    return status;
                }
            }

            populate_filesystem_node(node, &g_volume_table[volume_index], &filesystem_node);
            return StatusOK;
        }

        memzero(normalized_name, sizeof(normalized_name));
        {
            Status status = parse_device_name(path, normalized_name);

            if (status != StatusOK) {
                return status;
            }
        }

        alias = find_device_alias(normalized_name);
        if (alias == NULL) {
            return StatusNotFound;
        }

        populate_device_node(node, normalized_name, alias->device);
        return StatusOK;
    }

    SSize vfs_read_impl(const VfsNode* node, U64 offset, void* buffer, Size length) {
        U64 now_msec;
        VfsFileCacheEntry* cached_entry;

        if ((node == NULL) || ((buffer == NULL) && (length != 0U))) {
            return StatusInvalidArgument;
        }

        if (node->backend_kind == VfsBackendKindDevice) {
            if (node->device == NULL) {
                return StatusNotFound;
            }

            return node->device->read(offset, buffer, length);
        }

        if (node->backend_kind == VfsBackendKindFilesystem) {
            if ((node->filesystem == NULL) || (node->filesystem->ops == NULL) || (node->filesystem->ops->read == NULL)) {
                return StatusNotSupported;
            }

            now_msec = vfs_cache_now_msec();
            cached_entry = vfs_file_cache_find(node, now_msec);
            if (cached_entry != NULL) {
                return vfs_file_cache_copy_range(cached_entry, offset, buffer, length);
            }

            cached_entry = vfs_file_cache_populate(node, now_msec);
            if (cached_entry != NULL) {
                return vfs_file_cache_copy_range(cached_entry, offset, buffer, length);
            }

            return vfs_read_filesystem_backend(node, offset, buffer, length);
        }

        return StatusNotSupported;
    }

    Status resolve_filesystem_path(const char* path, const VfsVolumeEntry** volume_out, char normalized_path[VFS_PATH_CAPACITY]) {
        char drive_letter;
        const char* volume_suffix;
        int volume_index;

        if ((path == NULL) || (volume_out == NULL) || (normalized_path == NULL)) {
            return StatusInvalidArgument;
        }
        if (!parse_volume_path(path, &drive_letter, &volume_suffix)) {
            return StatusInvalidArgument;
        }

        volume_index = volume_index_for_letter(drive_letter);
        if ((volume_index < 0) || !g_volume_table[volume_index].mounted) {
            return StatusNotFound;
        }

        memzero(normalized_path, VFS_PATH_CAPACITY);
        {
            Status status = normalize_volume_relative_path(volume_suffix, normalized_path);

            if (status != StatusOK) {
                return status;
            }
        }

        *volume_out = &g_volume_table[volume_index];
        return StatusOK;
    }

    Status to_filesystem_node_type(VfsNodeType type, FilesystemNodeType* filesystem_type_out) {
        if (filesystem_type_out == NULL) {
            return StatusInvalidArgument;
        }

        if (type == VfsNodeTypeFile) {
            *filesystem_type_out = FilesystemNodeTypeFile;
            return StatusOK;
        }
        if (type == VfsNodeTypeDirectory) {
            *filesystem_type_out = FilesystemNodeTypeDirectory;
            return StatusOK;
        }

        return StatusInvalidArgument;
    }

    Status vfs_create_impl(const char* path, VfsNodeType type, VfsNode* node) {
        const VfsVolumeEntry* volume;
        char normalized_path[VFS_PATH_CAPACITY];
        FilesystemNodeType filesystem_type;
        FilesystemNode filesystem_node;
        Status status;

        status = resolve_filesystem_path(path, &volume, normalized_path);
        if (status != StatusOK) {
            return status;
        }
        if ((volume->mount.filesystem == NULL) || (volume->mount.filesystem->ops == NULL) || (volume->mount.filesystem->ops->create == NULL)) {
            return StatusNotSupported;
        }

        status = to_filesystem_node_type(type, &filesystem_type);
        if (status != StatusOK) {
            return status;
        }
        if (normalized_path[0] == '\0') {
            return StatusInvalidArgument;
        }

        memzero(&filesystem_node, sizeof(filesystem_node));
        status = volume->mount.filesystem->ops->create(volume->mount.filesystem_state, normalized_path, filesystem_type, &filesystem_node);
        if (status != StatusOK) {
            return status;
        }

        if (node != NULL) {
            populate_filesystem_node(node, volume, &filesystem_node);
        }

        return StatusOK;
    }

    Status vfs_remove_impl(const char* path) {
        const VfsVolumeEntry* volume;
        char normalized_path[VFS_PATH_CAPACITY];
        Status status = resolve_filesystem_path(path, &volume, normalized_path);

        if (status != StatusOK) {
            return status;
        }
        if ((volume->mount.filesystem == NULL) || (volume->mount.filesystem->ops == NULL) || (volume->mount.filesystem->ops->remove == NULL)) {
            return StatusNotSupported;
        }
        if (normalized_path[0] == '\0') {
            return StatusInvalidArgument;
        }

        return volume->mount.filesystem->ops->remove(volume->mount.filesystem_state, normalized_path);
    }

    SSize vfs_write_impl(VfsNode* node, U64 offset, const void* buffer, Size length) {
        if ((node == NULL) || ((buffer == NULL) && (length != 0U))) {
            return StatusInvalidArgument;
        }

        if (node->backend_kind == VfsBackendKindDevice) {
            if (node->device == NULL) {
                return StatusNotFound;
            }

            return node->device->write(offset, buffer, length);
        }

        if (node->backend_kind == VfsBackendKindFilesystem) {
            if ((node->filesystem == NULL) || (node->filesystem->ops == NULL) || (node->filesystem->ops->write == NULL)) {
                return StatusNotSupported;
            }

            {
                SSize written = node->filesystem->ops->write(node->filesystem_state, &node->backing_node, offset, buffer, length);

                if (written >= 0) {
                    vfs_file_cache_invalidate_all();
                    node->inode = node->backing_node.identifier;
                    node->size_bytes = node->backing_node.size_bytes;
                    node->flags = node->backing_node.mode;
                    if (node->backing_node.type == FilesystemNodeTypeDirectory) {
                        node->type = VfsNodeTypeDirectory;
                    }
                    else if (node->backing_node.type == FilesystemNodeTypeDevice) {
                        node->type = VfsNodeTypeDevice;
                    }
                    else {
                        node->type = VfsNodeTypeFile;
                    }
                }

                return written;
            }
        }

        return StatusNotSupported;
    }

    typedef struct VfsEnumerateBridgeContext {
        const VfsNode* directory_node;
        const VfsVolumeEntry* volume;
        void* user_context;
        VfsEnumerateVisitor visitor;
    } VfsEnumerateBridgeContext;

    Status vfs_enumerate_bridge(const char* name, const FilesystemNode* node, void* context) {
        VfsEnumerateBridgeContext* bridge = static_cast<VfsEnumerateBridgeContext*>(context);
        VfsNode child_node;

        if ((bridge == NULL) || (bridge->directory_node == NULL) || (bridge->volume == NULL) || (bridge->visitor == NULL) || (name == NULL) || (node == NULL)) {
            return StatusInvalidArgument;
        }

        populate_filesystem_node(&child_node, bridge->volume, node);
        return bridge->visitor(name, &child_node, bridge->user_context);
    }

    Status vfs_enumerate_impl(const VfsNode* node, void* context, VfsEnumerateVisitor visitor) {
        VfsEnumerateBridgeContext bridge = {};
        VfsVolumeEntry* volume = NULL;
        int volume_index;

        if ((node == NULL) || (visitor == NULL)) {
            return StatusInvalidArgument;
        }
        if ((node->backend_kind != VfsBackendKindFilesystem) || (node->type != VfsNodeTypeDirectory)) {
            return StatusInvalidArgument;
        }
        if ((node->filesystem == NULL) || (node->filesystem->ops == NULL) || (node->filesystem->ops->enumerate == NULL)) {
            return StatusNotSupported;
        }

        volume_index = volume_index_for_letter(node->volume_letter);
        if ((volume_index < 0) || !g_volume_table[volume_index].mounted) {
            return StatusNotFound;
        }

        volume = &g_volume_table[volume_index];
        bridge.directory_node = node;
        bridge.volume = volume;
        bridge.user_context = context;
        bridge.visitor = visitor;
        return node->filesystem->ops->enumerate(node->filesystem_state, const_cast<FilesystemNode*>(&node->backing_node), &bridge, &vfs_enumerate_bridge);
    }

} // namespace

Status VirtualFileSystem::init(void) {
    memzero(g_volume_table, sizeof(g_volume_table));
    memzero(g_device_aliases, sizeof(g_device_aliases));
    memzero(g_vfs_file_cache, sizeof(g_vfs_file_cache));
    g_vfs_file_cache_bytes = 0U;
    return StatusOK;
}

Status VirtualFileSystem::mount_root(const VfsMount* mount) {
    VfsMount root_mount;

    if (mount == NULL) {
        return StatusInvalidArgument;
    }

    root_mount = *mount;
    root_mount.drive_letter = 'C';
    return vfs_mount_volume_impl(&root_mount);
}

Status VirtualFileSystem::mount_volume(const VfsMount* mount) {
    return vfs_mount_volume_impl(mount);
}

Status VirtualFileSystem::register_device_name(const char* name, Device* device) {
    return vfs_register_device_name_impl(name, device);
}

Status VirtualFileSystem::resolve(const char* path, VfsNode* node) {
    return vfs_resolve_impl(path, node);
}

Status VirtualFileSystem::create(const char* path, VfsNodeType type, VfsNode* node) {
    Status status = vfs_create_impl(path, type, node);

    if (status == StatusOK) {
        vfs_file_cache_invalidate_all();
        // Publish the requested path directly so subscribers can filter on namespace intent without resolving the node again.
        KernelEventBroker::publish_vfs_event(KernelEventTypeFilesystemCreated, path, static_cast<U32>(type), status);
    }

    return status;
}

Status VirtualFileSystem::remove(const char* path) {
    Status status = vfs_remove_impl(path);

    if (status == StatusOK) {
        vfs_file_cache_invalidate_all();
        KernelEventBroker::publish_vfs_event(KernelEventTypeFilesystemRemoved, path, static_cast<U32>(VfsNodeTypeUnknown), status);
    }

    return status;
}

SSize VirtualFileSystem::read(const VfsNode* node, U64 offset, void* buffer, Size length) {
    return vfs_read_impl(node, offset, buffer, length);
}

SSize VirtualFileSystem::write(VfsNode* node, U64 offset, const void* buffer, Size length) {
    return vfs_write_impl(node, offset, buffer, length);
}

Status VirtualFileSystem::enumerate(const VfsNode* node, void* context, VfsEnumerateVisitor visitor) {
    return vfs_enumerate_impl(node, context, visitor);
}