#include <ros.h>
#include "fat32.h"
#include "../filesystem.h"
#include "../../hal/hal.h"
#include "fat32-struct.h"
#include "memory.h"
#include "lib/string.h"
#include "log.h"

unsigned int fat32_fat_read(struct FAT32Private* priv, unsigned int current) {
	Address buf = kmalloc(SECTORSIZE);
	unsigned int sect = priv->boot_sector->reserved_sector + current / 128;
	hal_partition_read(priv->partition_id, sect, 1, (Pointer)buf);
	unsigned int val = ((UInt*)buf)[current % 128];
	kfree(buf);
	return val;
}

unsigned int fat32_offset_cluster(struct FAT32Private* priv, unsigned int cluster,
								  unsigned int offset) {
	int n = offset / 512 / priv->boot_sector->sector_per_cluster;
	while (n--) {
		cluster = fat32_fat_read(priv, cluster);
		if (cluster >= 0x0ffffff8) {
			return 0;
		}
	}
	return cluster;
}

int fat32_write_fat(struct FAT32Private* priv, unsigned int cluster, unsigned int data) {
	Address buf = kmalloc(SECTORSIZE);
	int sect = priv->boot_sector->reserved_sector + cluster / 128;
	if (hal_partition_read(priv->partition_id, sect, 1, (Pointer)buf) < 0) {
		kfree(buf);
		return ERROR_READ_FAIL;
	}
	((UInt*)buf)[cluster % 128] = data;
	// first fat
	if (hal_partition_write(priv->partition_id, sect, 1, (Pointer)buf) < 0) {
		kfree(buf);
		return ERROR_WRITE_FAIL;
	}
	// second fat
	if (hal_partition_write(priv->partition_id, sect + priv->boot_sector->fat_size, 1, (Pointer)buf) < 0) {
		kfree(buf);
		return ERROR_WRITE_FAIL;
	}
	kfree(buf);
	return 0;
}

int fat32_append_cluster(struct FAT32Private* priv, unsigned int begin_cluster,
						 unsigned int end_cluster) {
	unsigned int clus = begin_cluster;
	while (fat32_fat_read(priv, clus) < 0x0ffffff8) {
		clus = fat32_fat_read(priv, clus);
	}
	return fat32_write_fat(priv, clus, end_cluster);
}

int fat32_free_chain(struct FAT32Private* priv, unsigned int cluster) {
	do {
		// read it out and clear FAT entry
		unsigned int clus = fat32_fat_read(priv, cluster);
		if (fat32_write_fat(priv, cluster, 0) < 0) {
			return ERROR_WRITE_FAIL;
		}
		// advance to next FAT entry
		cluster = clus;
	} while (cluster < 0x0ffffff8);
	return 0;
}