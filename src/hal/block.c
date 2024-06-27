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
		return ERROR_INVAILD;
	}
	if (!hal_block_map[id].driver) {
		return ERROR_INVAILD;
	}
	return hal_block_map[id].driver->block_read(hal_block_map[id].private, begin, count, buf);
}


static void hal_block_probe_partition(int block_id) {
	
    	_trace("Probing partition on block device id: %d", block_id);
	if (hal_disk_read(block_id, 1, 1, (void*)sector_buffer) < 0) {
		kpanic("disk read error");
	}
    	printf("First Long of buffer: 0x%lX\n", ((ULong*)(sector_buffer))[0]);
    	kdump((void*)sector_buffer);
	
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
	_trace("Adding %s block device", name);
    	for (int i = 0; i < HAL_BLOCK_MAX; i++) {
		if (!hal_block_map[i].driver) {
			_trace("[hal] Block device %s added\n", name);
			hal_block_map[i].private = private;
			hal_block_probe_partition(i);
			hal_block_map[i].driver = driver;
			// hal_block_cache_init(i);
			return;
		}
	}
	kpanic("too many block devices");
}


/**
 * Stage 2: initialization after memory management is initialized
*/
void hal_block_init_2(){
	for (int i = 0; i < HAL_BLOCK_MAX; i++) {
		if (hal_block_map[i].driver) {
			// init here
			// hal_block_cache_init(i);
			return;
		}
	}
}

static void hal_block_cache_init(int block_id) {
	// hal_block_map[block_id].cache = mem_alloc_page();
	// memset(hal_block_map[block_id].cache, 0, PAGE_SIZE);
	// hal_block_map[block_id].cache_next = 0;
	// initlock(&hal_block_map[block_id].cache_lock, "block-cache");
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