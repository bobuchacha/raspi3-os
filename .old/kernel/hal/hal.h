//
// Created by Thang Cao on 6/16/24.
//

#ifndef RASPI3_OS_HAL_H
#define RASPI3_OS_HAL_H

#include <ros.h>
#include "../param.h"

// HAL Block Device
typedef struct BlockDeviceDriver {
    int (*block_read)(void* private, unsigned int begin, int count, void* buf);
    int (*block_write)(void* private, unsigned int begin, int count, const void* buf);
} BlockDeviceDriver;

/* Struct describing block cache. */
typedef struct BlockCache {
    int lba; /* Holds lba for this scope. */
    void* buf; /* Holds buf for this scope. */
} BlockCache;

/* Struct describing block device. */
typedef struct BlockDevice {
    const struct BlockDeviceDriver* driver; /* Holds driver for this scope. */
    void* private; /* Holds private for this scope. */
    struct BlockCache* cache; /* Holds cache for this scope. */
    int cache_next; /* Holds cache next for this scope. */
    //    struct spinlock cache_lock;
} BlockDevice;

/* Enum describing hal partition filesystem type. */
typedef enum HalPartitionFilesystemType {
    HAL_PARTITION_TYPE_NONE,
    HAL_PARTITION_TYPE_OTHER,
    HAL_PARTITION_TYPE_FAT32,
    HAL_PARTITION_TYPE_LINUX,
    HAL_PARTITION_TYPE_DATA,
    HAL_PARTITION_TYPE_ESP
} HalPartitionFilesystemType;

// partition table cache
typedef struct HalPartitionMap {
    HalPartitionFilesystemType fs_type; /* Holds fs type for this scope. */
    unsigned int dev; /* Holds dev for this scope. */
    unsigned int begin; /* Holds begin for this scope. */
    unsigned int size; /* Size of this region in bytes. */
} HalPartitionMap;

// hal.c
void hal_init();

// block.c
extern struct BlockDevice hal_block_map[HAL_BLOCK_MAX];
extern struct HalPartitionMap hal_partition_map[HAL_PARTITION_MAX];

/* Handle `hal_partition_map_insert` work for the hal subsystem. */
struct HalPartitionMap* hal_partition_map_insert(enum HalPartitionFilesystemType fs,
    unsigned int dev, unsigned int begin,
    unsigned int size);

/* Block a register device for the hal subsystem. */
void hal_block_init(void);      // stage1 initialization - without memory management
void hal_block_init_2(void);    // stage 2 initialization - with memory management
void hal_block_register_device(const char* name, void* private,
    const struct BlockDeviceDriver* driver);
/* Block a read for the hal subsystem. */
int hal_block_read(int id, int begin, int count, void* buf);
/* Handle `hal_disk_read` work for the hal subsystem. */
int hal_disk_read(int id, int begin, int count, void* buf);
/* Handle `hal_partition_read` work for the hal subsystem. */
int hal_partition_read(int id, int begin, int count, void* buf);
/* Block a write for the hal subsystem. */
int hal_block_write(int id, int begin, int count, const void* buf);
/* Handle `hal_disk_write` work for the hal subsystem. */
int hal_disk_write(int id, int begin, int count, const void* buf);
/* Handle `hal_partition_write` work for the hal subsystem. */
int hal_partition_write(int id, int begin, int count, const void* buf);
/* Handle `hal_partition_find_first` work for the hal subsystem. */
int hal_partition_find_first(enum HalPartitionFilesystemType fs_type);


// mbr.c
void mbr_probe_partition(int block_id);
#endif //RASPI3_OS_HAL_H
