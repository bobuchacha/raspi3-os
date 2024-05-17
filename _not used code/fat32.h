// #ifndef FAT32_H
// #define FAT32_H

// #include "ros.h"

// struct bios_parameter_block {
//     INT16 bytes_per_sector;          // IMPORTANT
//     INT8 sectors_per_cluster;        // IMPORTANT
//     INT16 reserved_sectors;          // IMPORTANT
//     INT8 FAT_count;                  // IMPORTANT
//     INT16 dir_entries;
//     INT16 total_sectors;
//     INT8 media_descriptor_type;
//     INT16 count_sectors_per_FAT12_16; // FAT12/FAT16 only.
//     INT16 count_sectors_per_track;
//     INT16 count_heads_or_sizes_on_media;
//     INT32 count_hidden_sectors;
//     INT32 large_sectors_on_media;  // This is set instead of total_sectors if it's > 65535

//     // Extended Boot Record
//     INT32 count_sectors_per_FAT32;   // IMPORTANT
//     INT16 flags;
//     INT16 FAT_version;
//     INT32 cluster_number_root_dir;   // IMPORTANT
//     INT16 sector_number_FSInfo;
//     INT16 sector_number_backup_boot_sector;
//     INT8 drive_number;
//     INT8 windows_flags;
//     INT8 signature;                  // IMPORTANT
//     INT32 volume_id;
//     char volume_label[12];
//     char system_id[9];
// };

// #define READONLY  1
// #define HIDDEN    (1 << 1)
// #define SYSTEM    (1 << 2)
// #define VolumeID  (1 << 3)
// #define DIRECTORY (1 << 4)
// #define ARCHIVE   (1 << 5)
// #define LFN (READONLY | HIDDEN | SYSTEM | VolumeID)
// #define BS_OEMName_LENGTH 8
// #define BS_VolLab_LENGTH 11
// #define BS_FilSysType_LENGTH 8 
// #define ULONG unsigned long 
// #define BUFFER_SIZE 512

// #define BS_OEMName_LENGTH 8
// #define BS_VolLab_LENGTH 11
// #define BS_FilSysType_LENGTH 8 
// #define ULONG unsigned long 
// #define BUFFER_SIZE 512

// struct dir_entry {
//     char *name;
//     INT8 dir_attrs;
//     INT32 first_cluster;
//     INT32 file_size;
// };

// struct directory {
//     INT32 cluster;
//     struct dir_entry *entries;
//     INT32 num_entries;
// };

// #pragma pack(push)
// #pragma pack(1)
// struct file_system_info_struct {
// 	unsigned int FSI_LeadSig;	
// 	unsigned char FSI_Reserved1[480];	
// 	unsigned int FSI_StrucSig;		
// 	unsigned int FSI_Free_Count;		
// 	unsigned int FSI_Nxt_Free;		
// 	unsigned char FSI_Reserved2[12];	
// 	unsigned int FSI_TrailSig; 
// };
// struct fat32BS_struct {
// 	char BS_jmpBoot[3];
// 	char BS_OEMName[BS_OEMName_LENGTH];
// 	unsigned short BPB_BytesPerSec;
// 	unsigned char BPB_SecPerClus;
// 	unsigned short BPB_RsvdSecCnt;
// 	unsigned char BPB_NumFATs;
// 	unsigned short BPB_RootEntCnt;
// 	unsigned short BPB_TotSec16;
// 	unsigned char BPB_Media;
// 	unsigned short BPB_FATSz16;
// 	unsigned short BPB_SecPerTrk;
// 	unsigned short BPB_NumHeads;
// 	unsigned int BPB_HiddSec;
// 	unsigned int BPB_TotSec32;
// 	unsigned int BPB_FATSz32;
// 	unsigned short BPB_ExtFlags;
// 	unsigned char BPB_FSVerLow;
// 	unsigned char BPB_FSVerHigh;
// 	unsigned int BPB_RootClus;
// 	unsigned short BPB_FSInfo;
// 	unsigned short BPB_BkBootSec;
// 	char BPB_reserved[12];
// 	unsigned char BS_DrvNum;
// 	unsigned char BS_Reserved1;
// 	unsigned char BS_BootSig;
// 	unsigned int BS_VolID;
// 	char BS_VolLab[BS_VolLab_LENGTH];
// 	char BS_FilSysType[BS_FilSysType_LENGTH];
// 	char BS_CodeReserved[420];
// 	unsigned char BS_SigA;
// 	unsigned char BS_SigB;
// };
// #pragma pack(pop)

// // REFACTOR
// // I want to get rid of this from the header. This should be internal
// // implementation, but for now, it's too convenient for stdio.c impl.

// // EOC = End Of Chain
// #define EOC 0x0FFFFFF8

// struct f32 {
//     //FILE *f;
//     INT32 *FAT;
//     struct bios_parameter_block bpb;
//     INT32 partition_begin_sector;
//     INT32 fat_begin_sector;
//     INT32 cluster_begin_sector;
//     INT32 cluster_size;
//     INT32 cluster_alloc_hint;
// };

// typedef struct f32              f32;
// typedef struct file_system_info_struct          FILE_SYSTEM_INFO;
// typedef struct fat32BS_struct                   BOOT_SECTOR;
// void getCluster(f32 *fs, INT8 *buff, INT32 cluster_number);
// INT32 get_next_cluster_id(f32 *fs, INT32 cluster);

// // END REFACTOR

// f32 *makeFilesystem(char *fatSystem);
// void destroyFilesystem(f32 *fs);

// const struct bios_parameter_block *getBPB(f32 *fs);

// void populate_root_dir(f32 *fs, struct directory *dir);
// void populate_dir(f32 *fs, struct directory *dir, INT32 cluster);
// void free_directory(f32 *fs, struct directory *dir);

// //INT8 *readFile(f32 *fs, struct dir_entry *dirent);
// void writeFile(f32 *fs, struct directory *dir, INT8 *file, char *fname, INT32 flen);
// void mkdir(f32 *fs, struct directory *dir, char *dirname);
// void delFile(f32 *fs, struct directory *dir, char *filename);

// void print_directory(f32 *fs, struct directory *dir);
// INT32 count_free_clusters(f32 *fs);

// extern f32 *master_fs;
// f32 *fat_getpartition();

// int init_fat32_file_system();
// #endif