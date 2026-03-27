#include <ros.h>
#include "../../param.h"
#include "vfs.h"
#include "utils.h"
#include "log.h"
#include "../../hal/hal.h"
#include "memory.h"

int vfs_dir_open(struct FileDesc* fd, const char* dirname) {
	memset(fd, 0, sizeof(struct FileDesc));
	struct VfsPath dirpath;
	dirpath.pathbuf = (char*)kmalloc(4096);
	dirpath.parts = vfs_path_split(dirname, dirpath.pathbuf);
	if (dirname[0] != '/') {
		vfs_get_absolute_path(&dirpath);
	}
	
	struct VfsPath path;
	int fs_id = vfs_path_to_fs(dirpath, &path);

	int fblock = vfs_mount_table[fs_id].fs_driver->open(vfs_mount_table[fs_id].private, path);
	if (fblock < 0) {
		kfree((Address)dirpath.pathbuf);
		return fblock;
	}
	fd->block = fblock;
	fd->offset = vfs_mount_table[fs_id].fs_driver->dir_first_file(vfs_mount_table[fs_id].private, fd->block);

	fd->fs_id = fs_id;
	fd->dir = 1;
	fd->read = 1;
	fd->used = 1;
	kfree((Address)dirpath.pathbuf);
	return 0;
}

int vfs_dir_read(struct FileDesc* fd, char* buffer) {
	if (!fd->used) {
		return ERROR_INVAILD;
	}
	if (!fd->read) {
		return ERROR_INVAILD;
	}
	if (!fd->dir) {
		return ERROR_INVAILD;
	}

	int off;
	off = vfs_mount_table[fd->fs_id].fs_driver->dir_read(vfs_mount_table[fd->fs_id].private, buffer,
														 fd->block, fd->offset);

	if (off == 0) {
		return 0; // EOF
	} else if (off < 0) {
		return off; // error
	}
	fd->offset = off;
	return 1;
}

int vfs_dir_read_ex(struct FileDesc* fd, struct DirectoryEntry * entry){
	if (!fd->used) {
		return ERROR_INVAILD;
	}
	if (!fd->read) {
		return ERROR_INVAILD;
	}
	if (!fd->dir) {
		return ERROR_INVAILD;
	}

	int off;
	off = vfs_mount_table[fd->fs_id].fs_driver->dir_read_ex(vfs_mount_table[fd->fs_id].private, entry,
														 fd->block, fd->offset);

	if (off == 0) {
		return 0; // EOF
	} else if (off < 0) {
		return off; // error
	}
	fd->offset = off;
	return 1;
}

int vfs_dir_close(struct FileDesc* fd) {
	if (!fd->used) {
		return ERROR_INVAILD;
	}
	if (!fd->dir) {
		return ERROR_INVAILD;
	}

	fd->used = 0;
	return 0;
}