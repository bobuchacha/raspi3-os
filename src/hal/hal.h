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

typedef struct BlockCache {
    int lba;
    void* buf;
} BlockCache;

typedef struct BlockDevice {
    const struct BlockDeviceDriver* driver;
    void* private;
    struct BlockCache* cache;
    int cache_next;
//    struct spinlock cache_lock;
} BlockDevice;

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
    HalPartitionFilesystemType fs_type;
    unsigned int dev;
    unsigned int begin;
    unsigned int size;
} HalPartitionMap;

// hal.c
void hal_init();

// block.c
extern struct BlockDevice hal_block_map[HAL_BLOCK_MAX];
extern struct HalPartitionMap hal_partition_map[HAL_PARTITION_MAX];

struct HalPartitionMap* hal_partition_map_insert(enum HalPartitionFilesystemType fs,
                                                 unsigned int dev, unsigned int begin,
                                                 unsigned int size);

void hal_block_init(void);      // stage1 initialization - without memory management
void hal_block_init_2(void);    // stage 2 initialization - with memory management
void hal_block_register_device(const char* name, void* private,
                               const struct BlockDeviceDriver* driver);
int hal_block_read(int id, int begin, int count, void* buf);
int hal_disk_read(int id, int begin, int count, void* buf);
int hal_partition_read(int id, int begin, int count, void* buf);
int hal_block_write(int id, int begin, int count, const void* buf);
int hal_disk_write(int id, int begin, int count, const void* buf);
int hal_partition_write(int id, int begin, int count, const void* buf);


// mbr.c
void mbr_probe_partition(int block_id);
#endif //RASPI3_OS_HAL_H
