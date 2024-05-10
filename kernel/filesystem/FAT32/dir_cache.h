//
// Created by bobuc on 5/8/2024.
//

#ifndef DIR_CACHE_H
#define DIR_CACHE_H

// every time if a cache entry length hit this number, we will reallocate the memory to expand the cache
#define FAT32_DIR_CACHE_ENTRY_LENGTH_INCREMENT 10;

typedef struct fat32_cache_entry_s {
        unsigned char short_name[13];            // name of the file or directory. 15 char so we can achieve alignment
        unsigned char checksum;         // checksum of the short name (for search)
        unsigned short long_name[256];       // long name of the entry - unicode
        unsigned char long_name_ascii[256];   // long name of the entry but non unicode
        unsigned int cluster_num;              // cluster number of the file or directory
        unsigned int file_size;                 // file size of the file
        unsigned int is_directory;          // is this a directory
} DirectoryCacheEntry, *PDirectoryCacheEntry;

typedef struct fat32_cache_s {
    int index;
    int num_of_entries;                     // total entries in this Directory
    int num_of_directories;                 // total sub directories
    int num_of_files;                       // total files in this directory
    int num_of_empty_entries;               // empty entry. when it is zero, initialize FAT32_DIR_CACHE_ENTRY_LENGTH_INCREMENT more entries
    int next_entry;                         // next empty entry
    PDirectoryCacheEntry cache_start;
} DirectoryContentCache, *PDirectoryContentCache;


void dir_cache_alloc();
void dir_cache_reset();
void dir_cache_add(PDirectoryCacheEntry entry);     // add an entry to cache
void dir_cache_dump();                              // dump it
unsigned int dir_cache_get_size();                  // return size of directory cache for memcpy
PDirectoryContentCache dir_cache_get_pointer();     // get Entry array pointer

#endif //DIR_H