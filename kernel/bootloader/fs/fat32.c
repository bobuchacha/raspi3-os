#include "fat32.h"

#include "block_device.h"
#include "console.h"

#define FAT32_ATTRIBUTE_VOLUME_ID 0x08U
#define FAT32_ATTRIBUTE_LONG_NAME 0x0FU
#define FAT32_CLUSTER_END 0x0FFFFFF8U
#define FAT32_DIR_ENTRY_SIZE 32U
#define FAT32_PARTITION_FAT32_CHS 0x0BU
#define FAT32_PARTITION_FAT32_LBA 0x0CU

#define FAT32_BOOT_BYTES_PER_SECTOR_OFFSET 11U
#define FAT32_BOOT_SECTORS_PER_CLUSTER_OFFSET 13U
#define FAT32_BOOT_RESERVED_SECTORS_OFFSET 14U
#define FAT32_BOOT_FAT_COUNT_OFFSET 16U
#define FAT32_BOOT_FAT_SIZE_32_OFFSET 36U
#define FAT32_BOOT_ROOT_CLUSTER_OFFSET 44U
#define FAT32_BOOT_FS_TYPE_OFFSET 82U

#define FAT32_DIR_ATTRIBUTES_OFFSET 11U
#define FAT32_DIR_CLUSTER_HI_OFFSET 20U
#define FAT32_DIR_CLUSTER_LO_OFFSET 26U
#define FAT32_DIR_SIZE_OFFSET 28U

#define FAT32_MBR_ENTRY_SIZE 16U
#define FAT32_MBR_ENTRY_TYPE_OFFSET 4U
#define FAT32_MBR_ENTRY_LBA_OFFSET 8U

static U8 g_sector_buffer[512];

static U16 boot_read_le16(const U8* bytes) {
    return (U16)bytes[0] | ((U16)bytes[1] << 8);
}

static U32 boot_read_le32(const U8* bytes) {
    return (U32)bytes[0] |
           ((U32)bytes[1] << 8) |
           ((U32)bytes[2] << 16) |
           ((U32)bytes[3] << 24);
}

static void bootfs_parse_boot_sector(BootFsFat32BootSector* boot_sector, const U8* sector) {
    int index;

    boot_sector->bytes_per_sector = boot_read_le16(sector + FAT32_BOOT_BYTES_PER_SECTOR_OFFSET);
    boot_sector->sectors_per_cluster = sector[FAT32_BOOT_SECTORS_PER_CLUSTER_OFFSET];
    boot_sector->reserved_sectors = boot_read_le16(sector + FAT32_BOOT_RESERVED_SECTORS_OFFSET);
    boot_sector->fat_count = sector[FAT32_BOOT_FAT_COUNT_OFFSET];
    boot_sector->fat_size_32 = boot_read_le32(sector + FAT32_BOOT_FAT_SIZE_32_OFFSET);
    boot_sector->root_cluster = boot_read_le32(sector + FAT32_BOOT_ROOT_CLUSTER_OFFSET);
    for (index = 0; index < 8; ++index) {
        boot_sector->fs_type[index] = (char)sector[FAT32_BOOT_FS_TYPE_OFFSET + index];
    }
}

static void bootfs_parse_dir_entry(BootFsFat32DirEntry* entry, const U8* bytes) {
    int index;

    for (index = 0; index < 11; ++index) {
        entry->name[index] = bytes[index];
    }
    entry->attributes = bytes[FAT32_DIR_ATTRIBUTES_OFFSET];
    entry->cluster_hi = boot_read_le16(bytes + FAT32_DIR_CLUSTER_HI_OFFSET);
    entry->cluster_lo = boot_read_le16(bytes + FAT32_DIR_CLUSTER_LO_OFFSET);
    entry->size = boot_read_le32(bytes + FAT32_DIR_SIZE_OFFSET);
}

static int boot_char_upper(int ch) {
    if ((ch >= 'a') && (ch <= 'z')) {
        return ch - ('a' - 'A');
    }

    return ch;
}

static int boot_memcmp(const void* lhs, const void* rhs, Size size) {
    const U8* left = (const U8*)lhs;
    const U8* right = (const U8*)rhs;
    Size index;

    for (index = 0; index < size; ++index) {
        if (left[index] != right[index]) {
            return (int)left[index] - (int)right[index];
        }
    }

    return 0;
}

static int boot_strcmp(const char* lhs, const char* rhs) {
    while ((*lhs != '\0') && (*rhs != '\0') && (*lhs == *rhs)) {
        ++lhs;
        ++rhs;
    }

    return (int)(U8)*lhs - (int)(U8)*rhs;
}

static Status bootfs_read_sector(U32 lba, void* buffer) {
    return boot_block_read(lba, 1U, buffer);
}

static bool bootfs_is_fat32_boot_sector(const U8* sector) {
    return (sector[510] == 0x55U) &&
           (sector[511] == 0xAAU) &&
           (boot_read_le16(sector + FAT32_BOOT_BYTES_PER_SECTOR_OFFSET) == 512U) &&
           (sector[FAT32_BOOT_SECTORS_PER_CLUSTER_OFFSET] != 0U) &&
           (sector[FAT32_BOOT_FAT_COUNT_OFFSET] != 0U) &&
           (boot_read_le32(sector + FAT32_BOOT_FAT_SIZE_32_OFFSET) != 0U) &&
           (boot_read_le32(sector + FAT32_BOOT_ROOT_CLUSTER_OFFSET) >= 2U) &&
           (boot_memcmp(sector + FAT32_BOOT_FS_TYPE_OFFSET, "FAT32", 5U) == 0);
}

static Status bootfs_find_partition_lba(U32* partition_lba_out) {
    int index;

    if (partition_lba_out == NULL) {
        return StatusInvalidArgument;
    }
    if (bootfs_read_sector(0U, g_sector_buffer) != StatusOK) {
        return StatusIoError;
    }
    if (bootfs_is_fat32_boot_sector(g_sector_buffer)) {
        *partition_lba_out = 0U;
        return StatusOK;
    }

    for (index = 0; index < 4; ++index) {
        const U8* entry = g_sector_buffer + 0x1BE + ((Size)index * FAT32_MBR_ENTRY_SIZE);
        U8 type = entry[FAT32_MBR_ENTRY_TYPE_OFFSET];

        if ((type == FAT32_PARTITION_FAT32_CHS) || (type == FAT32_PARTITION_FAT32_LBA)) {
            *partition_lba_out = boot_read_le32(entry + FAT32_MBR_ENTRY_LBA_OFFSET);
            return StatusOK;
        }
    }

    return StatusNotFound;
}

static U32 bootfs_cluster_to_lba(const BootFs* filesystem, U32 cluster) {
    return filesystem->data_lba + ((cluster - 2U) * filesystem->boot_sector.sectors_per_cluster);
}

static Status bootfs_read_fat_entry(const BootFs* filesystem, U32 cluster, U32* next_cluster_out) {
    U32 fat_offset;
    U32 sector;
    U32 offset;
    U32 value;

    if ((filesystem == NULL) || (next_cluster_out == NULL)) {
        return StatusInvalidArgument;
    }

    fat_offset = cluster * 4U;
    sector = filesystem->fat_lba + (fat_offset / 512U);
    offset = fat_offset % 512U;

    if (bootfs_read_sector(sector, g_sector_buffer) != StatusOK) {
        return StatusIoError;
    }

    value = ((U32)g_sector_buffer[offset]) |
            ((U32)g_sector_buffer[offset + 1U] << 8) |
            ((U32)g_sector_buffer[offset + 2U] << 16) |
            ((U32)g_sector_buffer[offset + 3U] << 24);
    *next_cluster_out = value & 0x0FFFFFFFU;
    return StatusOK;
}

static Status bootfs_make_short_name(const char* path, char short_name[11]) {
    const char* cursor = path;
    const char* dot = NULL;
    int name_len;
    int ext_len = 0;
    int index;

    if ((path == NULL) || (short_name == NULL) || (path[0] != '/')) {
        return StatusInvalidArgument;
    }

    cursor++;
    if (*cursor == '\0') {
        return StatusInvalidArgument;
    }

    while (*cursor != '\0') {
        if (*cursor == '/') {
            return StatusNotSupported;
        }
        if (*cursor == '.') {
            if (dot != NULL) {
                return StatusInvalidArgument;
            }
            dot = cursor;
        }
        ++cursor;
    }

    memzero(short_name, 11U);
    for (index = 0; index < 11; ++index) {
        short_name[index] = ' ';
    }

    if (dot != NULL) {
        name_len = (int)(dot - (path + 1));
        ext_len = (int)(cursor - dot - 1);
    }
    else {
        name_len = (int)(cursor - (path + 1));
    }

    if ((name_len <= 0) || (name_len > 8) || (ext_len > 3)) {
        return StatusInvalidArgument;
    }

    for (index = 0; index < name_len; ++index) {
        short_name[index] = (char)boot_char_upper(path[1 + index]);
    }
    for (index = 0; index < ext_len; ++index) {
        short_name[8 + index] = (char)boot_char_upper(dot[1 + index]);
    }

    return StatusOK;
}

static Status bootfs_find_root_entry(BootFs* filesystem, const char short_name[11], BootFsFat32DirEntry* entry_out) {
    U32 cluster;

    if ((filesystem == NULL) || (short_name == NULL) || (entry_out == NULL)) {
        return StatusInvalidArgument;
    }

    cluster = filesystem->boot_sector.root_cluster;
    while ((cluster >= 2U) && (cluster < FAT32_CLUSTER_END)) {
        U32 offset = 0U;

        while (offset < filesystem->cluster_size) {
            U32 sector_lba = bootfs_cluster_to_lba(filesystem, cluster) + (offset / 512U);
            U32 sector_offset = offset % 512U;
            const U8* entry_bytes;

            if (bootfs_read_sector(sector_lba, g_sector_buffer) != StatusOK) {
                return StatusIoError;
            }

            entry_bytes = g_sector_buffer + sector_offset;
            if (entry_bytes[0] == 0x00U) {
                return StatusNotFound;
            }
            if ((entry_bytes[0] != 0xE5U) &&
                (entry_bytes[FAT32_DIR_ATTRIBUTES_OFFSET] != FAT32_ATTRIBUTE_LONG_NAME) &&
                ((entry_bytes[FAT32_DIR_ATTRIBUTES_OFFSET] & FAT32_ATTRIBUTE_VOLUME_ID) == 0U) &&
                (boot_memcmp(entry_bytes, short_name, 11U) == 0)) {
                bootfs_parse_dir_entry(entry_out, entry_bytes);
                return StatusOK;
            }

            offset += FAT32_DIR_ENTRY_SIZE;
        }

        if (bootfs_read_fat_entry(filesystem, cluster, &cluster) != StatusOK) {
            return StatusIoError;
        }
    }

    return StatusNotFound;
}

static Status bootfs_locate_cluster(BootFs* filesystem, BootFsFile* file, U32 offset, U32* cluster_out) {
    U32 target_index;
    U32 cluster;
    U32 cluster_index;

    if ((filesystem == NULL) || (file == NULL) || (cluster_out == NULL)) {
        return StatusInvalidArgument;
    }

    target_index = offset / filesystem->cluster_size;
    cluster = file->current_cluster;
    cluster_index = file->current_cluster_index;

    if ((cluster == 0U) || (target_index < cluster_index)) {
        cluster = file->first_cluster;
        cluster_index = 0U;
    }

    while (cluster_index < target_index) {
        if (bootfs_read_fat_entry(filesystem, cluster, &cluster) != StatusOK) {
            return StatusIoError;
        }
        if ((cluster < 2U) || (cluster >= FAT32_CLUSTER_END)) {
            return StatusNotFound;
        }
        ++cluster_index;
    }

    file->current_cluster = cluster;
    file->current_cluster_index = cluster_index;
    *cluster_out = cluster;
    return StatusOK;
}

Status bootfs_init(BootFs* filesystem) {
    U32 partition_lba;

    if (filesystem == NULL) {
        return StatusInvalidArgument;
    }

    if (bootfs_find_partition_lba(&partition_lba) != StatusOK) {
        return StatusNotFound;
    }
    if (bootfs_read_sector(partition_lba, g_sector_buffer) != StatusOK) {
        return StatusIoError;
    }
    if (!bootfs_is_fat32_boot_sector(g_sector_buffer)) {
        return StatusNotSupported;
    }

    bootfs_parse_boot_sector(&filesystem->boot_sector, g_sector_buffer);
    filesystem->partition_lba = partition_lba;
    filesystem->fat_lba = partition_lba + filesystem->boot_sector.reserved_sectors;
    filesystem->data_lba = filesystem->fat_lba + (filesystem->boot_sector.fat_count * filesystem->boot_sector.fat_size_32);
    filesystem->cluster_size = (U32)filesystem->boot_sector.sectors_per_cluster * 512U;
    return StatusOK;
}

Status bootfs_open(BootFs* filesystem, const char* path, BootFsFile* file) {
    BootFsFat32DirEntry entry;
    char short_name[11];

    if ((filesystem == NULL) || (path == NULL) || (file == NULL)) {
        return StatusInvalidArgument;
    }
    if (bootfs_make_short_name(path, short_name) != StatusOK) {
        return StatusInvalidArgument;
    }
    if (bootfs_find_root_entry(filesystem, short_name, &entry) != StatusOK) {
        return StatusNotFound;
    }

    file->first_cluster = ((U32)entry.cluster_hi << 16) | entry.cluster_lo;
    file->current_cluster = file->first_cluster;
    file->current_cluster_index = 0U;
    file->size = entry.size;
    file->offset = 0U;
    return StatusOK;
}

Status bootfs_seek(BootFs* filesystem, BootFsFile* file, U32 offset) {
    (void)filesystem;

    if ((file == NULL) || (offset > file->size)) {
        return StatusInvalidArgument;
    }

    file->offset = offset;
    if (offset == 0U) {
        file->current_cluster = file->first_cluster;
        file->current_cluster_index = 0U;
    }

    return StatusOK;
}

SSize bootfs_read(BootFs* filesystem, BootFsFile* file, void* buffer, U32 size) {
    U8* output = (U8*)buffer;
    U8* output_begin = (U8*)buffer;
    U32 remaining;

    if ((filesystem == NULL) || (file == NULL) || (buffer == NULL)) {
        return (SSize)StatusInvalidArgument;
    }
    if (file->offset >= file->size) {
        return 0;
    }

    remaining = file->size - file->offset;
    if (size > remaining) {
        size = remaining;
    }

    while (size > 0U) {
        U32 cluster;
        U32 cluster_offset;
        U32 sector_in_cluster;
        U32 sector_offset;
        U32 sector_lba;
        U32 chunk;

        if (bootfs_locate_cluster(filesystem, file, file->offset, &cluster) != StatusOK) {
            return (SSize)StatusIoError;
        }

        cluster_offset = file->offset % filesystem->cluster_size;
        sector_in_cluster = cluster_offset / 512U;
        sector_offset = cluster_offset % 512U;
        sector_lba = bootfs_cluster_to_lba(filesystem, cluster) + sector_in_cluster;
        if (bootfs_read_sector(sector_lba, g_sector_buffer) != StatusOK) {
            return (SSize)StatusIoError;
        }

        chunk = 512U - sector_offset;
        if (chunk > size) {
            chunk = size;
        }

        memcopy(output, g_sector_buffer + sector_offset, chunk);
        output += chunk;
        file->offset += chunk;
        size -= chunk;
    }

    return (SSize)(output - output_begin);
}