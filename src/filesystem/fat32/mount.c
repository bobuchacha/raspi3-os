#include <ros.h>
#include "fat32.h"
#include "../filesystem.h"
#include "../../hal/hal.h"
#include "fat32-struct.h"
#include "memory.h"
#include "lib/string.h"
#include "log.h"

int fat32_mount(int partition_id, void** private) {
	struct FAT32BootSector* bootsect = (struct FAT32BootSector*)kmalloc(512);
	_trace("Reading partition's first sector");
	if (hal_partition_read(partition_id, 0, 1, bootsect) < 0) {
		kerror("Read failed");
		return ERROR_READ_FAIL;
	}

	if (strncmp(bootsect->fstype, "FAT32", 5)) {
		kpanic("Mounting a non-FAT32 filesystem. %s", bootsect->fstype);
	}


	char label[12];
	strncpy(label, bootsect->volume_label, 12);
	label[11] = '\0';
	_trace("Mounting: %s\n", label);

	struct FAT32Private* priv = (struct FAT32Private*)kmalloc(sizeof(struct FAT32Private));
	priv->partition_id = partition_id;
	priv->boot_sector = bootsect;
	priv->mode = 0777;
	priv->uid = 0;
	priv->gid = 0;
	*private = priv;	// set private information of the partition

	_trace("Mounted successfully");

	
	return 0;
}

int fat32_probe(int partition_id) {
	struct FAT32BootSector* bootsect = (struct FAT32BootSector*)kmalloc(sizeof(struct FAT32BootSector));
	if (hal_partition_read(partition_id, 0, 1, bootsect) < 0) {
		return ERROR_READ_FAIL;
	}
	return strncmp(bootsect->fstype, "FAT32", 5);
}

void fat32_set_default_attr(void* private, unsigned int uid, unsigned int gid, unsigned int mode) {
	struct FAT32Private* priv = private;
	priv->uid = uid;
	priv->gid = gid;
	priv->mode = mode;
}

















