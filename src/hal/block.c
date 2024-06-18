//
// Created by Thang Cao on 6/16/24.
//
#include <ros.h>
#include "hal.h"
#include "utils.h"
#include "log.h"
#include "memory.h"

struct BlockDevice hal_block_map[HAL_BLOCK_MAX];
struct HalPartitionMap hal_partition_map[HAL_PARTITION_MAX];
unsigned char sector_buffer[512];

void hal_block_init(void) {
    memset(hal_block_map, 0, sizeof(hal_block_map));
    memset(hal_partition_map, 0, sizeof(hal_partition_map));
}

int hal_disk_read(int id, int begin, int count, void* buf) {
	if (id >= HAL_BLOCK_MAX) {
		log_error("Invalid block device ID %d!", id);
		return ERROR_INVAILD;
	}
	if (!hal_block_map[id].driver) {
		log_error("Invalid block device driver. Device %d, 0x%lx!", id, hal_block_map[id].driver);
		return ERROR_INVAILD;
	}
	return hal_block_map[id].driver->block_read(hal_block_map[id].private, begin, count, buf);
}


static void hal_block_probe_partition(int block_id) {
	
    _trace("Probing partition on block device id: %d", block_id);
	if (hal_disk_read(block_id, 1, 1, (void*)sector_buffer) == ERROR_INVAILD) {
		kpanic("disk read error");
	}
    printf("First Long of buffer: 0x%lX\n", ((ULong*)(sector_buffer))[0]);
    kdump(sector_buffer);
	if (((ULong*)(sector_buffer))[0] == 0x5452415020494645) { // EFI PART
		// GPT
		log_info("[hal] GPT partition table on block %d\n", block_id);
		// gpt_probe_partition(block_id);
        kpanic("Implements GPT prob partition here");
	} else {
		// MBR
		log_info("[hal] MBR partition table on block %d\n", block_id);
		mbr_probe_partition(block_id);
	}
}

    
void hal_block_register_device(const char* name, void* private, const struct BlockDeviceDriver* driver) {
	_trace("Adding %s block device, driver: 0x%lX", name, driver);
    for (int i = 0; i < HAL_BLOCK_MAX; i++) {
		if (!hal_block_map[i].driver) {
			_trace("[hal] Block device %s added into slot %d\n", name, i);
			hal_block_map[i].private = private;
			hal_block_map[i].driver = driver;
			hal_block_probe_partition(i);
			// hal_block_cache_init(i);
			return;
		}
	}
	kpanic("too many block devices");
}


struct HalPartitionMap* hal_partition_map_insert(enum HalPartitionFilesystemType fs,
                                                 unsigned int dev, unsigned int begin,
                                                 unsigned int size) {
    for (int i = 0; i < HAL_PARTITION_MAX; i++) {
        if (hal_partition_map[i].fs_type == HAL_PARTITION_TYPE_NONE) {
            hal_partition_map[i].fs_type = fs;
            hal_partition_map[i].dev = dev;
            hal_partition_map[i].begin = begin;
            hal_partition_map[i].size = size;
            return &hal_partition_map[i];
        }
    }
    return 0;
}