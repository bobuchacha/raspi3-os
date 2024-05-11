//
// Created by Jeff on 5/7/2024.
//

#ifndef RASPO3B_OS_MYFAT32_H
#define RASPO3B_OS_MYFAT32_H

#include "../filesystem.h"

#define ATTR_READONLY  1
#define ATTR_HIDDEN    (1 << 1)
#define ATTR_SYSTEM    (1 << 2)
#define ATTR_VOLUME_ID  (1 << 3)
#define ATTR_DIRECTORY (1 << 4)
#define ATTR_ARCHIVE   (1 << 5)
#define ATTR_LFN (ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)
#define ATTR_LFN_MASK (ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID | ATTR_DIRECTORY | ATTR_ARCHIVE)
#define EOC 0x0FFFFFF8          // EOC = End Of Chain
//#undef NULL
//#define NULL (void*)0;
typedef struct __attribute__((packed)) boot_sector
{
    unsigned char	BS_jmpBoot[3];
    unsigned char	oem[8];

    unsigned short	BPB_BytesPerSector; //sector_size
    unsigned char	BPB_SectorsPerCluster;
    unsigned short	BPB_RsvdSecCnt; //reserved sectors
    unsigned char	BPB_NumberofFATS;
    unsigned short	BPB_RootEntCnt; //root_dir_entries
    unsigned short	BPB_TotalSectorsShort;
    unsigned char	BPB_MediaDescriptor;
    unsigned short	BPB_FATSz16; //fat_size_sectors
    unsigned short	BPB_SectorsPerTrack;
    unsigned short	BPB_NumberOfHeads;
    unsigned int	BPB_HiddenSectors;
    unsigned int	BPB_TotalSectorsLong;


    unsigned int	BPB_FATSz32;
    unsigned short	BPB_ExtFlags;
    unsigned short	BPB_FSVer;
    unsigned int	BPB_RootCluster;


    unsigned short	BPB_FSInfo;
    unsigned short	BPB_BkBootSec;
    unsigned char	BPB_Reserved[12];
    unsigned char	BS_DrvNum;
    unsigned char	BS_Reserved1;
    unsigned char	BS_BootSig;
    unsigned int	BS_VolID;
    unsigned char	BS_VolLab[11];
    unsigned char	BS_FilSysType[8];
    char BS_CodeReserved[420];
    unsigned short BS_Sig;
} FAT32BootBlock;

typedef struct
{
    unsigned int 	LeadSig;		// 4
    unsigned char  	Reserved1[480]; // 480
    unsigned int 	StructSig;	    // 4
    unsigned int 	FreeCount;		// 4
    unsigned int 	NextFree;		// 4
    unsigned char 	Reserved2[12];	// 12
    unsigned int 	TrailSig;		// 4
} __attribute__((packed)) FSI;


typedef struct __attribute__((__packed__)) fat32_descriptor_s {
    char BS_OEMName[8];             // OEM NAME
    int  BPB_BytesPerSec;
    int  BPB_SecPerClus;
    int  BPB_RsvdSecCnt;
    int  BPB_NumFATs;
    int  BPB_RootEntCnt;
    int  BPB_FATSz32;
    int  BPB_RootClus;
    int  BPB_FSInfo;

    FSI * fsi;
    FAT32BootBlock *bpb;
    unsigned char *cluster_buffer;

    // FAT32
    int  RootDirSectors;             //Amount of root directory sectors
    int  FirstDataSector;            //Where the first data sector exists in the fat32 file image.
    int  FirstSectorofCluster;       //First sector of the data cluster exists at point 0 in the fat32 file image.
    int TotalDataSectors;       // total sectors of data
    int CountofClusters;        // total clusters of data
    int TotalSectors;           // total sectors of the volume
    int ClusterSize;            // Cluster Size in Bytes
    int EntriesPerCluster;      // Number of FAT32 Directory Entries per cluster
    char BPB_Volume[12];//String to store the volume of the fat32 file image

} f32FS_S, *f32FS_HANDLER;



/**
 * initialize fat 32 file system and return handler (pointer to our FS descriptor)
 * @return
 */
fsHANDLER f32_init();


// interface

HDirectory fat32_read_root_directory(f32FS_HANDLER fs);
HDirectory fat32_read_directory(f32FS_HANDLER fs,int cluster_num);

#endif //RASPO3B_OS_MYFAT32_H
