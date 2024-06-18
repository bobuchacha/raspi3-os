#include <ros.h>
#include "../../param.h"
#include "vfs.h"
#include "utils.h"
#include "log.h"
#include "../../hal/hal.h"

struct VfsMountTableEntry vfs_mount_table[VFS_MOUNT_TABLE_MAX];

void vfs_init(void) {
	memset(vfs_mount_table, 0, sizeof(vfs_mount_table));
	int fs_id = 0;

	for (int i = 0; i < HAL_PARTITION_MAX; i++) {
		if (hal_partition_map[i].fs_type == HAL_PARTITION_TYPE_FAT32) {
			if (fs_id == 0) {
				_trace("[vfs] mount fat32 on /\n");
			} else if (fs_id == 1) {
				_trace("[vfs] mount fat32 on /fat32\n");
			}
			filesystem_fat32_driver->mount(i, &vfs_mount_table[fs_id].private);
			vfs_mount_table[fs_id].fs_driver = filesystem_fat32_driver;
			fs_id++;
			break;
		} else if (hal_partition_map[i].fs_type == HAL_PARTITION_TYPE_DATA) {
			if (filesystem_fat32_driver->probe(i) != 0) {
				continue;
			}
			if (fs_id == 0) {
				_trace("[vfs] mount fat32 on /\n");
			} else if (fs_id == 1) {
				_trace("[vfs] mount fat32 on /fat32\n");
			}
			filesystem_fat32_driver->mount(i, &vfs_mount_table[fs_id].private);
			vfs_mount_table[fs_id].fs_driver = filesystem_fat32_driver;
			fs_id++;
			break;
		}
	}
	if (fs_id == 0) {
		kpanic("root filesystem not found");
	}
}