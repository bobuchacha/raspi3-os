//
// Created by Jeff on 5/7/2024.
//

#ifndef RASPO3B_OS_FILESYSTEM_H
#define RASPO3B_OS_FILESYSTEM_H


typedef void * fsHANDLER;   // handler of a file system

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

typedef struct file_entry_s {
    FILESYSTEM  fs;                 // file system of this file
    unsigned int is_directory;      // if this is a directory. !0 - yes; 0 - no
    unsigned char *name;            // name of file
    unsigned int attr;              // attrib of file
    unsigned int size;              // size of file
    unsigned int fs_reference;      // file system reference. FAT32 it is cluster number to read file
} FileHandler, *HFile;                           // Handler to a file, which is pointer to struct file_entry_s

/**
 * initialize file system used by root and return FILESYSTEM struct which we can use to handle
 * different file system task
 * @return {FILESYSTEM}
 */
FILESYSTEM fs_init();
HFile fs_open(char* path);
void fs_close(HFile handler);
#endif //RASPO3B_OS_FILESYSTEM_H
