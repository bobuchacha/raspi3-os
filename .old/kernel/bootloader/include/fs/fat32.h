#ifndef ROS_LOADER_FS_FAT32_H
#define ROS_LOADER_FS_FAT32_H

#include "ros.h"

typedef struct {
    UByte jump[3];
    UByte oemname[8];
    UWord bytes_per_sector;
    UByte sectors_per_cluster;
    UWord reserved_sectors;
    UByte fat_count;
    UWord root_entries;
    UWord total_sectors_16;
    UByte media;
    UWord fat_size_16;
    UWord sectors_per_track;
    UWord head_count;
    UInt hidden_sectors;
    UInt total_sectors_32;
    UInt fat_size_32;
    UWord ext_flags;
    UWord fs_version;
    UInt root_cluster;
    UWord fsinfo_sector;
    UWord backup_boot_sector;
    UByte reserved[12];
    UByte drive_number;
    UByte reserved1;
    UByte boot_signature;
    UInt volume_id;
    char volume_label[11];
    char fs_type[8];
} PACKED BootFsFat32BootSector;

typedef struct {
    UByte name[11];
    UByte attr;
    UByte nt_reserved;
    UByte create_time_millis;
    UWord create_time;
    UWord create_date;
    UWord access_date;
    UWord cluster_hi;
    UWord write_time;
    UWord write_date;
    UWord cluster_lo;
    UInt size;
} PACKED BootFsFat32DirEntry;

typedef struct {
    UInt partition_lba;
    BootFsFat32BootSector boot_sector;
    UInt fat_lba;
    UInt data_lba;
    UInt cluster_size;
} BootFs;

typedef struct {
    UInt first_cluster;
    UInt current_cluster;
    UInt current_cluster_index;
    UInt size;
    UInt offset;
} BootFsFile;

int bootfs_init(BootFs* fs);
int bootfs_open(BootFs* fs, const char* path, BootFsFile* file);
int bootfs_seek(BootFs* fs, BootFsFile* file, UInt offset);
int bootfs_read(BootFs* fs, BootFsFile* file, void* buffer, UInt size);

#endif
