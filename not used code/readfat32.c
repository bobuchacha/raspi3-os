#include "ros.h"
#include "readfat32.h"
#include "../../lib/string.h"
#include "../uart/uart.h"
#include "../../terminal/terminal.h"
#include "../../memory/paging.h"
#include "sd.h"


#define breakpoint asm volatile("brk #0");

int fileID;      // File reading from
ULONG bps;        // Bytes Per Sector
ULONG spc;    // Sectors Per Cluster
ULONG dps;    // DirEntry per sector
ULONG reservedSec;  // Reserved sector location
ULONG dataSecStart; // Starting point for data sector
ULONG FATSz32;
unsigned char *buffer;
unsigned long *FAT;
fat32BS bootSector;
DIR dirStruct;
FSI fsiStruct;

static unsigned short readi16(unsigned char *buff, int offset) {
    unsigned char *ubuff = buff + offset;
    return ubuff[1] << 8 | ubuff[0];
}

static unsigned int readi32(unsigned char *buff, int offset) {
    unsigned char *ubuff = buff + offset;
    return ((ubuff[3] << 24) & 0xFF000000) |
           ((ubuff[2] << 16) & 0x00FF0000) |
           ((ubuff[1] << 8) & 0x0000FF00) |
           (ubuff[0] & 0x000000FF);
}

/**
* convert short to int by browsing in memory
*/
int shortToInt(void *s) {
    unsigned char *buffer = s;
    return buffer[1] << 8 + buffer[0];
}

/**
 * identify the file system and ready basic information to memory
*/
int initFileSystem() {
    printf("---------------------INIT FILE SYSTEM --------------------------\n");
    int count;
    int numSectors;
    char *input;
    int rootDir_SecNum;
    unsigned int readLBA = 0;

    buffer = kmalloc(BUFFER_SIZE);

    // read first sector of sd card
    if (!sd_readblock(readLBA, buffer, 1)) {
        printf("initFileSystem: ERROR: Can not read first sector\n");
        return 0;
    }

    // copy boot sector to our memory
    memcpy(&bootSector, buffer, sizeof(fat32BS));


    // Get the boot sector information
    //bps = ((char*)&bootSector.BPB_BytesPerSec);           // compiler generate wierd code
    bps = shortToInt(&bootSector.BPB_BytesPerSec);
    //kfree(buffer);          // release above buffer
    spc = bootSector.BPB_SecPerClus;
    // create new buffer according to cluster size for better read

    numSectors = bps * bootSector.BPB_FATSz32 / 4;
    FATSz32 = bootSector.BPB_FATSz32;
    dps = bps / 32;
    reservedSec = bootSector.BPB_RsvdSecCnt;
    // Starting sector after FAT
    dataSecStart = bootSector.BPB_RsvdSecCnt + bootSector.BPB_NumFATs * bootSector.BPB_FATSz32;
    rootDir_SecNum = returnFirstSector(bootSector.BPB_RootClus);
    FAT = (unsigned long *) kmalloc(sizeof(unsigned long) * bootSector.BPB_FATSz32 * bps);

    // Read fsi
    readLBA = bootSector.BPB_FSInfo;
    sd_readblock(readLBA, buffer, 1);

    memcpy(&fsiStruct, buffer, sizeof(FSI));
    //check for valid Signature
    // if ((unsigned short)fsiStruct.FSI_TrailSig != 0xAA55) {
    //     kerror("initFileSystem:: Signatures don't match: 0x%lx\n", fsiStruct.FSI_TrailSig);
    // }

    ULONG freeSpace = fsiStruct.FSI_Free_Count * spc * bps;

    printf("============== [SD CARD INFORMATION] ==============\n");
    printf("Name:                   %s\n", bootSector.BS_OEMName);
    printf("Free Space:             %ld KB\n", freeSpace / 1024);
    printf("Bytes per Sector:       %ld\n", bps);
    printf("Sector per Cluster:     %ld\n", spc);
    printf("Number of FATs:         %d\n", bootSector.BPB_NumFATs);
    printf("Sectors Per FAT:        %ld\n", FATSz32);
    printf("Usable Storage:         %d\n", (bps / 4) * bps * spc);
    printf("Number of Clusters (Sectors):   %d\n", numSectors);
    printf("Number of Clusters (Kbs):       %d\n", numSectors * bps * spc / 1000);
    printf("===================================================\n");

    list_all_files();

}

// Returns the first sector number for cluster
ULONG returnFirstSector(ULONG cluster) {
    return dataSecStart + (cluster - 2) * spc;
}

void list_all_files() {

    kinfo("Listing all files ");
    dataSecStart = bootSector.BPB_RsvdSecCnt + bootSector.BPB_NumFATs * bootSector.BPB_FATSz32;


    // Read root directory
    memset(buffer, 0, BUFFER_SIZE);

    // Recursively go through the entire thing*/
    readDir(bootSector.BPB_RootClus, 0);
}

/**
 * read a directory entry
 * return pointer to DIR in buffer
*/
unsigned char *readEntry(ULONG sectorNum, unsigned char *buffer, ULONG numSectors, ULONG entryNum) {
    unsigned char *offset;     /* used for error checking only */

    // Read the sector
    sd_readblock(sectorNum, buffer, numSectors);

    //determine where entry is in buffer
    offset = buffer + (entryNum * 32);
    // printf("Entry number %d is in sector number %d, offset 0x%x\n", entryNum, sectorNum, offset);
    return offset;
}

static char *parse_long_name(unsigned char *entries, unsigned char entry_count) {
    // each entry can hold 13 characters.
    char *name = kmalloc(entry_count * 13);
    int i, j;
    for (i = 0; i < entry_count; i++) {
        unsigned char *entry = entries + (i * 32);
        unsigned char entry_no = (unsigned char) entry[0] & 0x0F;
        char *name_offset = name + ((entry_no - 1) * 13);

        for (j = 1; j < 10; j += 2) {
            if (entry[j] >= 32 && entry[j] <= 127) {
                *name_offset = entry[j];
            } else {
                *name_offset = 0;
            }
            name_offset++;
        }
        for (j = 14; j < 25; j += 2) {
            if (entry[j] >= 32 && entry[j] <= 127) {
                *name_offset = entry[j];
            } else {
                *name_offset = 0;
            }
            name_offset++;
        }
        for (j = 28; j < 31; j += 2) {
            if (entry[j] >= 32 && entry[j] <= 127) {
                *name_offset = entry[j];
            } else {
                *name_offset = 0;
            }
            name_offset++;
        }
    }
    return name;
}

static unsigned char read_dir_entry(unsigned char *start, struct dir_entry *dirent){
    unsigned char first_byte = start[0];
    unsigned char *entry = start;

    uart_dump(start);
}


// Given a directory, read all the files in this directory
void readDir(ULONG clusterNum, int loop) {
//    kdebug("reading directory at cluster %d\n", clusterNum);
    int sectorReadIndex;
    int directoryIndex;
    int k;
    char name[12];
    char nameNoSpaces[12];
    char nameNoSpace[12];
    DIR *currEntry;
    DIR *currRealEntry;
    unsigned char *currRealEntryStart;

    unsigned int FATSecOffset = (clusterNum * spc);
    unsigned int nextCluster;


    // Get base sector
    unsigned int baseSector = returnFirstSector(clusterNum);


    // Read sector in each cluster
    // read the cluster (each time read 512 bytes)
    for (sectorReadIndex = 0; sectorReadIndex < spc; sectorReadIndex++) {
        sd_readblock(baseSector + sectorReadIndex, buffer, 1);
        currEntry = (DIR *) buffer;
        for (directoryIndex = 0; directoryIndex < dps; directoryIndex++, currEntry++) {
            unsigned int LFNCount = 0;
            unsigned char *entryBaseAddr = (unsigned char *) currEntry;
            struct dir_entry my_entry;
            currRealEntryStart = entryBaseAddr;     // save this current base if we face a LFN
            currRealEntry = currEntry;              // save this current base if we face a LFN

            // deleted, skip
            if(entryBaseAddr[0] == 0x00 || entryBaseAddr[0] == 0xE5)
                continue;
            //uart_dump_size(entryBaseAddr, 32);

            // check for long file name
            // keep reading next entry until it ends
            while (entryBaseAddr[11] == LFN) {
                LFNCount++;
                entryBaseAddr = (unsigned char *) ++currEntry;
                directoryIndex++;
            }

            if (LFNCount == 0) {
                memcpy(name, currEntry->DIR_Name, 11);
                name[11] = '\0';
                // Remove space in name
                removeSpaces(name, nameNoSpace);
            }

           // printf("attr: %d  or 0x%x, index %d \n", currEntry->DIR_Attr, currEntry->DIR_Attr, directoryIndex);

            if ((currEntry->DIR_Attr == 16 || currEntry->DIR_Attr == 32) && directoryIndex > 2) {

                printf("|");
                for (k = 0; k < loop * 2; k++) {
                    printf("-");
                }

                if (currEntry->DIR_Attr == 16) {
                    printf("Directory: %s\n", nameNoSpace);
                    readDir(currEntry->DIR_FstClusLO, loop + 1);
                } else if (currEntry->DIR_Attr == 32) {
                    if (LFNCount > 0) {
                        unsigned char *filename = parse_long_name(currRealEntryStart, LFNCount);
                        printf("File: %s          size 0x%x 0x%x\n", filename, currEntry->DIR_FstClusHI, currEntry->DIR_FstClusLO);
                        kfree(filename);
                    } else {
                        appendDot(nameNoSpace);
                        printf("File: %s           size 0x%x 0x%x\n", nameNoSpace, currEntry->DIR_FstClusHI, currEntry->DIR_FstClusLO);
                    }
                }
            }
        }

    }

    // Insert thing here to check FAT to see if it continues
    unsigned char *FAT_ADDR;
    sd_readblock(reservedSec, buffer, 1);
    FAT_ADDR = (unsigned char *) buffer + FATSecOffset;
    nextCluster = readi32(FAT_ADDR, 0) & 0x0FFFFFFF;

    if (nextCluster < 0xffffff7) {
        // printf("0x%lx, %ld\n", nextCluster, nextCluster);
        // printf("Next cluster: %ld\n", nextCluster);
        // // printf("Sector Number: %ld\n", FATSecNum);
        // printf("Sector Offset: %ld\n", FATSecOffset);
        readDir(nextCluster, loop);
    }

}


static void trim_spaces(char *c, int max) {
    int i = 0;
    while (*c != ' ' && i++ < max) {
        c++;
    }
    if (*c == ' ')
        *c = 0;
}

// Remove spaces from given string
void removeSpaces(char source[], char dest[]) {
    int i;
    int j = 0;

    for (i = 0; i < strlen(source); i++) {
        if (source[i] != ' ') {
            dest[j] = source[i];
            j++;
        }
    }
    dest[j] = '\0';
}


void appendDot(char file[]) {
    int i;
    char temp = '.';
    char temp2;
    for (i = strlen(file) - 3; i < strlen(file) + 1; i++) {
        temp2 = file[i];
        file[i] = temp;
        temp = temp2;
    }
}

void print_directory(struct directory *dir) {
    unsigned int i;
    unsigned int max_name = 0;

    for(i = 0; i < dir->num_entries; i++) {
        unsigned int namelen = strlen(dir->entries[i].name);
        max_name = namelen > max_name ? namelen : max_name;
    }

    char *namebuff = kmalloc(max_name + 1);

    for(i = 0; i < dir->num_entries; i++) {
//        printf("[%d] %*s %c %8d bytes ",
//               i,
//               -max_name,
//               dir->entries[i].name,
//               dir->entries[i].dir_attrs & DIRECTORY?'D':' ',
//               dir->entries[i].file_size, dir->entries[i].first_cluster);
        printf("[%d] ", i);


        unsigned int j;
        for(j = 0; j < max_name; j++) {
            namebuff[j] = ' ';
        }
        namebuff[max_name] = 0;
        for(j = 0; j < strlen(dir->entries[i].name); j++) {
            namebuff[j] = dir->entries[i].name[j];
        }

        printf("%s %c %d ",
               namebuff,
               dir->entries[i].dir_attrs & DIRECTORY?'D':' ',
               dir->entries[i].file_size);

        unsigned int cluster = dir->entries[i].first_cluster;
        unsigned int cluster_count = 1;
        while(1) {
            cluster = FAT[cluster];
            if(cluster >= EOC) break;
            if(cluster == 0) {
                kpanic("BAD CLUSTER CHAIN! FS IS CORRUPT!");
            }
            cluster_count++;
        }
        printf("clusters: [%d]\n", cluster_count);
    }
    kfree(namebuff);
}