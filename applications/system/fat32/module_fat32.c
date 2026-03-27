#include "app/fs_module.h"
#include "app/kernel_module.h"
#include "stdint.h"

extern int hal_partition_read(int id, int begin, int count, void* buf);
extern int hal_partition_write(int id, int begin, int count, const void* buf);
extern int hal_partition_find_first(int fs_type);

enum {
    HAL_PARTITION_TYPE_NONE = 0,
    HAL_PARTITION_TYPE_OTHER = 1,
    HAL_PARTITION_TYPE_FAT32 = 2,
};

enum {
    FAT32_ATTR_DIRECTORY = 0x10,
    FAT32_ATTR_VOLUME_ID = 0x08,
    FAT32_ATTR_LONG_NAME = 0x0F,
    FAT32_EOC = 0x0FFFFFF8U,
    FAT32_BAD = 0x0FFFFFF7U,
    FAT32_SECTOR_BYTES = 512U,
};

typedef struct __attribute__((packed)) Fat32BootSector {
    uint8_t jmp[3];
    uint8_t oemname[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t fat_count;
    uint16_t root_entry_count;
    uint16_t total_sector_16;
    uint8_t media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sector_count;
    uint32_t total_sector_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type_label[8];
} Fat32BootSector;

typedef struct __attribute__((packed)) Fat32DirEntry {
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t create_time_tenths;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_low;
    uint32_t size;
} Fat32DirEntry;

typedef struct {
    int partition_id;
    uint32_t reserved_sector_count;
    uint32_t fat_begin_sector;
    uint32_t data_begin_sector;
    uint32_t fat_size_sectors;
    uint32_t fat_count;
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_sector;
    uint32_t cluster_bytes;
    uint32_t ready;
} Fat32State;

static const RosKernelModuleApi* g_api;
static Fat32State g_state;

static const RosKernelModuleEmbeddedMetadata g_fat32_metadata ROS_KERNEL_MODULE_METADATA_SECTION = {
    ROS_KERNEL_MODULE_EMBEDDED_MAGIC,
    ROS_KERNEL_MODULE_EMBEDDED_VERSION,
    0,
    "fat32",
    "fat32_module_init",
    "",
    "",
    2,
    {
        ROS_KERNEL_MODULE_EXPORT("probe", fat32_module_probe),
        ROS_KERNEL_MODULE_EXPORT("call", fat32_module_call),
    },
};

static void mem_zero(void* ptr, uint64_t size) {
    uint8_t* current = (uint8_t*)ptr;
    while (size-- > 0) {
        *current++ = 0;
    }
}

static int string_length(const char* text) {
    int len = 0;
    if (!text) {
        return 0;
    }
    while (text[len] != '\0') {
        len++;
    }
    return len;
}

static char ascii_upper(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - ('a' - 'A'));
    }
    return ch;
}

static int path_is_absolute(const char* path) {
    return path && path[0] == '/';
}

static uint32_t fat32_first_cluster(const Fat32DirEntry* entry) {
    return ((uint32_t)entry->cluster_high << 16) | (uint32_t)entry->cluster_low;
}

static int fat32_read_sector(uint32_t relative_sector, void* buffer) {
    if (!g_state.ready) {
        return -1;
    }
    return hal_partition_read(g_state.partition_id, (int)relative_sector, 1, buffer) > 0 ? 0 : -1;
}

static int fat32_write_sector(uint32_t relative_sector, const void* buffer) {
    if (!g_state.ready) {
        return -1;
    }
    return hal_partition_write(g_state.partition_id, (int)relative_sector, 1, buffer) > 0 ? 0 : -1;
}

static uint32_t fat32_cluster_to_sector(uint32_t cluster) {
    return g_state.data_begin_sector + ((cluster - 2U) * g_state.sectors_per_cluster);
}

static uint32_t fat32_read_fat_entry(uint32_t cluster) {
    uint8_t sector[FAT32_SECTOR_BYTES];
    uint32_t fat_offset = cluster * 4U;
    uint32_t sector_index = g_state.fat_begin_sector + (fat_offset / FAT32_SECTOR_BYTES);
    uint32_t sector_offset = fat_offset % FAT32_SECTOR_BYTES;
    uint32_t value;

    if (fat32_read_sector(sector_index, sector) != 0) {
        return FAT32_BAD;
    }
    value = *(uint32_t*)(void*)(sector + sector_offset);
    return value & 0x0FFFFFFFU;
}

static int fat32_is_eoc(uint32_t cluster) {
    return cluster >= FAT32_EOC || cluster == 0;
}

typedef struct {
    uint32_t cluster;
    uint32_t sector_index;
    uint32_t entry_offset;
    Fat32DirEntry entry;
    int found;
    int has_free_slot;
    uint32_t free_cluster;
    uint32_t free_sector_index;
    uint32_t free_entry_offset;
} Fat32DirLocation;

static int fat32_split_parent_path(const char* path, char* parent, int parent_size, char* name, int name_size) {
    int length;
    int last_slash = -1;
    int index;

    if (!path || !parent || !name || parent_size <= 1 || name_size <= 1 || path[0] != '/') {
        return -1;
    }

    length = string_length(path);
    if (length == 1) {
        return -1;
    }
    for (index = 0; index < length; index++) {
        if (path[index] == '/') {
            last_slash = index;
        }
    }
    if (last_slash < 0 || last_slash >= length - 1) {
        return -1;
    }

    if (last_slash == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    }
    else {
        if (last_slash >= parent_size) {
            return -1;
        }
        for (index = 0; index < last_slash; index++) {
            parent[index] = path[index];
        }
        parent[last_slash] = '\0';
    }

    if (length - last_slash >= name_size) {
        return -1;
    }
    for (index = last_slash + 1; index < length; index++) {
        name[index - last_slash - 1] = path[index];
    }
    name[length - last_slash - 1] = '\0';
    return 0;
}

static int fat32_read_fat_entry_at_copy(uint32_t copy_index, uint32_t cluster) {
    uint8_t sector[FAT32_SECTOR_BYTES];
    uint32_t fat_offset = cluster * 4U;
    uint32_t sector_index = g_state.fat_begin_sector + (copy_index * g_state.fat_size_sectors) + (fat_offset / FAT32_SECTOR_BYTES);
    uint32_t sector_offset = fat_offset % FAT32_SECTOR_BYTES;
    uint32_t value;

    if (fat32_read_sector(sector_index, sector) != 0) {
        return FAT32_BAD;
    }
    value = *(uint32_t*)(void*)(sector + sector_offset);
    return (int)(value & 0x0FFFFFFFU);
}

static int fat32_write_fat_entry(uint32_t cluster, uint32_t value) {
    uint8_t sector[FAT32_SECTOR_BYTES];
    uint32_t fat_offset = cluster * 4U;
    uint32_t sector_offset = fat_offset % FAT32_SECTOR_BYTES;

    for (uint32_t copy_index = 0; copy_index < g_state.fat_count; copy_index++) {
        uint32_t sector_index = g_state.fat_begin_sector + (copy_index * g_state.fat_size_sectors) + (fat_offset / FAT32_SECTOR_BYTES);

        if (fat32_read_sector(sector_index, sector) != 0) {
            return -1;
        }
        *(uint32_t*)(void*)(sector + sector_offset) = value;
        if (fat32_write_sector(sector_index, sector) != 0) {
            return -1;
        }
    }

    return 0;
}

static int fat32_zero_cluster(uint32_t cluster) {
    uint8_t sector[FAT32_SECTOR_BYTES];
    uint32_t cluster_sector = fat32_cluster_to_sector(cluster);

    mem_zero(sector, sizeof(sector));
    for (uint32_t sector_index = 0; sector_index < g_state.sectors_per_cluster; sector_index++) {
        if (fat32_write_sector(cluster_sector + sector_index, sector) != 0) {
            return -1;
        }
    }

    return 0;
}

static int fat32_allocate_cluster(void) {
    uint8_t sector[FAT32_SECTOR_BYTES];

    for (uint32_t fat_sector = 0; fat_sector < g_state.fat_size_sectors; fat_sector++) {
        uint32_t sector_index = g_state.fat_begin_sector + fat_sector;

        if (fat32_read_sector(sector_index, sector) != 0) {
            return -1;
        }
        for (uint32_t entry_index = 0; entry_index < FAT32_SECTOR_BYTES / sizeof(uint32_t); entry_index++) {
            uint32_t cluster = (fat_sector * (FAT32_SECTOR_BYTES / sizeof(uint32_t))) + entry_index;

            if (cluster < 2U) {
                continue;
            }
            if (((uint32_t*)sector)[entry_index] != 0) {
                continue;
            }
            if (fat32_write_fat_entry(cluster, 0x0FFFFFFFU) != 0) {
                return -1;
            }
            if (fat32_zero_cluster(cluster) != 0) {
                return -1;
            }
            return (int)cluster;
        }
    }

    return -1;
}

static int fat32_free_chain(uint32_t cluster) {
    while (!fat32_is_eoc(cluster)) {
        uint32_t next = (uint32_t)fat32_read_fat_entry_at_copy(0, cluster);

        if (fat32_write_fat_entry(cluster, 0) != 0) {
            return -1;
        }
        if (next == FAT32_BAD) {
            return -1;
        }
        cluster = next;
    }

    return 0;
}

static int fat32_scan_directory_location(uint32_t directory_cluster, const uint8_t target_name[11], Fat32DirLocation* location) {
    uint8_t sector[FAT32_SECTOR_BYTES];
    uint32_t cluster = directory_cluster;

    location->found = 0;
    location->has_free_slot = 0;

    while (!fat32_is_eoc(cluster)) {
        uint32_t cluster_sector = fat32_cluster_to_sector(cluster);

        for (uint32_t sector_index = 0; sector_index < g_state.sectors_per_cluster; sector_index++) {
            if (fat32_read_sector(cluster_sector + sector_index, sector) != 0) {
                return -1;
            }
            for (uint32_t offset = 0; offset < FAT32_SECTOR_BYTES; offset += sizeof(Fat32DirEntry)) {
                Fat32DirEntry* entry = (Fat32DirEntry*)(void*)(sector + offset);
                int match = 1;

                if (entry->name[0] == 0x00) {
                    if (!location->has_free_slot) {
                        location->has_free_slot = 1;
                        location->free_cluster = cluster;
                        location->free_sector_index = sector_index;
                        location->free_entry_offset = offset;
                    }
                    return 0;
                }
                if (entry->name[0] == 0xE5) {
                    if (!location->has_free_slot) {
                        location->has_free_slot = 1;
                        location->free_cluster = cluster;
                        location->free_sector_index = sector_index;
                        location->free_entry_offset = offset;
                    }
                    continue;
                }
                if (entry->attr == FAT32_ATTR_LONG_NAME || entry->attr == FAT32_ATTR_VOLUME_ID) {
                    continue;
                }
                for (int index = 0; index < 11; index++) {
                    if (entry->name[index] != target_name[index]) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    location->found = 1;
                    location->cluster = cluster;
                    location->sector_index = sector_index;
                    location->entry_offset = offset;
                    location->entry = *entry;
                    return 1;
                }
            }
        }

        cluster = (uint32_t)fat32_read_fat_entry_at_copy(0, cluster);
        if (cluster == FAT32_BAD) {
            return -1;
        }
    }

    return 0;
}

static int fat32_write_directory_entry(uint32_t cluster, uint32_t sector_index, uint32_t offset, const Fat32DirEntry* entry) {
    uint8_t sector[FAT32_SECTOR_BYTES];
    uint32_t sector_va = fat32_cluster_to_sector(cluster) + sector_index;

    if (fat32_read_sector(sector_va, sector) != 0) {
        return -1;
    }
    *(Fat32DirEntry*)(void*)(sector + offset) = *entry;
    if (fat32_write_sector(sector_va, sector) != 0) {
        return -1;
    }
    return 0;
}

static int fat32_format_short_name(const char* component, uint8_t out_name[11]) {
    int index;
    int dot_index = -1;
    int length;

    if (!component || component[0] == '\0') {
        return -1;
    }
    for (index = 0; index < 11; index++) {
        out_name[index] = ' ';
    }

    length = string_length(component);
    for (index = 0; index < length; index++) {
        if (component[index] == '.') {
            dot_index = index;
            break;
        }
    }

    if (dot_index < 0) {
        dot_index = length;
    }
    if (dot_index == 0 || dot_index > 8) {
        return -1;
    }
    for (index = 0; index < dot_index; index++) {
        out_name[index] = (uint8_t)ascii_upper(component[index]);
    }

    if (dot_index < length) {
        int ext_length = length - dot_index - 1;

        if (ext_length <= 0 || ext_length > 3) {
            return -1;
        }
        for (index = 0; index < ext_length; index++) {
            out_name[8 + index] = (uint8_t)ascii_upper(component[dot_index + 1 + index]);
        }
    }

    return 0;
}

static void fat32_short_name_to_text(const uint8_t name[11], char* out, uint64_t out_size) {
    uint64_t used = 0;
    int index;
    int has_extension = 0;

    if (!out || out_size == 0) {
        return;
    }

    for (index = 8; index < 11; index++) {
        if (name[index] != ' ') {
            has_extension = 1;
            break;
        }
    }

    for (index = 0; index < 8 && used + 1 < out_size; index++) {
        if (name[index] == ' ') {
            break;
        }
        out[used++] = (char)name[index];
    }
    if (has_extension && used + 1 < out_size) {
        out[used++] = '.';
        for (index = 8; index < 11 && used + 1 < out_size; index++) {
            if (name[index] == ' ') {
                break;
            }
            out[used++] = (char)name[index];
        }
    }
    out[used] = '\0';
}

static int fat32_find_directory_entry(uint32_t directory_cluster, const uint8_t target_name[11], Fat32DirEntry* out_entry) {
    uint8_t sector[FAT32_SECTOR_BYTES];
    uint32_t cluster = directory_cluster;

    while (!fat32_is_eoc(cluster)) {
        uint32_t cluster_sector = fat32_cluster_to_sector(cluster);
        for (uint32_t sector_offset = 0; sector_offset < g_state.sectors_per_cluster; sector_offset++) {
            if (fat32_read_sector(cluster_sector + sector_offset, sector) != 0) {
                return -1;
            }
            for (uint32_t offset = 0; offset < FAT32_SECTOR_BYTES; offset += sizeof(Fat32DirEntry)) {
                Fat32DirEntry* entry = (Fat32DirEntry*)(void*)(sector + offset);
                int match = 1;

                if (entry->name[0] == 0x00) {
                    return 0;
                }
                if (entry->name[0] == 0xE5 || entry->attr == FAT32_ATTR_LONG_NAME || entry->attr == FAT32_ATTR_VOLUME_ID) {
                    continue;
                }
                for (int index = 0; index < 11; index++) {
                    if (entry->name[index] != target_name[index]) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    if (out_entry) {
                        *out_entry = *entry;
                    }
                    return 1;
                }
            }
        }

        cluster = fat32_read_fat_entry(cluster);
        if (cluster == FAT32_BAD) {
            return -1;
        }
    }

    return 0;
}

static int fat32_resolve_path(const char* path, Fat32DirEntry* out_entry) {
    uint32_t current_cluster;
    const char* current;

    if (!path_is_absolute(path)) {
        return -1;
    }
    if (path[1] == '\0') {
        mem_zero(out_entry, sizeof(*out_entry));
        out_entry->attr = FAT32_ATTR_DIRECTORY;
        out_entry->cluster_low = (uint16_t)(g_state.root_cluster & 0xFFFFU);
        out_entry->cluster_high = (uint16_t)((g_state.root_cluster >> 16) & 0xFFFFU);
        return 1;
    }

    current_cluster = g_state.root_cluster;
    current = path + 1;
    while (*current != '\0') {
        char component[16];
        uint8_t short_name[11];
        int comp_len = 0;
        Fat32DirEntry entry;

        while (*current != '\0' && *current != '/') {
            if (comp_len + 1 >= (int)sizeof(component)) {
                return -1;
            }
            component[comp_len++] = *current++;
        }
        component[comp_len] = '\0';
        if (fat32_format_short_name(component, short_name) != 0) {
            return -1;
        }
        if (fat32_find_directory_entry(current_cluster, short_name, &entry) <= 0) {
            return -2;
        }
        if (*current == '\0') {
            if (out_entry) {
                *out_entry = entry;
            }
            return 1;
        }
        if ((entry.attr & FAT32_ATTR_DIRECTORY) == 0) {
            return -1;
        }
        current_cluster = fat32_first_cluster(&entry);
        if (current_cluster == 0) {
            current_cluster = g_state.root_cluster;
        }
        current++;
    }

    return -1;
}

static int fat32_allocate_cluster_chain(uint32_t cluster_count, uint32_t* first_cluster) {
    uint32_t previous_cluster = 0;

    *first_cluster = 0;
    for (uint32_t index = 0; index < cluster_count; index++) {
        int cluster = fat32_allocate_cluster();

        if (cluster < 0) {
            if (*first_cluster != 0) {
                fat32_free_chain(*first_cluster);
            }
            return -1;
        }
        if (*first_cluster == 0) {
            *first_cluster = (uint32_t)cluster;
        }
        if (previous_cluster != 0 && fat32_write_fat_entry(previous_cluster, (uint32_t)cluster) != 0) {
            fat32_free_chain(*first_cluster);
            return -1;
        }
        previous_cluster = (uint32_t)cluster;
    }

    return 0;
}

static int fat32_write_cluster_data(uint32_t first_cluster, const char* buffer, uint64_t size) {
    uint32_t cluster = first_cluster;
    uint64_t written = 0;
    uint8_t sector[FAT32_SECTOR_BYTES];

    while (written < size && !fat32_is_eoc(cluster)) {
        uint32_t cluster_sector = fat32_cluster_to_sector(cluster);

        for (uint32_t sector_index = 0; sector_index < g_state.sectors_per_cluster && written < size; sector_index++) {
            uint64_t chunk = size - written;

            if (chunk > FAT32_SECTOR_BYTES) {
                chunk = FAT32_SECTOR_BYTES;
            }

            if (fat32_read_sector(cluster_sector + sector_index, sector) != 0) {
                return -1;
            }
            for (uint64_t index = 0; index < chunk; index++) {
                sector[index] = (uint8_t)buffer[written + index];
            }
            if (fat32_write_sector(cluster_sector + sector_index, sector) != 0) {
                return -1;
            }

            written += chunk;
        }

        cluster = (uint32_t)fat32_read_fat_entry_at_copy(0, cluster);
    }

    return written == size ? 0 : -1;
}

static long fat32_write_file_request(const FsModuleWriteFileRequest* request) {
    char parent_path[64];
    char file_name[16];
    uint8_t short_name[11];
    Fat32DirEntry parent_entry;
    Fat32DirLocation location;
    Fat32DirEntry new_entry;
    uint32_t parent_cluster;
    uint32_t first_cluster = 0;
    uint32_t cluster_count;
    int status;

    if (!request || !request->path || !request->buffer) {
        return -1;
    }
    if (g_api && g_api->logf) {
        g_api->logf("INFO", "fat32", "write request path=%s size=%lu", request->path, (unsigned long)request->size);
    }
    if (fat32_split_parent_path(request->path, parent_path, sizeof(parent_path), file_name, sizeof(file_name)) != 0) {
        if (g_api && g_api->logf) {
            g_api->logf("WARN", "fat32", "write split parent failed for %s", request->path);
        }
        return -1;
    }
    if (fat32_format_short_name(file_name, short_name) != 0) {
        if (g_api && g_api->logf) {
            g_api->logf("WARN", "fat32", "write short-name format failed for %s", file_name);
        }
        return -1;
    }
    if (fat32_resolve_path(parent_path, &parent_entry) <= 0) {
        if (g_api && g_api->logf) {
            g_api->logf("WARN", "fat32", "write parent resolve failed for %s", parent_path);
        }
        return -2;
    }
    parent_cluster = fat32_first_cluster(&parent_entry);
    if (parent_cluster == 0) {
        parent_cluster = g_state.root_cluster;
    }

    status = fat32_scan_directory_location(parent_cluster, short_name, &location);
    if (status < 0) {
        if (g_api && g_api->logf) {
            g_api->logf("WARN", "fat32", "write directory scan failed for %s", request->path);
        }
        return -1;
    }

    if (location.found) {
        if (location.entry.attr & FAT32_ATTR_DIRECTORY) {
            return -1;
        }
        if (location.entry.cluster_low != 0 || location.entry.cluster_high != 0) {
            if (fat32_free_chain(fat32_first_cluster(&location.entry)) != 0) {
                return -1;
            }
        }
    }
    else if (!location.has_free_slot) {
        return -1;
    }

    cluster_count = (uint32_t)((request->size + g_state.cluster_bytes - 1U) / g_state.cluster_bytes);
    if (cluster_count == 0) {
        cluster_count = 1;
    }
    if (fat32_allocate_cluster_chain(cluster_count, &first_cluster) != 0) {
        if (g_api && g_api->logf) {
            g_api->logf("WARN", "fat32", "write cluster allocation failed for %s", request->path);
        }
        return -1;
    }
    if (fat32_write_cluster_data(first_cluster, request->buffer, request->size) != 0) {
        if (g_api && g_api->logf) {
            g_api->logf("WARN", "fat32", "write data commit failed for %s", request->path);
        }
        fat32_free_chain(first_cluster);
        return -1;
    }

    mem_zero(&new_entry, sizeof(new_entry));
    for (int index = 0; index < 11; index++) {
        new_entry.name[index] = short_name[index];
    }
    new_entry.attr = 0;
    new_entry.cluster_low = (uint16_t)(first_cluster & 0xFFFFU);
    new_entry.cluster_high = (uint16_t)((first_cluster >> 16) & 0xFFFFU);
    new_entry.size = (uint32_t)request->size;

    if (fat32_write_directory_entry(parent_cluster,
        location.found ? location.sector_index : location.free_sector_index,
        location.found ? location.entry_offset : location.free_entry_offset,
        &new_entry) != 0) {
        if (g_api && g_api->logf) {
            g_api->logf("WARN", "fat32", "write directory entry commit failed for %s", request->path);
        }
        fat32_free_chain(first_cluster);
        return -1;
    }

    return (long)request->size;
}

static long fat32_mkdir_request(const FsModulePathRequest* request) {
    (void)request;
    return -1;
}

static long fat32_remove_request(const FsModulePathRequest* request) {
    char parent_path[64];
    char file_name[16];
    uint8_t short_name[11];
    Fat32DirEntry parent_entry;
    Fat32DirLocation location;
    uint32_t parent_cluster;
    Fat32DirEntry deleted_entry;

    if (!request || !request->path) {
        return -1;
    }
    if (g_api && g_api->logf) {
        g_api->logf("INFO", "fat32", "remove request path=%s", request->path);
    }
    if (fat32_split_parent_path(request->path, parent_path, sizeof(parent_path), file_name, sizeof(file_name)) != 0) {
        return -1;
    }
    if (fat32_format_short_name(file_name, short_name) != 0) {
        return -1;
    }
    if (fat32_resolve_path(parent_path, &parent_entry) <= 0) {
        return -2;
    }
    parent_cluster = fat32_first_cluster(&parent_entry);
    if (parent_cluster == 0) {
        parent_cluster = g_state.root_cluster;
    }

    if (fat32_scan_directory_location(parent_cluster, short_name, &location) <= 0 || !location.found) {
        return -2;
    }
    if (location.entry.attr & FAT32_ATTR_DIRECTORY) {
        return -1;
    }
    if (location.entry.cluster_low != 0 || location.entry.cluster_high != 0) {
        if (fat32_free_chain(fat32_first_cluster(&location.entry)) != 0) {
            return -1;
        }
    }

    deleted_entry = location.entry;
    deleted_entry.name[0] = 0xE5;
    deleted_entry.size = 0;
    deleted_entry.cluster_low = 0;
    deleted_entry.cluster_high = 0;
    return fat32_write_directory_entry(parent_cluster, location.sector_index, location.entry_offset, &deleted_entry);
}

static long fat32_read_file_request(const FsModuleReadFileRequest* request) {
    Fat32DirEntry entry;
    uint32_t cluster;
    uint64_t offset_remaining;
    uint64_t bytes_remaining;
    uint8_t sector[FAT32_SECTOR_BYTES];
    char* dest;
    uint32_t file_size;

    if (!request || !request->path || !request->buffer || request->size == 0) {
        return -1;
    }
    if (fat32_resolve_path(request->path, &entry) <= 0) {
        return -2;
    }
    if (entry.attr & FAT32_ATTR_DIRECTORY) {
        return -1;
    }

    file_size = entry.size;
    if (request->offset >= file_size) {
        return 0;
    }

    cluster = fat32_first_cluster(&entry);
    offset_remaining = request->offset;
    bytes_remaining = request->size;
    if (bytes_remaining > (uint64_t)(file_size - request->offset)) {
        bytes_remaining = (uint64_t)(file_size - request->offset);
    }
    dest = request->buffer;

    while (offset_remaining >= g_state.cluster_bytes && !fat32_is_eoc(cluster)) {
        offset_remaining -= g_state.cluster_bytes;
        cluster = fat32_read_fat_entry(cluster);
        if (cluster == FAT32_BAD) {
            return -1;
        }
    }

    while (bytes_remaining > 0 && !fat32_is_eoc(cluster)) {
        uint32_t cluster_sector = fat32_cluster_to_sector(cluster);
        uint64_t cluster_offset = offset_remaining;

        for (uint32_t sector_index = 0; sector_index < g_state.sectors_per_cluster && bytes_remaining > 0; sector_index++) {
            uint64_t sector_offset = cluster_offset;
            uint64_t chunk;

            if (sector_offset >= FAT32_SECTOR_BYTES) {
                cluster_offset -= FAT32_SECTOR_BYTES;
                continue;
            }
            if (fat32_read_sector(cluster_sector + sector_index, sector) != 0) {
                return -1;
            }
            chunk = FAT32_SECTOR_BYTES - sector_offset;
            if (chunk > bytes_remaining) {
                chunk = bytes_remaining;
            }
            for (uint64_t index = 0; index < chunk; index++) {
                dest[index] = (char)sector[sector_offset + index];
            }
            dest += chunk;
            bytes_remaining -= chunk;
            cluster_offset = 0;
        }

        offset_remaining = 0;
        cluster = fat32_read_fat_entry(cluster);
        if (cluster == FAT32_BAD) {
            return -1;
        }
    }

    return (long)((uint64_t)(dest - request->buffer));
}

static long fat32_dir_entry_request(const FsModuleDirEntryRequest* request) {
    Fat32DirEntry directory;
    uint8_t sector[FAT32_SECTOR_BYTES];
    uint32_t cluster;
    uint64_t visible_index = 0;

    if (!request || !request->path || !request->entry) {
        return -1;
    }

    if (request->path[0] == '/' && request->path[1] == '\0') {
        mem_zero(&directory, sizeof(directory));
        directory.attr = FAT32_ATTR_DIRECTORY;
        directory.cluster_low = (uint16_t)(g_state.root_cluster & 0xFFFFU);
        directory.cluster_high = (uint16_t)((g_state.root_cluster >> 16) & 0xFFFFU);
    }
    else if (fat32_resolve_path(request->path, &directory) <= 0) {
        return -2;
    }

    if ((directory.attr & FAT32_ATTR_DIRECTORY) == 0) {
        return -1;
    }

    cluster = fat32_first_cluster(&directory);
    if (cluster == 0) {
        cluster = g_state.root_cluster;
    }

    while (!fat32_is_eoc(cluster)) {
        uint32_t cluster_sector = fat32_cluster_to_sector(cluster);
        for (uint32_t sector_index = 0; sector_index < g_state.sectors_per_cluster; sector_index++) {
            if (fat32_read_sector(cluster_sector + sector_index, sector) != 0) {
                return -1;
            }
            for (uint32_t offset = 0; offset < FAT32_SECTOR_BYTES; offset += sizeof(Fat32DirEntry)) {
                Fat32DirEntry* entry = (Fat32DirEntry*)(void*)(sector + offset);

                if (entry->name[0] == 0x00) {
                    return 0;
                }
                if (entry->name[0] == 0xE5 || entry->attr == FAT32_ATTR_LONG_NAME || entry->attr == FAT32_ATTR_VOLUME_ID) {
                    continue;
                }
                if (visible_index++ != request->entry_index) {
                    continue;
                }

                request->entry->name[0] = '\0';
                request->entry->size = entry->size;
                request->entry->attr = entry->attr;
                fat32_short_name_to_text(entry->name, request->entry->name, sizeof(request->entry->name));
                return 1;
            }
        }
        cluster = fat32_read_fat_entry(cluster);
        if (cluster == FAT32_BAD) {
            return -1;
        }
    }

    return 0;
}

int fat32_module_init(const RosKernelModuleApi* api) {
    uint8_t boot_sector_bytes[FAT32_SECTOR_BYTES];
    const Fat32BootSector* boot_sector;
    int partition_id;
    uint32_t fat_begin;
    uint32_t data_begin;

    g_api = api;
    mem_zero(&g_state, sizeof(g_state));

    partition_id = hal_partition_find_first(HAL_PARTITION_TYPE_FAT32);
    if (partition_id < 0) {
        if (g_api && g_api->logf) {
            g_api->logf("WARN", "fat32", "no FAT32 partition found");
        }
        return -1;
    }
    if (hal_partition_read(partition_id, 0, 1, boot_sector_bytes) <= 0) {
        return -1;
    }
    boot_sector = (const Fat32BootSector*)(const void*)boot_sector_bytes;
    if (boot_sector->bytes_per_sector != FAT32_SECTOR_BYTES || boot_sector->sectors_per_cluster == 0 || boot_sector->fat_size_32 == 0) {
        return -1;
    }

    fat_begin = boot_sector->reserved_sector_count;
    data_begin = boot_sector->reserved_sector_count + ((uint32_t)boot_sector->fat_count * boot_sector->fat_size_32);

    g_state.partition_id = partition_id;
    g_state.reserved_sector_count = boot_sector->reserved_sector_count;
    g_state.fat_begin_sector = fat_begin;
    g_state.data_begin_sector = data_begin;
    g_state.fat_size_sectors = boot_sector->fat_size_32;
    g_state.fat_count = boot_sector->fat_count;
    g_state.root_cluster = boot_sector->root_cluster;
    g_state.sectors_per_cluster = boot_sector->sectors_per_cluster;
    g_state.bytes_per_sector = boot_sector->bytes_per_sector;
    g_state.cluster_bytes = (uint32_t)boot_sector->bytes_per_sector * (uint32_t)boot_sector->sectors_per_cluster;
    g_state.ready = 1;

    if (g_api && g_api->logf) {
        g_api->logf("INFO", "fat32", "mounted partition=%d root_cluster=%lu", partition_id, (unsigned long)g_state.root_cluster);
    }
    return 0;
}

long fat32_module_probe(unsigned long unused0, unsigned long unused1) {
    (void)unused0;
    (void)unused1;
    return g_state.ready ? 1 : 0;
}

long fat32_module_call(unsigned long operation, unsigned long request_ptr) {
    if (!g_state.ready || request_ptr == 0) {
        return -1;
    }

    switch (operation) {
    case FS_MODULE_OP_READ_FILE:
        return fat32_read_file_request((const FsModuleReadFileRequest*)request_ptr);
    case FS_MODULE_OP_DIR_ENTRY:
        return fat32_dir_entry_request((const FsModuleDirEntryRequest*)request_ptr);
    case FS_MODULE_OP_WRITE_FILE:
        return fat32_write_file_request((const FsModuleWriteFileRequest*)request_ptr);
    case FS_MODULE_OP_MKDIR:
        return fat32_mkdir_request((const FsModulePathRequest*)request_ptr);
    case FS_MODULE_OP_REMOVE:
        return fat32_remove_request((const FsModulePathRequest*)request_ptr);
    default:
        return -1;
    }
}
