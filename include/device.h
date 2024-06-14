#ifndef _DEVICE_H
#define _DEVICE_H

#include "ros.h"
#include "device/raspi3b.h"             // this is our device
#include "arch/cortex-a53/mmu.h"                // we use this arch

// HAL Block Device. In here we use sdcard as block device
typedef struct BlockDeviceDriver {
    int (*block_read)(void *private, unsigned int begin, int count, void *buf);

    int (*block_write)(void *private, unsigned int begin, int count, const void *buf);
} BlockDeviceDriver;

#define HAL_BLOCK_CACHE_MAX 512

typedef struct BlockCache {
    int  lba;
    void *buf;
} BlockCache;

struct BlockDevice {
    const BlockDeviceDriver *driver;
    void                    *private;
    BlockCache              *cache;
    int                     cache_next;
//    struct spinlock cache_lock;
};

#define HAL_BLOCK_MAX 8

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
    UInt                       dev;
    UInt                       begin;
    UInt                       size;
} HalPartitionMap;

#define HAL_PARTITION_MAX 8

// device/block.c
extern struct BlockDevice hal_block_map[HAL_BLOCK_MAX];
extern struct HalPartitionMap hal_partition_map[HAL_PARTITION_MAX];
struct HalPartitionMap* hal_partition_map_insert(enum HalPartitionFilesystemType fs,
                                                 unsigned int dev, unsigned int begin,
                                                 unsigned int size);
void hal_block_init(void);
void hal_block_register_device(const char* name, void* private,
                               const struct BlockDeviceDriver* driver);
int hal_block_read(int id, int begin, int count, void* buf);
int hal_disk_read(int id, int begin, int count, void* buf);
int hal_partition_read(int id, int begin, int count, void* buf);
int hal_block_write(int id, int begin, int count, const void* buf);
int hal_disk_write(int id, int begin, int count, const void* buf);
int hal_partition_write(int id, int begin, int count, const void* buf);

void device_init();                     // initialize device
void device_init_fs();

#endif