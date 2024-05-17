//
// Created by Jeff on 5/8/2024.
//
#include "dir_cache.h"
#include "../../hardware/sdcard/sd.h"
#include "../../hardware/uart/uart.h"
#include "../../lib/string.h"
#include "../../memory/paging.h"

static DirectoryContentCache directoryContentCache;
static int _added = 0;
void dir_cache_alloc() {
    int cacheIncNum = directoryContentCache.num_of_entries + FAT32_DIR_CACHE_ENTRY_LENGTH_INCREMENT;
//    kdebug("dir_cache_alloc::Initializing %d more directory entries for cache", cacheIncNum);
    directoryContentCache.cache_start = krealloc(directoryContentCache.cache_start,
                                                 sizeof(DirectoryCacheEntry) * (cacheIncNum)); // pad 1KB info incase of over flow for some reason
//    directoryContentCache.cache_start = kmalloc(sizeof(DirectoryCacheEntry) * cacheIncNum);
    directoryContentCache.num_of_empty_entries = FAT32_DIR_CACHE_ENTRY_LENGTH_INCREMENT;
}

void dir_cache_reset() {

    if (directoryContentCache.cache_start) {
        kfree(directoryContentCache.cache_start);
    }
    directoryContentCache.num_of_entries       = 0;
    directoryContentCache.num_of_directories   = 0;
    directoryContentCache.num_of_files         = 0;
    directoryContentCache.num_of_empty_entries = 0;
    directoryContentCache.next_entry           = 0;
    directoryContentCache.index                 = 0;
    _added = 0; // debug only
    // reinitialize the cache
    dir_cache_alloc();
}

void dir_cache_add(PDirectoryCacheEntry entry) {
    // alloc more memory if no room left for next one
//    kinfo("%3d - dir_cache_add called for this directory, current num entries is %d", ++_added, directoryContentCache.num_of_empty_entries);
    if (directoryContentCache.num_of_empty_entries == 0) {
//        kdebug("dir_cache_add: We need more room for entries. We now have %d. Allocate more...\n\n", directoryContentCache.num_of_entries);
        dir_cache_alloc();
    }
    void* next_cache_addr = directoryContentCache.cache_start + directoryContentCache.next_entry;

    // copy information
    memcpy(next_cache_addr,
           entry,
           sizeof(DirectoryCacheEntry));


    //directoryContentCache.cache_start++;
    directoryContentCache.num_of_entries++;        // add total entries
    directoryContentCache.next_entry++;            // next entry after this
    directoryContentCache.index++;
    directoryContentCache.num_of_empty_entries--;  // decrease num of empty entries
    if (entry->is_directory) directoryContentCache.num_of_directories++;
    else directoryContentCache.num_of_files++;
}
//OK
PDirectoryContentCache dir_cache_get_pointer(){
    return &directoryContentCache;
}

DirectoryContentCache dir_cache_get(){
    return directoryContentCache;
}

void dir_cache_dump(){
    printf("========================= [DIR CACHE DUMP] =========================\n");
    printf("- Total Entries:            %d\n", directoryContentCache.num_of_entries);
    printf("- Total Files:              %d\n", directoryContentCache.num_of_files);
    printf("- Total Directories         %d\n", directoryContentCache.num_of_directories);
    printf("- Free Entries              %d\n", directoryContentCache.num_of_empty_entries);
    printf("- Address of Array          0x%x\n", directoryContentCache.cache_start);
    printf("Content:\n");

    PDirectoryCacheEntry entry = directoryContentCache.cache_start;
    //printf("Address from pre-dump 0x%x\n", &directoryContentCache.cache_start);

    for (int i = 0; i < directoryContentCache.num_of_entries; i++, entry++) {
        printf("%d\t%d %d\t%8s\t\t%s\n", i, entry->file_size, entry->cluster_num, entry->short_name, entry->long_name_ascii);
    }

    //printf("Address from dump 0x%x\n", directoryContentCache);
    printf("====================================================================\n");
}

unsigned int dir_cache_get_size(){

}