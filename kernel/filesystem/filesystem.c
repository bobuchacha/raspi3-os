//
// Created by Jeff on 5/7/2024.
//
#include "ros.h"
#include "filesystem.h"
#include "FAT32/myfat32.h"
#include "../hardware/sdcard/sd.h"
#include "../hardware/uart/uart.h"
#include "../lib/string.h"
#include "../lib/array.h"
#include "../memory/paging.h"

extern fsHANDLER f32_init();
static FILESYSTEM myFS;
fsHANDLER sdroot;

M_CREATE_ARRAY(String, unsigned char)

FILESYSTEM fs_init(){
    String thang = kmalloc(sizeof(String) + sizeof(unsigned char)*50);
    thang->length = 50;
    memcpy(thang->baseAddr, "Thang D Cao", 12);
    uart_dump(thang);
    printf("size of the array %d = %x", thang->length, thang->baseAddr[2]);

    ///kdebug("fs_init: Initializing File System Abstract Layer");
    // since we only support sdcard, init sdcard
    //if (!(sd_init() == SD_OK)) {
    //    kpanic("Can not initialize SD Card");
    //    return 0;
   //}
   
    //sdroot = f32_init();

    //HDirectory myDir = fs_getdir("/This is a long name Directory");

}
extern void dir_cache_dump();
/**
 * get content of directory. List of its entry
*/
HDirectory fs_getdir(char *path){
    unsigned char * lookup_name = strtok(path, "/");
    int i = 0;

    // first open root directory
    DirectoryEntryID currentFolder = 0; // lookup root first

    while (lookup_name != NULL){
        // open first folder in root cluster
        kinfo("fs_getdir: Looking for %s", lookup_name);
        currentFolder = fat32_lookup_folder(sdroot, currentFolder, (unsigned char*)lookup_name, NULL); // not look for unicode
        if (currentFolder == FS_NOT_FOUND) break;
        lookup_name = strtok(NULL, "/");    // get next sub folder name
        i++;
    }

    if (currentFolder == FS_NOT_FOUND) {
//        kdebug("fs_getdir: not found %s", lookup_name);
        return NULL;
    }

//    kinfo("found %s at %d", path, currentFolder);

//    dir_cache_dump();

    // found entry in currentFolder as an ID. Now we need to read its content and return
    return fat32_read_directory(sdroot, currentFolder);
}


/**
 * search for path and
 * @param name
 * @return
 */
HFile fs_fopen(char *path){
    kdebug("fs_open: Opening file %s", path);
}

/**
 * closes a file
 * @param handler
 */
void fs_fclose(HFile handler){
    kfree(handler);
}