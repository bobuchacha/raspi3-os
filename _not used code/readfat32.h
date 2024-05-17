#ifndef READFAT32_H
#define READFAT32_H

/* boot sector constants */
#define BS_OEMName_LENGTH 8
#define BS_VolLab_LENGTH 11
#define BS_FilSysType_LENGTH 8 
#define ULONG unsigned long 
#define BUFFER_SIZE 512

#define READONLY  1
#define HIDDEN    (1 << 1)
#define SYSTEM    (1 << 2)
#define VolumeID  (1 << 3)
#define DIRECTORY (1 << 4)
#define ARCHIVE   (1 << 5)
#define LFN (READONLY | HIDDEN | SYSTEM | VolumeID)

// EOC = End Of Chain
#define EOC 0x0FFFFFF8

#pragma pack(push)
#pragma pack(1)
struct fsi_struct {
	unsigned int FSI_LeadSig;	
	unsigned char FSI_Reserved1[480];	
	unsigned int FSI_StrucSig;		
	unsigned int FSI_Free_Count;		
	unsigned int FSI_Nxt_Free;		
	unsigned char FSI_Reserved2[12];	
	unsigned int FSI_TrailSig; 
};
struct dir_struct {
	unsigned char DIR_Name[11];
	unsigned char DIR_Attr;
	unsigned char DIR_NTRes;
	unsigned char DIR_CrtTimeTenth;
	unsigned short DIR_CrtTime;
	unsigned short DIR_CrtDate;
	unsigned short DIR_LstAccDate;
	unsigned short DIR_FstClusHI;
	unsigned short DIR_WrtTime;
	unsigned short DIR_WrtDate;
	unsigned short DIR_FstClusLO;
    unsigned int DIR_FileSize;
};
struct fat32BS_struct {
	char BS_jmpBoot[3];
	char BS_OEMName[BS_OEMName_LENGTH];
	unsigned short BPB_BytesPerSec;
	unsigned char BPB_SecPerClus;
	unsigned short BPB_RsvdSecCnt;
	unsigned char BPB_NumFATs;
	unsigned short BPB_RootEntCnt;
	unsigned short BPB_TotSec16;
	unsigned char BPB_Media;
	unsigned short BPB_FATSz16;
	unsigned short BPB_SecPerTrk;
	unsigned short BPB_NumHeads;
	unsigned int BPB_HiddSec;
	unsigned int BPB_TotSec32;
	unsigned int BPB_FATSz32;
	unsigned short BPB_ExtFlags;
	unsigned char BPB_FSVerLow;
	unsigned char BPB_FSVerHigh;
	unsigned int BPB_RootClus;
	unsigned short BPB_FSInfo;
	unsigned short BPB_BkBootSec;
	char BPB_reserved[12];
	unsigned char BS_DrvNum;
	unsigned char BS_Reserved1;
	unsigned char BS_BootSig;
	unsigned int BS_VolID;
	char BS_VolLab[BS_VolLab_LENGTH];
	char BS_FilSysType[BS_FilSysType_LENGTH];
	char BS_CodeReserved[420];
	unsigned char BS_SigA;
	unsigned char BS_SigB;
};
struct dir_entry {
    char *name;
    unsigned char dir_attrs;
    unsigned int first_cluster;
    unsigned int file_size;
};

struct directory {
    unsigned int cluster;
    struct dir_entry *entries;
    unsigned int num_entries;
};
#pragma pack(pop)


typedef struct dir_struct       DIR;
typedef struct fsi_struct       FSI;
typedef struct fat32BS_struct   FAT32BS, fat32BS;


ULONG getClusterNum(DIR dirEntry);
ULONG returnFirstSector (ULONG cluster);
void readDir(ULONG clusterNum, int loop);
int readSector(int sectorNum, unsigned char *buffer, int numSectors);
unsigned char *readEntry(ULONG sectorNum, unsigned char *buffer, ULONG numSectors, ULONG entryNum) ;
void removeSpaces(char source[], char dest[]);
void readFile(ULONG clusterNum);
int readFAT(ULONG sectorNum, unsigned char *buffer, ULONG offset);
int findFile(ULONG clusterNum, char *fileName, DIR * entry);
void appendDot(char file[]);

int initFileSystem();
void list_all_files();
#endif