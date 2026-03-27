#include <ros.h>
#include "fat32.h"
#include "../filesystem.h"
#include "../../hal/hal.h"
#include "fat32-struct.h"
#include "memory.h"
#include "lib/string.h"
#include "log.h"

int fat32_cluster_to_sector(struct FAT32Private* priv, unsigned int cluster) {
	return (cluster - 2) * (priv->boot_sector->sector_per_cluster)
		   + (priv->boot_sector->reserved_sector)
		   + (priv->boot_sector->fat_number) * (priv->boot_sector->fat_size);
}

int fat32_read_cluster(struct FAT32Private* priv, void* dest, unsigned int cluster,
					   unsigned int begin, unsigned int size) {
	Address sect = kmalloc(SECTORSIZE);
	unsigned int off = 0;
	while (off < size) {
		int copysize;
		if ((begin + off) / SECTORSIZE < (begin + size) / SECTORSIZE) {
			copysize = ((begin + off) / SECTORSIZE + 1) * SECTORSIZE - (begin + off);
		} else {
			copysize = size - off;
		}
		unsigned int sector = fat32_cluster_to_sector(priv, cluster) + (begin + off) / SECTORSIZE;
		if (hal_partition_read(priv->partition_id, sector, 1, (Pointer)sect) < 0) {
			kfree(sect);
			return ERROR_READ_FAIL;
		}
		memmove(dest + off, (void*)(sect + (begin + off) % SECTORSIZE), copysize);
		off += copysize;
	}
	kfree(sect);
	return 0;
}

int fat32_read(void* private, unsigned int cluster, void* buf, unsigned int offset, unsigned int size) {
	struct FAT32Private* priv = private;
	int clussize = priv->boot_sector->sector_per_cluster * SECTORSIZE;
	unsigned int off = 0;
	while (off < size) {
		int copysize;
		if ((offset + off) / clussize < (offset + size) / clussize) {
			copysize = ((offset + off) / clussize + 1) * clussize - (offset + off);
		} else {
			copysize = size - off;
		}
		unsigned int clus = fat32_offset_cluster(priv, cluster, offset + off);
		if (fat32_read_cluster(priv, buf + off, clus, (offset + off) % clussize, copysize) < 0) {
			return ERROR_READ_FAIL;
		}
		off += copysize;
	}
	return size;
}

int fat32_write_cluster(struct FAT32Private* priv, const void* src, unsigned int cluster,
						unsigned int begin, unsigned int size) {
	Address sect = kmalloc(SECTORSIZE);
	unsigned int off = 0;
	while (off < size) {
		int copysize;
		if ((begin + off) / SECTORSIZE < (begin + size) / SECTORSIZE) {
			copysize = ((begin + off) / SECTORSIZE + 1) * SECTORSIZE - (begin + off);
		} else {
			copysize = size - off;
		}
		unsigned int sector = fat32_cluster_to_sector(priv, cluster) + (begin + off) / SECTORSIZE;
		if (hal_partition_read(priv->partition_id, sector, 1, (Pointer)sect) < 0) {
			kfree(sect);
			return ERROR_READ_FAIL;
		}
		memmove((void*)sect + (begin + off) % SECTORSIZE, src + off, copysize);
		if (hal_partition_write(priv->partition_id, sector, 1, (Pointer)sect) < 0) {
			kfree(sect);
			return ERROR_WRITE_FAIL;
		}
		off += copysize;
	}
	kfree(sect);
	return 0;
}

unsigned int fat32_allocate_cluster(struct FAT32Private* priv) {
	Address buf = kmalloc(SECTORSIZE);
	for (unsigned int fat = 0; fat < priv->boot_sector->fat_size; fat++) {
		int sect = priv->boot_sector->reserved_sector + fat;
		if (hal_partition_read(priv->partition_id, sect, 1, (Pointer)buf) < 0) {
			kfree(buf);
			return ERROR_READ_FAIL;
		}
		for (int i = 0; i < 128; i++) {
			if (((int*)buf)[i] == 0) {
				((int*)buf)[i] = 0x0fffffff;
				// first FAT
				if (hal_partition_write(priv->partition_id, sect, 1, (Pointer)buf) < 0) {
					kfree(buf);
					return ERROR_WRITE_FAIL;
				}
				// second FAT
				if (hal_partition_write(priv->partition_id, sect + priv->boot_sector->fat_size, 1, (Pointer)buf)< 0) {
					kfree(buf);
					return ERROR_WRITE_FAIL;
				}
				kfree(buf);
				return fat * 128 + i;
			}
		}
	}
	kfree(buf);
	return 0;
}

int fat32_write(void* private, unsigned int cluster, const void* buf, unsigned int offset,unsigned int size) {
	struct FAT32Private* priv = private;
	int clussize = priv->boot_sector->sector_per_cluster * SECTORSIZE;
	unsigned int off = 0;
	while (off < size) {
		int copysize;
		if ((offset + off) / clussize < (offset + size) / clussize) {
			copysize = ((offset + off) / clussize + 1) * clussize - (offset + off);
		} else {
			copysize = size - off;
		}
		unsigned int clus = fat32_offset_cluster(priv, cluster, offset + off);
		if (clus == 0) { // end of cluster chain
			if ((clus = fat32_allocate_cluster(priv)) < 0) {
				return ERROR_OUT_OF_SPACE;
			}
			if (fat32_append_cluster(priv, cluster, clus) < 0) {
				return ERROR_WRITE_FAIL;
			}
		}
		if (fat32_write_cluster(priv, buf + off, clus, (offset + off) % clussize, copysize) < 0) {
			return ERROR_WRITE_FAIL;
		}
		off += copysize;
	}
	return size;
}













