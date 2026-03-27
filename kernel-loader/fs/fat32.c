#include "fs/fat32.h"
#include "device/sd.h"
#include "lib/string.h"
#include "log.h"

#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_LONG_NAME 0x0F
#define FAT32_CLUSTER_END 0x0FFFFFF8U
#define FAT32_PARTITION_FAT32_CHS 0x0BU
#define FAT32_PARTITION_FAT32_LBA 0x0CU

typedef struct PACKED {
    UByte status;
    UByte first_chs[3];
    UByte type;
    UByte last_chs[3];
    UInt lba;
    UInt size;
} BootFsMbrEntry;

static UByte bootfs_sector_buffer[512];

static int bootfs_read_sector(UInt lba, void* buffer) {
    return sd_readblock(lba, (unsigned char*)buffer, 1) == 512 ? 0 : -1;
}

static UInt bootfs_find_partition_lba(void) {
    int index;

    if (bootfs_read_sector(0, bootfs_sector_buffer) != 0) {
        return 0;
    }
    for (index = 0; index < 4; ++index) {
        const BootFsMbrEntry* entry = (const BootFsMbrEntry*)(bootfs_sector_buffer + 0x1BE + (index * 16));

        if (entry->type == FAT32_PARTITION_FAT32_CHS || entry->type == FAT32_PARTITION_FAT32_LBA) {
            return entry->lba;
        }
    }
    return 0;
}

static UInt bootfs_cluster_to_lba(BootFs* fs, UInt cluster) {
    return fs->data_lba + ((cluster - 2U) * fs->boot_sector.sectors_per_cluster);
}

static int bootfs_read_fat_entry(BootFs* fs, UInt cluster, UInt* next_cluster) {
    UInt fat_offset = cluster * 4U;
    UInt sector = fs->fat_lba + (fat_offset / 512U);
    UInt offset = fat_offset % 512U;
    UInt value;

    if (bootfs_read_sector(sector, bootfs_sector_buffer) != 0) {
        return -1;
    }

    value = ((UInt)bootfs_sector_buffer[offset]) |
            ((UInt)bootfs_sector_buffer[offset + 1U] << 8) |
            ((UInt)bootfs_sector_buffer[offset + 2U] << 16) |
            ((UInt)bootfs_sector_buffer[offset + 3U] << 24);
    *next_cluster = value & 0x0FFFFFFFU;
    return 0;
}

static int bootfs_make_short_name(const char* path, char short_name[11]) {
    int name_len = 0;
    int ext_len = 0;
    const char* cursor = path;
    const char* dot = NULL;
    int index;

    if (!path || path[0] != '/') {
        return -1;
    }
    cursor++;
    if (*cursor == '\0') {
        return -1;
    }
    while (*cursor) {
        if (*cursor == '/') {
            return -1;
        }
        if (*cursor == '.') {
            if (dot != NULL) {
                return -1;
            }
            dot = cursor;
        }
        cursor++;
    }

    memset(short_name, ' ', 11);
    if (dot) {
        name_len = (int)(dot - (path + 1));
        ext_len = (int)(cursor - dot - 1);
    } else {
        name_len = (int)(cursor - (path + 1));
    }
    if (name_len <= 0 || name_len > 8 || ext_len > 3) {
        return -1;
    }

    for (index = 0; index < name_len; ++index) {
        short_name[index] = (char)k_toupper(path[1 + index]);
    }
    for (index = 0; index < ext_len; ++index) {
        short_name[8 + index] = (char)k_toupper(dot[1 + index]);
    }
    return 0;
}

static int bootfs_find_root_entry(BootFs* fs, const char short_name[11], BootFsFat32DirEntry* entry_out) {
    UInt cluster = fs->boot_sector.root_cluster;
    UInt cluster_size = fs->cluster_size;

    while (cluster >= 2U && cluster < FAT32_CLUSTER_END) {
        UInt offset = 0;

        while (offset < cluster_size) {
            UInt sector_lba = bootfs_cluster_to_lba(fs, cluster) + (offset / 512U);
            UInt sector_offset = offset % 512U;
            const BootFsFat32DirEntry* entry;

            if (bootfs_read_sector(sector_lba, bootfs_sector_buffer) != 0) {
                return -1;
            }
            entry = (const BootFsFat32DirEntry*)(bootfs_sector_buffer + sector_offset);
            if (entry->name[0] == 0x00) {
                return -1;
            }
            if (entry->name[0] != 0xE5 &&
                entry->attr != FAT32_ATTR_LONG_NAME &&
                !(entry->attr & FAT32_ATTR_VOLUME_ID) &&
                memcmp(entry->name, short_name, 11U) == 0) {
                memmove(entry_out, entry, sizeof(*entry_out));
                return 0;
            }
            offset += sizeof(BootFsFat32DirEntry);
        }

        if (bootfs_read_fat_entry(fs, cluster, &cluster) != 0) {
            return -1;
        }
    }

    return -1;
}

static int bootfs_locate_cluster(BootFs* fs, BootFsFile* file, UInt offset, UInt* cluster_out) {
    UInt target_index = offset / fs->cluster_size;
    UInt cluster = file->current_cluster;
    UInt cluster_index = file->current_cluster_index;

    if (cluster == 0 || target_index < cluster_index) {
        cluster = file->first_cluster;
        cluster_index = 0;
    }
    while (cluster_index < target_index) {
        if (bootfs_read_fat_entry(fs, cluster, &cluster) != 0) {
            return -1;
        }
        if (cluster < 2U || cluster >= FAT32_CLUSTER_END) {
            return -1;
        }
        cluster_index++;
    }

    file->current_cluster = cluster;
    file->current_cluster_index = cluster_index;
    *cluster_out = cluster;
    return 0;
}

int bootfs_init(BootFs* fs) {
    UInt partition_lba;
    const BootFsFat32BootSector* boot_sector;

    if (!fs) {
        return -1;
    }

    partition_lba = bootfs_find_partition_lba();
    if (bootfs_read_sector(partition_lba, bootfs_sector_buffer) != 0) {
        return -1;
    }

    boot_sector = (const BootFsFat32BootSector*)bootfs_sector_buffer;
    if (boot_sector->bytes_per_sector != 512U ||
        boot_sector->sectors_per_cluster == 0U ||
        boot_sector->fat_count == 0U ||
        boot_sector->fat_size_32 == 0U ||
        boot_sector->root_cluster < 2U ||
        strncmp(boot_sector->fs_type, "FAT32", 5) != 0) {
        log_error("bootfs: unsupported FAT32 volume");
        return -1;
    }

    fs->partition_lba = partition_lba;
    memmove(&fs->boot_sector, boot_sector, sizeof(fs->boot_sector));
    fs->fat_lba = partition_lba + boot_sector->reserved_sectors;
    fs->data_lba = fs->fat_lba + (boot_sector->fat_count * boot_sector->fat_size_32);
    fs->cluster_size = (UInt)boot_sector->sectors_per_cluster * 512U;
    return 0;
}

int bootfs_open(BootFs* fs, const char* path, BootFsFile* file) {
    BootFsFat32DirEntry entry;
    char short_name[11];

    if (!fs || !path || !file) {
        return -1;
    }
    if (bootfs_make_short_name(path, short_name) != 0) {
        return -1;
    }
    if (bootfs_find_root_entry(fs, short_name, &entry) != 0) {
        return -1;
    }

    file->first_cluster = ((UInt)entry.cluster_hi << 16) | entry.cluster_lo;
    file->current_cluster = file->first_cluster;
    file->current_cluster_index = 0;
    file->size = entry.size;
    file->offset = 0;
    return 0;
}

int bootfs_seek(BootFs* fs, BootFsFile* file, UInt offset) {
    (void)fs;
    if (!file || offset > file->size) {
        return -1;
    }
    file->offset = offset;
    if (offset == 0U) {
        file->current_cluster = file->first_cluster;
        file->current_cluster_index = 0;
    }
    return 0;
}

int bootfs_read(BootFs* fs, BootFsFile* file, void* buffer, UInt size) {
    UByte* out = (UByte*)buffer;
    UInt remaining;

    if (!fs || !file || !buffer) {
        return -1;
    }
    if (file->offset >= file->size) {
        return 0;
    }

    remaining = file->size - file->offset;
    if (size > remaining) {
        size = remaining;
    }

    while (size > 0U) {
        UInt cluster;
        UInt cluster_offset;
        UInt sector_in_cluster;
        UInt sector_offset;
        UInt sector_lba;
        UInt chunk;

        if (bootfs_locate_cluster(fs, file, file->offset, &cluster) != 0) {
            return -1;
        }

        cluster_offset = file->offset % fs->cluster_size;
        sector_in_cluster = cluster_offset / 512U;
        sector_offset = cluster_offset % 512U;
        sector_lba = bootfs_cluster_to_lba(fs, cluster) + sector_in_cluster;
        if (bootfs_read_sector(sector_lba, bootfs_sector_buffer) != 0) {
            return -1;
        }

        chunk = 512U - sector_offset;
        if (chunk > size) {
            chunk = size;
        }

        memmove(out, bootfs_sector_buffer + sector_offset, chunk);
        out += chunk;
        file->offset += chunk;
        size -= chunk;
    }

    return (int)(out - (UByte*)buffer);
}
