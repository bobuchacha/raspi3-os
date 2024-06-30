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
	if (!hal_block_map[id].driver || !hal_block_map[id].driver->block_read){
		log_error("Disk read error");
		kpanic("Block Driver of device %d at address 0x%lX is invalid.", hal_block_map[id].driver);
	}
	
	// _trace("Reading disk %d. Driver address 0x%lX. Read Address 0x%lX", id, hal_block_map[id].driver, hal_block_map[id].driver->block_read);
	return hal_block_map[id].driver->block_read(hal_block_map[id].private, begin, count, buf);
}

void hal_partition_dump(){
	kprint("\n\n====================== DUMP PARITION MAP ========================\n\n");
	for (int i = 0;i < HAL_PARTITION_MAX; i++){
		HalPartitionMap p = hal_partition_map[i];
		if (p.fs_type == HAL_PARTITION_TYPE_NONE) continue;
		kprint("Partition: %d\n", i);
		kprint(" - begin: 0x%lX\n", p.begin);
		kprint(" - size: 0x%lX\n", p.size);
		kprint(" - file system: %d\n", p.fs_type);
		kprint(" - device ID: %d\n", p.dev);
		kprint(" - params: 0x%lX\n", hal_block_map[p.dev].private);

	}
	kprint("\n================================================================\n\n");
}

int hal_partition_read(int id, int begin, int count, void* buf) {
	if (id >= HAL_PARTITION_MAX) {
		return -1;
	}
	if (hal_partition_map[id].fs_type == HAL_PARTITION_TYPE_NONE) {
		return -1;
	}
	return hal_block_read(hal_partition_map[id].dev, hal_partition_map[id].begin + begin, count,
						  buf);
}


int hal_block_read(int id, int begin, int count, void* buf) {
	// _trace("Reading %d, from %d, count: %d to 0x%lX\n", id, begin, count, buf);
	return hal_disk_read(id, begin, count, buf);
	if (count > 1) {
		return hal_disk_read(id, begin, count, buf);
	}

	// struct BlockDevice* blk = &hal_block_map[id];
	// // acquire(&blk->cache_lock);
	// for (int i = 0; i < HAL_BLOCK_CACHE_MAX; i++) {
	// 	if (blk->cache[i].buf && blk->cache[i].lba == begin) {
	// 		memmove(buf, blk->cache[i].buf, 512);
	// 		// release(&blk->cache_lock);
	// 		return 0;
	// 	}
	// }

	// if (!blk->cache[blk->cache_next].buf) {
	// 	blk->cache[blk->cache_next].buf = kmalloc(512);
	// }
	// blk->cache[blk->cache_next].lba = begin;
	// // release(&blk->cache_lock);
	// hal_disk_read(id, begin, count, blk->cache[blk->cache_next].buf);
	// // acquire(&blk->cache_lock);
	// memmove(buf, blk->cache[blk->cache_next].buf, 512);
	// blk->cache_next++;
	// if (blk->cache_next >= HAL_BLOCK_CACHE_MAX) {
	// 	blk->cache_next = 0;
	// }
	// // release(&blk->cache_lock);
	// return 0;
}

static void hal_block_probe_partition(int block_id) {
	
//     _trace("Probing partition on block device id: %d", block_id);
	if (hal_disk_read(block_id, 1, 1, (void*)sector_buffer) == ERROR_INVAILD) {
		log_error("Disk read error");
		kpanic("disk read error");
	}
//     printf("First Long of buffer: 0x%lX\n", ((ULong*)(sector_buffer))[0]);
//     kdump(sector_buffer);
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
	// _trace("Adding %s block device, driver: 0x%lX", name, driver);
    for (int i = 0; i < HAL_BLOCK_MAX; i++) {
		if (!hal_block_map[i].driver) {
			// _trace("[hal] Block device %s added into slot %d\n", name, i);
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

int hal_partition_write(int id, int begin, int count, const void* buf) {
	if (id >= HAL_PARTITION_MAX) {
		return -1;
	}
	if (hal_partition_map[id].fs_type == HAL_PARTITION_TYPE_NONE) {
		return -1;
	}
	return hal_block_write(hal_partition_map[id].dev, hal_partition_map[id].begin + begin, count,
						   buf);
}

int hal_disk_write(int id, int begin, int count, const void* buf) {
	if (id >= HAL_BLOCK_MAX) {
		return -1;
	}
	if (!hal_block_map[id].driver) {
		return ERROR_INVAILD;
	}

	return hal_block_map[id].driver->block_write(hal_block_map[id].private, begin, count, buf);
}

int hal_block_write(int id, int begin, int count, const void* buf) {

	return hal_disk_write(id, begin, count, buf);

	
	// if (count > 1) {
	// 	for (int i = 0; i < HAL_BLOCK_CACHE_MAX; i++) {
	// 		if (hal_block_map[id].cache[i].buf) {
	// 			kfree(hal_block_map[id].cache[i].buf);
	// 		}
	// 	}
	// 	hal_disk_write(id, begin, count, buf);
	// }

	// struct BlockDevice* blk = &hal_block_map[id];
	// acquire(&blk->cache_lock);
	// for (int i = 0; i < HAL_BLOCK_CACHE_MAX; i++) {
	// 	if (blk->cache[i].buf && blk->cache[i].lba == begin) {
	// 		memmove(blk->cache[i].buf, buf, 512);
	// 		release(&blk->cache_lock);
	// 		return hal_disk_write(id, begin, count, blk->cache[i].buf);
	// 	}
	// }
	// release(&blk->cache_lock);
	return hal_disk_write(id, begin, count, buf);
}