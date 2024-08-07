#include <ros.h>
#include "lib/string.h"
#include "filesystem.h"
#include "vfs/vfs.h"
#include "fat32/fat32.h"
#include "log.h"
#include "memory.h"

const struct FilesystemDriver* filesystem_fat32_driver;

/**
 * Register driver of file systen.
*/
void filesystem_register_driver(const struct FilesystemDriver* fs_driver) {
	if (strcmp(fs_driver->name, "fat32") == 0) {
		filesystem_fat32_driver = fs_driver;
	}
}

/**
 * initialize file system
*/
void filesystem_init(void) {
	fat32_init();
	vfs_init();
	



}