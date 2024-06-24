#include <ros.h>
#include "../../param.h"
#include "vfs.h"
#include "utils.h"
#include "log.h"
#include "../../hal/hal.h"
#include "memory.h"

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

void vfs_mount_table_dump(){
	kprint("\n\n====================== MOUNT TABLE DUMP ========================\n\n");
	for (int i = 0;i < VFS_MOUNT_TABLE_MAX; i++){
		struct VfsMountTableEntry p = vfs_mount_table[i];
		// if (!p.fs_driver) continue;
		kprint("Mount Point: %d\n", i);
		kprint(" - driver: 0x%lX\n", p.fs_driver);
		kprint(" - private: 0x%lX\n", p.private);

	}
	kprint("\n================================================================\n\n");
}

int vfs_path_to_fs(struct VfsPath orig_path, struct VfsPath* path) {
    if (orig_path.parts == 0) {
        path->parts = 0;
        return 0;
    } else {
        if (strncmp(orig_path.pathbuf, "fat32", 64) == 0) {
            path->parts = orig_path.parts - 1;
            path->pathbuf = orig_path.pathbuf + VFS_NAME_LENGTH_MAX;
            return 1;
        } else {
            path->parts = orig_path.parts;
            path->pathbuf = orig_path.pathbuf;
            return 0;
        }
    }
}


int vfs_mkdir(const char* dirname) {
	struct VfsPath filepath;
	filepath.pathbuf = (char*)kmalloc(4096);
	filepath.parts = vfs_path_split(dirname, filepath.pathbuf);
	if (dirname[0] != '/') {
		vfs_get_absolute_path(&filepath);
	}
	struct VfsPath path;
	int fs_id = vfs_path_to_fs(filepath, &path);

	int ret
		= vfs_mount_table[fs_id].fs_driver->create_directory(vfs_mount_table[fs_id].private, path);
	
	kfree((Address)filepath.pathbuf);
	return ret;
}