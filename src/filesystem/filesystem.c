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

/**
 * parse a path into multiple names. Note: This will modify our original path so
 * make a copy if needed
 * @param path
 * @param names
 * @return
 */
int parse_path(char* path, char* names[]) {
    char counter = 0;
    unsigned char * lookup_name = strtok(path, "/");
    while (lookup_name != NULL){
        names[counter++] = lookup_name;
        lookup_name = strtok(NULL, "/");
    }

    return counter;
}

FILESYSTEM fs_init(){
//    String str_array = darray_init(sizeof(String), 1);
//    printf("size: %d\n", str_array->_real_length);

    kdebug("fs_init: Initializing File System Abstract Layer");
    //since we only support sdcard, init sdcard
    if (!(sd_init() == SD_OK)) {
        kpanic("Can not initialize SD Card");
        return 0;
   }

//    sdroot = f32_init();
//    HFile myFile = fs_fopen("/This is a long name file.asd");
//    char *buffer = kmalloc(myFile->file_size);  // allocate memory for file content
//    bzero(buffer, myFile->file_size);           // clear buffer
//    int len = fs_read(myFile, buffer,0, myFile->file_size);
//    printf("Read %d bytes successfully.\n", len);
//    uart_dump(buffer);

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

    // if path is "/" then we get root directory
    if (strcmp("/", path) == 0) goto list_directory;

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

    list_directory:
    // found entry in currentFolder as an ID. Now we need to read its content and return
    return fat32_read_directory(sdroot, currentFolder);
}

void fs_print_directory_content(HDirectory dir){
    if (dir == NULL) return;
    PDirectoryEntry entry = dir->directories;
    printf("No.      ");
    printf("Size           ");
    printf("Name");
    printf("\n=================================================================\n");
    for (int i=0; i < dir->num_of_entries; i++, entry++){
        printf("%3d  ", i);
        printf("%s", entry->is_directory ? "D" : " ");
        if (!entry->is_directory) {
            if (entry->file_size < 1024) printf("   %d B", entry->file_size);
            else if (entry->file_size < 1024*1024) printf("   %d KB", entry->file_size/1024);
            else if (entry->file_size < 1024*1024*1024) printf("   %d MB", entry->file_size/1024/1024);
            else printf("   %d GB", entry->file_size/1024/1024/1024);
        }
        else printf("       ");
        printf("    %s", entry->long_name_ascii);
        printf("\n");
    }
}


/**
 * search for path and
 * @param name
 * @return
 */
HFile fs_fopen(char *path){

    // first open root directory
    DirectoryEntryID currentFolder = 0; // lookup root first
    char *names[255];
    char path_length = parse_path(path, names);
    for (int i = 0; i < path_length; i++){
        if (i==path_length-1){
            // this is the file name.
            // lookup file in currentDirectory
            return fat32_lookup_file(sdroot,
                                     currentFolder,
                                     names[i],
                                     NULL); // no unicode yet
        }else {
            // change current Directory
            currentFolder = fat32_lookup_folder(sdroot,
                                                currentFolder,
                                                names[i],
                                                NULL); // we dont use unicode yet
            if (currentFolder == FS_NOT_FOUND) break;
        }
        printf("%3d %s\n", i, names[i]);
    }

    // foun dnothing
    return NULL;
}

/**
 * read file at a position and save to buffer
 * @param file
 * @param position
 * @param length
 * @return bytes written to buffer
 */
int fs_read(HFile file, void* buffer, int position, int length){
    return fat32_read_file(sdroot, file, buffer, position, length);
}

/**
 * closes a file
 * @param handler
 */
void fs_fclose(HFile handler){
    kfree(handler);
}