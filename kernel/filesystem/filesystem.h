
//
// Created by Jeff on 5/7/2024.
//

#ifndef RASPO3B_OS_FILESYSTEM_H
#define RASPO3B_OS_FILESYSTEM_H
#define FS_NOT_FOUND -1

typedef void * fsHANDLER;           // handler of a file system
typedef int DirectoryEntryID;       // holds an ID to a file or directory. In FAT32 it is cluster number of that file

typedef enum file_system_format {
    FAT32 = 0
} fsFORMAT;

enum e_bool{
    true = !0,
    false = 0
};

#define bool enum e_bool;

typedef struct file_system_s {
    unsigned char *volume_name;     // name of the drive or file system
    fsFORMAT      format;           // format of the file system
    fsHANDLER      handler;         // point to handler of a file system
} *FILESYSTEM;


// when edit this struct make sure to sync changes with dir_cache.h DirectoryCacheEntry 
typedef struct __attribute((__packed__, aligned(4))) directory_entry_s {
        unsigned char short_name[13];            // name of the file or directory. 15 char so we can achieve alignment
        unsigned char checksum;         // checksum of the short name (for search)
        unsigned short long_name[256];       // long name of the entry - unicode
        unsigned char long_name_ascii[256];   // long name of the entry but non unicode
        unsigned int cluster_num;              // cluster number of the file or directory
        unsigned int file_size;                 // file size of the file
        unsigned int is_directory;          // is this a directory
} DirectoryEntry, *PDirectoryEntry, *HFile;
typedef struct __attribute((__packed__, aligned(4))) directory_content_s {
    int index;
    int num_of_entries;                     // total entries in this Directory
    int num_of_directories;                 // total sub directories
    int num_of_files;                       // total files in this directory
    int num_of_empty_entries;               // empty entry. when it is zero, initialize FAT32_DIR_CACHE_ENTRY_LENGTH_INCREMENT more entries
    int next_entry;                         // next empty entry
    PDirectoryEntry directories;
} DirectoryContent, *PDirectoryContent, *HDirectory;
// end of sync

/**
 * initialize file system used by root and return FILESYSTEM struct which we can use to handle
 * different file system task
 * @return {FILESYSTEM}
 */
FILESYSTEM fs_init();
HDirectory fs_getdir(char *path);       // read a directory
HFile fs_fopen(char* path);             // open a file
void fs_cflose(HFile handler);          // close a file
#endif //RASPO3B_OS_FILESYSTEM_H
