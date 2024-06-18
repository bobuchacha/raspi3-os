// /*
//  * Copyright (C) 2018 bzt (bztsrc@github)
//  *
//  * Permission is hereby granted, free of charge, to any person
//  * obtaining a copy of this software and associated documentation
//  * files (the "Software"), to deal in the Software without
//  * restriction, including without limitation the rights to use, copy,
//  * modify, merge, publish, distribute, sublicense, and/or sell copies
//  * of the Software, and to permit persons to whom the Software is
//  * furnished to do so, subject to the following conditions:
//  *
//  * The above copyright notice and this permission notice shall be
//  * included in all copies or substantial portions of the Software.
//  *
//  * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
//  * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
//  * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
//  * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  * DEALINGS IN THE SOFTWARE.
//  *
//  */

// #include "ros.h"
// #include "fat32.h"
// #include "sd.h"
// #include "../uart/uart.h"
// #include "../../terminal/terminal.h"
// #include "../../memory/paging.h"
// #include "../../lib/string.h"

// #define breakpoint asm volatile("brk #0");

// unsigned int bytes_per_sector;		    // Bytes Per Sector
// unsigned int sector_per_cluster;	    // Sectors Per Cluster
// unsigned int dir_entry_per_sector;      // DirEntry per sector
// unsigned int reserved_sectors_count;    // Reserved sector count
// unsigned int data_sector_start;         // Starting point for data sector
// unsigned int sectors_per_FAT;       
// unsigned int number_of_sectors;         // disk size in sectors    
// unsigned char *buffer;                  // general buffer
// unsigned int *FAT;                     // pointer to FAT in memory
// unsigned int root_dir_sector_number;    // sector number of start of root directory
// FILE_SYSTEM_INFO FSIStruct;                          // file system information

// static void read_bpb(f32 *fs, struct bios_parameter_block *bpb);
// static unsigned int sector_for_cluster(INT32 cluster);
// static void trim_spaces(char *c, int max);
// int read_int_16(void *s);

// int init_fat32_file_system(){
//     kinfo("Initializing File System");
    
//     // alloc buffer to use during file system procedures
//     buffer = kmalloc(512);


//     // read first sector of sd card
//     if (!sd_readblock(0, buffer, 1)){
//         kerror("init_fat32_file_system: ERROR: Can not read first sector\n");
//         return 0;
//     }

//     // reference our boot sector struct to buffer so we can easily copy information
//     BOOT_SECTOR *boot_sector = (BOOT_SECTOR *)buffer;

//     // get file system information
//     bytes_per_sector =      read_int_16(&boot_sector->BPB_BytesPerSec);
//     sector_per_cluster =    boot_sector->BPB_SecPerClus;
//     sectors_per_FAT =       boot_sector->BPB_FATSz32;
//     number_of_sectors =     bytes_per_sector * boot_sector->BPB_FATSz32 / 4;
//     dir_entry_per_sector =  bytes_per_sector/32;
//     reserved_sectors_count = boot_sector->BPB_RsvdSecCnt;
//     root_dir_sector_number = boot_sector->BPB_RootClus;

//     // read File System Information
//     sd_readblock(boot_sector->BPB_FSInfo, (unsigned char*)&FSIStruct, 1);

//     // display information
//     unsigned int freeSpace =  FSIStruct.FSI_Free_Count * sector_per_cluster * bytes_per_sector;

//     terminal_printf("Name:              %s", boot_sector->BS_OEMName);
//     terminal_printf("Free Space:        %ld KB", freeSpace/1024);
//     terminal_printf("Bytes per Sector:  %ld", bytes_per_sector);
//     terminal_printf("Sector per Cluster: %ld", sector_per_cluster);
//     terminal_printf("Number of FATs =   %d", boot_sector->BPB_NumFATs);
//     terminal_printf("Sectors Per FAT =  %ld", sectors_per_FAT);
//     terminal_printf("Usable Storage:    %d", (bytes_per_sector/4)*bytes_per_sector*sector_per_cluster);
//     terminal_printf("Number of Clusters (Sectors):  %d", number_of_sectors);
//     terminal_printf("Number of Clusters (Kbs):      %d", number_of_sectors * bytes_per_sector * sector_per_cluster / 1024);


//     // load FAT
//     memset(buffer, 0, BUFFER_SIZE);
//     sd_readblock(sector_for_cluster(root_dir_sector_number), buffer, 1);
//     uart_dump(buffer);
// }

// static void getSector(f32 *fs, INT8 *buff, INT32 sector, INT32 count) {
//     sd_readblock((unsigned int)sector, (unsigned char*)buff, (unsigned int)count);
// }

// // static void putSector(f32 *fs, INT8 *buff, INT32 sector, INT32 count) {
// //     INT32 i;
// //     for(i = 0; i < count; i++) {
// //         ata_pio_write48(sector + i, 1, buff + (i * 512));
// //     }
// // }

// static INT16 readi16(INT8 *buff, int offset)
// {
//     INT8 *ubuff = buff + offset;
//     return ubuff[1] << 8 | ubuff[0];
// }

// int read_int_16(void *s){
//         unsigned char *buffer = s;
//         return buffer[1] << 8 + buffer[0];
// }

// static INT32 readi32(INT8 *buff, int offset)
// {
//     INT8 *ubuff = buff + offset;
//     return ((ubuff[3] << 24) & 0xFF000000) |
//            ((ubuff[2] << 16) & 0x00FF0000) |
//            ((ubuff[1] << 8) & 0x0000FF00) |
//            (ubuff[0] & 0x000000FF);
// }

// /**
//  * 11 2 The number of Bytes per sector (remember, all numbers are in the little-endian format).
//  * 13 1 Number of sectors per cluster.
//  * 14 2 Number of reserved sectors. The boot record sectors are included in this value.
//  * 16 1 Number of File Allocation Tables (FAT's) on the storage media. Often this value is 2.
//  * 17 2 Number of directory entries (must be set so that the root directory occupies entire sectors).
//  * 19 2 The total sectors in the logical volume. If this value is 0, it means there are more than 65535 sectors in the volume, and the actual count is stored in "Large Sectors (bytes 32-35).
//  * 21 1 This Byte indicates the media descriptor type.
//  * 22 2 Number of sectors per FAT. FAT12/FAT16 only.
//  * 24 2 Number of sectors per track.
//  * 26 2 Number of heads or sides on the storage media.
//  * 28 4 Number of hidden sectors. (i.e. the LBA of the beginning of the partition.)
//  * 32 4 Large amount of sector on media. This field is set if there are more than 65535 sectors in the volume.
//  */

// /**
//  * 36 4 Sectors per FAT. The size of the FAT in sectors.
//  * 40 2 Flags.
//  * 42 2 FAT version number. The high byte is the major version and the low byte is the minor version. FAT drivers should respect this field.
//  * 44 4 The cluster number of the root directory. Often this field is set to 2.
//  * 48 2 The sector number of the FSInfo structure.
//  * 50 2 The sector number of the backup boot sector.
//  * 52 12 Reserved. When the volume is formated these bytes should be zero.
//  * 64 1 Drive number. The values here are identical to the values returned by the BIOS interrupt 0x13. 0x00 for a floppy disk and 0x80 for hard disks.
//  * 65 1 Flags in Windows NT. Reserved otherwise.
//  * 66 1 Signature (must be 0x28 or 0x29).
//  * 67 4 VolumeID 'Serial' number. Used for tracking volumes between computers. You can ignore this if you want.
//  * 71 11 Volume label string. This field is padded with spaces.
//  * 82 8 System identifier string. Always "FAT32   ". The spec says never to trust the contents of this string for any use.
//  * 90 420 Boot code.
//  */

// static void read_bpb(f32 *fs, struct bios_parameter_block *bpb)
// {
//     char sector0[512];
    
//     // read first sector from sd card
//     sd_readblock(0, sector0, 1);

//     bpb->bytes_per_sector = readi16(sector0, 11);
//     ;
//     bpb->sectors_per_cluster = sector0[13];
//     bpb->reserved_sectors = readi16(sector0, 14);
//     bpb->FAT_count = sector0[16];
//     bpb->dir_entries = readi16(sector0, 17);
//     bpb->total_sectors = readi16(sector0, 19);
//     bpb->media_descriptor_type = sector0[21];
//     bpb->count_sectors_per_FAT12_16 = readi16(sector0, 22);
//     bpb->count_sectors_per_track = readi16(sector0, 24);
//     bpb->count_heads_or_sizes_on_media = readi16(sector0, 26);
//     bpb->count_hidden_sectors = readi32(sector0, 28);
//     bpb->large_sectors_on_media = readi32(sector0, 32);
//     // EBR
//     bpb->count_sectors_per_FAT32 = readi32(sector0, 36);
//     bpb->flags = readi16(sector0, 40);
//     bpb->FAT_version = readi16(sector0, 42);
//     bpb->cluster_number_root_dir = readi32(sector0, 44);
//     bpb->sector_number_FSInfo = readi16(sector0, 48);
//     bpb->sector_number_backup_boot_sector = readi16(sector0, 50);
//     // Skip 12 bytes
//     bpb->drive_number = sector0[64];
//     bpb->windows_flags = sector0[65];
//     bpb->signature = sector0[66];
//     bpb->volume_id = readi32(sector0, 67);
//     memcpy(&bpb->volume_label, sector0 + 71, 11);
//     bpb->volume_label[11] = 0;
//     memcpy(&bpb->system_id, sector0 + 82, 8);
//     bpb->system_id[8] = 0;
// }

// static unsigned int sector_for_cluster(unsigned int cluster)
// {
//     return data_sector_start + ((cluster - 2) * sector_per_cluster);
// }

// f32 *fat_getpartition(){
//     f32 *fs = kmalloc(sizeof (struct f32));

//     // read first sector
//     // unsigned char* buffer = mem_alloc_page();        // get 4096 or a PAGE_SIZE memory
    
//     // if (sd_readblock(0, buffer, PAGE_SIZE / 512));

//     read_bpb(fs, &fs->bpb);

//     trim_spaces(fs->bpb.system_id, 8);
//     if(strcmp(fs->bpb.system_id, "FAT32") != 0) {
//         kfree(fs);
//         printf("ERROR: File system %s is not supported!\n", fs->bpb.system_id);
//         return NULL;
//     }
    
//     printf("fat_getpartition: Sectors per cluster: %d\n", fs->bpb.sectors_per_cluster);
//     fs->partition_begin_sector = 0;
//     fs->fat_begin_sector = fs->partition_begin_sector + fs->bpb.reserved_sectors;
//     fs->cluster_begin_sector = fs->fat_begin_sector + (fs->bpb.FAT_count * fs->bpb.count_sectors_per_FAT32);
//     fs->cluster_size = 512 * fs->bpb.sectors_per_cluster;
//     fs->cluster_alloc_hint = 0;

//     // Load the FAT
//     unsigned int bytes_per_fat = 512 * fs->bpb.count_sectors_per_FAT32;
//     fs->FAT = kmalloc(bytes_per_fat);
//     unsigned int sector_i;
    
//     for(sector_i = 0; sector_i < fs->bpb.count_sectors_per_FAT32; sector_i++) {
//         unsigned char sector[512];
//         getSector(fs, sector, fs->fat_begin_sector + sector_i, 1);
//         int integer_j;
//         for(integer_j = 0; integer_j < 512/4; integer_j++) {
//             fs->FAT[sector_i * (512 / 4) + integer_j]
//                 = readi32(sector, integer_j * 4);
//         }
//     }
//     uart_dump(fs->FAT);
//     return fs;

// }

// static char *parse_long_name(unsigned char *entries, unsigned char entry_count) {
//     // each entry can hold 13 characters.
//     char *name = kmalloc(entry_count * 13);
//     int i, j;
//     for(i = 0; i < entry_count; i++) {
//         unsigned char *entry = entries + (i * 32);
//         unsigned char entry_no = (unsigned char)entry[0] & 0x0F;
//         char *name_offset = name + ((entry_no - 1) * 13);

//         for(j = 1; j < 10; j+=2) {
//             if(entry[j] >= 32 && entry[j] <= 127) {
//                 *name_offset = entry[j];
//             }
//             else {
//                 *name_offset = 0;
//             }
//             name_offset++;
//         }
//         for(j = 14; j < 25; j+=2) {
//             if(entry[j] >= 32 && entry[j] <= 127) {
//                 *name_offset = entry[j];
//             }
//             else {
//                 *name_offset = 0;
//             }
//             name_offset++;
//         }
//         for(j = 28; j < 31; j+=2) {
//             if(entry[j] >= 32 && entry[j] <= 127) {
//                 *name_offset = entry[j];
//             }
//             else {
//                 *name_offset = 0;
//             }
//             name_offset++;
//         }
//     }
//     return name;
// }

// // Populates dirent with the directory entry starting at start
// // Returns a pointer to the next 32-byte chunk after the entry
// // or NULL if either start does not point to a valid entry, or
// // there are not enough entries to build a struct dir_entry
// static char *read_dir_entry(f32 *fs, char *start, char *end, struct dir_entry *dirent) {

//     char first_byte = start[0];
//     char *entry = start;
//     if(first_byte == 0x00 || first_byte == 0xE5) {
//         // NOT A VALID ENTRY!
//         return NULL;
//     }

//     int LFNCount = 0;
//     while(entry[11] == LFN) {
//         LFNCount++;
//         entry += 32;
//         if(entry == end) {
//             return NULL;
//         }
//     }
//     if(LFNCount > 0) {
//         dirent->name = parse_long_name(start, LFNCount);
//     }
//     else {
//         // There's no long file name.
//         // Trim up the short filename.
//         dirent->name = kmalloc(13);
//         memcpy(dirent->name, entry, 11);
//         dirent->name[11] = 0;
//         char extension[4];
//         memcpy(extension, dirent->name + 8, 3);
//         extension[3] = 0;
//         trim_spaces(extension, 3);

//         dirent->name[8] = 0;
//         trim_spaces(dirent->name, 8);

//         if(strlen(extension) > 0) {
//             int len = strlen(dirent->name);
//             dirent->name[len++] = '.';
//             memcpy(dirent->name + len, extension, 4);
//         }
//     }

//     dirent->dir_attrs = entry[11];;
//     unsigned short first_cluster_high = readi16(entry, 20);
//     unsigned short first_cluster_low = readi16(entry, 26);
//     dirent->first_cluster = first_cluster_high << 16 | first_cluster_low;
//     dirent->file_size = readi32(entry, 28);
//     return entry + 32;
// }

// void print_directory(f32 *fs, struct directory *dir) {
//     unsigned int i;
//     unsigned int max_name = 0;
//     for(i = 0; i < dir->num_entries; i++) {
//         unsigned int namelen = strlen(dir->entries[i].name);
//         max_name = namelen > max_name ? namelen : max_name;
//     }

//     char *namebuff = kmalloc(max_name + 1);
//     for(i = 0; i < dir->num_entries; i++) {
// //        printf("[%d] %*s %c %8d bytes ",
// //               i,
// //               -max_name,
// //               dir->entries[i].name,
// //               dir->entries[i].dir_attrs & DIRECTORY?'D':' ',
// //               dir->entries[i].file_size, dir->entries[i].first_cluster);
//         printf("[%d] ", i);


//         unsigned int j;
//         for(j = 0; j < max_name; j++) {
//             namebuff[j] = ' ';
//         }
//         namebuff[max_name] = 0;
//         for(j = 0; j < strlen(dir->entries[i].name); j++) {
//             namebuff[j] = dir->entries[i].name[j];
//         }

//         printf("%s %c %d ",
//                namebuff,
//                dir->entries[i].dir_attrs & DIRECTORY?'D':' ',
//                dir->entries[i].file_size);

//         unsigned int cluster = dir->entries[i].first_cluster;
//         unsigned int cluster_count = 1;
//         while(1) {
//             cluster = fs->FAT[cluster];
//             if(cluster >= EOC) break;
//             if(cluster == 0) {
//                 printf("PANIC:: BAD CLUSTER CHAIN! FS IS CORRUPT!");
//             }
//             cluster_count++;
//         }
//         printf("clusters: [%d]\n", cluster_count);
//     }
//     kfree(namebuff);
// }

// static void trim_spaces(char *c, int max)
// {
//     int i = 0;
//     while (*c != ' ' && i++ < max)
//     {
//         c++;
//     }
//     if (*c == ' ')
//         *c = 0;
// }