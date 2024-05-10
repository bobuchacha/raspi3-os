//
// Created by Jeff on 5/7/2024.
//
#include "ros.h"
#include "filesystem.h"
#include "FAT32/myfat32.h"
#include "../hardware/sdcard/sd.h"
#include "../hardware/uart/uart.h"
#include "../lib/string.h"
#include "../memory/paging.h"

extern fsHANDLER f32_init();
static FILESYSTEM myFS;

FILESYSTEM fs_init(){
    kdebug("fs_init: Initializing File System Abstract Layer");
    // since we only support sdcard, init sdcard
    if (!sd_init() == SD_OK) {
        kpanic("Can not initialize SD Card");
        return 0;
    }
   

    // we are now only support FAT32
    myFS->format = FAT32;
    kdebug("fs_init: Initializing File System Abstract Layer");

    myFS->handler = f32_init();
    kdebug("fs_init: Initializing File System Abstract Layer");

    f32FS_HANDLER myF32 = myFS->handler;

    HFile fn = fs_open("/BOO.TXT");
    return myFS;
}

/**
 * search for path and
 * @param name
 * @return
 */
HFile fs_open(char *path){
    kdebug("fs_open: Opening file %s", path);
    // now let's use / as root
    HFile file = (HFile)kmalloc(sizeof(struct file_entry_s));
    file->fs = myFS;


}

/**
 * closes a file
 * @param handler
 */
void fs_close(HFile handler){
    kfree(handler);
}