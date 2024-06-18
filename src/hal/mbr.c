//
// Created by Thang Cao on 6/16/24.
//
#include <ros.h>
#include "hal.h"
#include "utils.h"
#include "log.h"
#include "memory.h"

struct MBREntry {
	UByte boot; // boot signature
	UByte first[3]; // first sector CHS address
	UByte type; // partition type
	UByte last[3]; // last sector CHS address
	UInt lba; // start sector LBA
	UInt size; // number of sectors
} PACKED;

unsigned char bootsect[512];

void mbr_probe_partition(int block_id) {
	int i = 0;

	if (hal_disk_read(block_id, 0, 1, bootsect) < 0) {
		kpanic("disk read error");
	}
	for (int j = 0; j < 4; j++) {
		struct MBREntry* entry = (void*)bootsect + 0x1be + j * 0x10;
		enum HalPartitionFilesystemType fs;
		
		if (entry->type == 0) {
			continue;
		} else if ((entry->type == 0xb) || (entry->type == 0xc)) {
			log_info("[mbr] FAT32 partition on block device %d MBR %d\n", block_id, j);
			fs = HAL_PARTITION_TYPE_FAT32;
		} else if (entry->type == 0x83) {
			log_info("[mbr] Linux partition on block device %d MBR %d\n", block_id, j);
			fs = HAL_PARTITION_TYPE_LINUX;
		} else {
			log_info("[mbr] Partition on block device %d MBR %d type %x\n", block_id, j,
					entry->type);
			fs = HAL_PARTITION_TYPE_OTHER;
		}
		i++;
		if (!hal_partition_map_insert(fs, block_id, entry->lba, entry->size)) {
			kpanic("hal too many partition");
		}
	}

	// layout is not partition type. 
	if (!i) {
		
		kerror("Implement non partition sdcard here");

		//TODO: implements here
		
		if (!hal_partition_map_insert(HAL_PARTITION_TYPE_FAT32, block_id, 0, 0xFFFFFFFF)) {
			kpanic("Can not add primary parition");
		}
	}
}