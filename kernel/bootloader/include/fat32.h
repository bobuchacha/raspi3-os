#ifndef KERNEL_BOOTLOADER_INCLUDE_FAT32_H
#define KERNEL_BOOTLOADER_INCLUDE_FAT32_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct BootFsFat32BootSector {
        U16 bytes_per_sector;
        U8 sectors_per_cluster;
        U16 reserved_sectors;
        U8 fat_count;
        U32 fat_size_32;
        U32 root_cluster;
        char fs_type[8];
    } BootFsFat32BootSector;

    typedef struct BootFsFat32DirEntry {
        U8 name[11];
        U8 attributes;
        U16 cluster_hi;
        U16 cluster_lo;
        U32 size;
    } BootFsFat32DirEntry;

    typedef struct BootFs {
        U32 partition_lba;
        BootFsFat32BootSector boot_sector;
        U32 fat_lba;
        U32 data_lba;
        U32 cluster_size;
    } BootFs;

    typedef struct BootFsFile {
        U32 first_cluster;
        U32 current_cluster;
        U32 current_cluster_index;
        U32 size;
        U32 offset;
    } BootFsFile;

    // Export functions

    /*
     * Initialize the FAT32 filesystem
     * @param filesystem The FAT32 filesystem to initialize
     * @return Status code indicating success or failure
     */
    Status bootfs_init(BootFs* filesystem);

    /*
     * Open a file in the FAT32 filesystem
     * @param filesystem The FAT32 filesystem
     * @param path The path to the file
     * @param file The file structure to initialize
     * @return Status code indicating success or failure
     */
    Status bootfs_open(BootFs* filesystem, const char* path, BootFsFile* file);


    /* Seek to a specific offset in a file
     * @param filesystem The FAT32 filesystem
     * @param file The file to seek
     * @param offset The offset to seek to
     * @return Status code indicating success or failure
     */
    Status bootfs_seek(BootFs* filesystem, BootFsFile* file, U32 offset);

    /*
    * Read data from a file
    * @param filesystem The FAT32 filesystem
    * @param file The file to read from
    * @param buffer The buffer to store the read data
    * @param size The number of bytes to read
    * @return The number of bytes read or an error code
    */
    SSize bootfs_read(BootFs* filesystem, BootFsFile* file, void* buffer, U32 size);

#ifdef __cplusplus
}
#endif

#endif