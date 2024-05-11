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
fsHANDLER sdroot;

FILESYSTEM fs_init(){
    kdebug("fs_init: Initializing File System Abstract Layer");
    // since we only support sdcard, init sdcard
    if (!sd_init() == SD_OK) {
        kpanic("Can not initialize SD Card");
        return 0;
    }
   
    sdroot = f32_init();

    HDirectory myDir = fs_getdir("/ROS/Copyright");
}

extern void dir_cache_dump();

/**
 * get content of directory. List of its entry
*/
HDirectory fs_getdir(char *path){
    unsigned char * component = strtok(path, "/");
    int i = 0;
    
    // first open root directory
    HDirectory hDir = fat32_read_root_directory(sdroot);
    // PDirectoryEntry entry = hDir->directories;

    // printf("========================= [DIR CONTENT] =========================\n");
    // printf("- Total Entries:            %d\n", hDir->num_of_entries);
    // printf("- Total Files:              %d\n", hDir->num_of_files);
    // printf("- Total Directories         %d\n", hDir->num_of_directories);
    // printf("- Free Entries              %d\n", hDir->num_of_empty_entries);
    // printf("- Address of Array          0x%x\n", hDir->directories);
    // printf("Content:\n");

    // for (int i=0; i < hDir->num_of_entries; i++, entry++){
    //     printf("%d: %s\n", i, entry->short_name);
    // }

    // dir_cache_dump();

    while (component != NULL){
        // open first folder in root cluster

        component = strtok(NULL, "/");    // get next sub folder name
        i++;
    }    
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