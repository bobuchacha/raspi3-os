//
// Created by Jeff on 5/7/2024.
//
#include "myfat32.h"
#include "dir.h"
#include "dir_cache.h"
#include "../filesystem.h"
#include "../../hardware/sdcard/sd.h"
#include "../../hardware/uart/uart.h"
#include "../../lib/string.h"
#include "../../lib/util.h"
#include "../../memory/paging.h"

#define breakpoint asm volatile("brk #0");

static int get_clusters_first_sector(f32FS_HANDLER f32, int cluster_number);

void read_cluster(f32FS_HANDLER fs, unsigned char *buff, unsigned int cluster_number);

void read_sectors(f32FS_HANDLER fs, void *buff, unsigned int sector, unsigned int count);

int do_directory_read(f32FS_HANDLER fs, int cluster_num);

void print_file_system_info(f32FS_HANDLER fs);

int do_directory_read_cluster(f32FS_HANDLER fs, int dir_start_cluster, int is_chain_load);

static void read_long_name(unsigned char *lfn_entries, unsigned char count, unsigned char *buffer, unsigned short* buffer_unicode);
int read_fat_entry(f32FS_HANDLER fs, int cluster_num, int fat_index);
void write_fat_entry(f32FS_HANDLER fs, int cluster_num, int fat_index, int value);

DirectoryContentCache directoryContentCache;
void formatDirectory(char *dirname, char* dest);
void format_short_name(unsigned char *DIR_Name, unsigned char *dest);
static void trim_spaces(char *c, int max);
static unsigned char FAT_sector[512];
static int current_FAT_sector;

static void trim_spaces(char *c, int max) {
    int i = 0;
    while (*c != ' ' && i++ < max) {
        c++;
    }
    if (*c == ' ') *c = 0;
}

void format_short_name(unsigned char *DIR_Name, unsigned char *dest){

    //uart_dump_size(DIR_Name, 16);

    char file_extension[4]; // 4 byte short name extension XXX\0
    char file_name[13];
    memcpy(file_extension, DIR_Name + 8, 3);
    memcpy(file_name, DIR_Name, 8);

    file_extension[3] = 0;  // extension end with \0
    file_name[8] = 0;  // keep only 8 bytes of the short name
    trim_spaces(file_extension, 3);
    trim_spaces(file_name, 8);

    if (strlen(file_extension) > 0) {
        unsigned int len = strlen(file_name);
        file_name[len++] = '.';
        memcpy(file_name + len, file_extension, 4);
    }

    memcpy(dest, file_name, strlen(file_name)+1);       // null at the end
//    uart_dump_size(file_name, 16);
}


void formatDirectory(char *dirname, char* dest) {
    char expanded_name[12];
    char formattedDirectory[30];
    memset(expanded_name, ' ', 12);

    char *token = strtok(dirname, ".");

    if (token) {
        strncpy(expanded_name, token, strlen(token));

        token = strtok(NULL, ".");

        if (token) {
            strncpy((char *) (expanded_name + 8), token, strlen(token));
        }

        expanded_name[11] = '\0';

        int i;
        for (i = 0; i < 11; i++) {
            expanded_name[i] = k_toupper(expanded_name[i]);
        }
    }
    else {
        strncpy(expanded_name, dirname, strlen(dirname));
        expanded_name[11] = '\0';
    }
    strncpy(dest, expanded_name, 12);
}

fsHANDLER f32_init() {
    kdebug("f32_init: Initializing FAT32 File System...");
    f32FS_HANDLER f32   = kmalloc(sizeof(f32FS_S));  // allocate a struct size of fat 32
    unsigned char *buff = kmalloc(512);              // allocate 512 bytes for sector buffer

    // read BPB
    // read 1st sector of the disk
    read_sectors(f32, buff, 0, 1);
    bzero(f32, sizeof(f32FS_S));

    f32->bpb = kmalloc(sizeof(FAT32BootBlock));
    memcpy(&f32->BS_OEMName, buff + 3, 8);
    memcpy(&f32->BPB_Volume, buff + 43, 11);
    memcpy(f32->bpb, buff, sizeof(FAT32BootBlock));

    f32->BPB_BytesPerSec = buff[12] << 8 + buff[11];
    f32->BPB_SecPerClus  = buff[13] & 0x000000FF;
    f32->BPB_RsvdSecCnt  = readi16(buff, 14);
    f32->BPB_NumFATs     = buff[16] & 0x000000FF;
    f32->TotalSectors    = readi32(buff, 32);
    f32->BPB_FATSz32     = readi32(buff, 36);
    f32->BPB_RootClus    = readi32(buff, 44);
    f32->BPB_RootEntCnt  = buff[18] << 8 + buff[17];     // FAT12/16 only, Fat 32 is zero
    f32->RootDirSectors =
            ((f32->BPB_RootEntCnt * 32) + (f32->BPB_BytesPerSec - 1)) / f32->BPB_BytesPerSec;   // Always 0 in FAT32
    f32->FirstDataSector = f32->BPB_RsvdSecCnt + (f32->BPB_NumFATs * f32->BPB_FATSz32) + f32->RootDirSectors;
    f32->TotalDataSectors =
            f32->TotalSectors - (f32->BPB_RsvdSecCnt + (f32->BPB_NumFATs * f32->BPB_FATSz32) + f32->RootDirSectors);
    f32->CountofClusters      = f32->TotalDataSectors / f32->BPB_SecPerClus;
    f32->FirstSectorofCluster = get_clusters_first_sector(f32, 0);
    f32->ClusterSize          = f32->BPB_BytesPerSec * f32->BPB_SecPerClus;
    f32->BPB_FSInfo           = readi16(buff, 48);
    f32->EntriesPerCluster    = f32->ClusterSize / 32;
    // read FSI
    read_sectors(f32, buff, f32->BPB_FSInfo, 1);
    f32->fsi = kmalloc(sizeof(FSI));
    memcpy(f32->fsi, buff, sizeof(FSI));

    // free buffer
    kfree(buff);

    // create buffer for 2 cluster content
    f32->cluster_buffer = kmalloc(f32->ClusterSize * 2);

    // print the info
    // print_file_system_info(f32);

    return (fsHANDLER) f32;
}

PDirectoryCacheEntry _do_lookup_directory_entry(f32FS_HANDLER fs, DirectoryEntryID parentDir, unsigned char *name, unsigned short *unicode){
    if (parentDir==0) parentDir = fs->BPB_RootClus;         // if parent directory = 0, lookup root directory

    // read the directory first cluster and get the directory content cache. This should go in pair
    do_directory_read_cluster(fs, parentDir, 0);

    PDirectoryContentCache _ = dir_cache_get_pointer();
    PDirectoryCacheEntry entry = _->cache_start;



    // cache some value for performance
    int len = strlen(name);

    // traverse through the list
    for (int i=0; i < _->num_of_entries; i++, entry++){
        if (len == 11 && strcmp(name, entry->short_name)==0) return entry;
        if (strcmp(name, entry->long_name_ascii)==0) return entry;
        if (unicode && strcmp((char*)unicode, (char*)entry->long_name)==0) return entry;
    }
    return NULL;
}

HFile fat32_lookup_file(f32FS_HANDLER fs, DirectoryEntryID parentDir, unsigned char *name, unsigned short *unicode) {
    PDirectoryCacheEntry entry = _do_lookup_directory_entry(fs, parentDir, name, unicode);
    if (entry == NULL || entry->is_directory) return NULL;
    return (HFile)entry;
}
DirectoryEntryID fat32_lookup_folder(f32FS_HANDLER fs, DirectoryEntryID parentDir, unsigned char *name, unsigned short *unicode){
    PDirectoryCacheEntry entry = _do_lookup_directory_entry(fs, parentDir, name, unicode);
    if (entry == NULL || !(entry->is_directory)) return FS_NOT_FOUND;
    return entry->cluster_num;
}

/**
 * do the file read
 * @param fs
 * @param file
 * @param buffer
 * @param position
 * @return
 */
unsigned int fat32_read_file(f32FS_HANDLER fs, HFile file, void*buffer, int position, int length){
    int fileFirstSector = get_clusters_first_sector(fs, file->cluster_num);           // first sector
    int lba = fileFirstSector + (position / fs->BPB_BytesPerSec);    // get the sector to read
    int sectorsToRead = ((position+length)/fs->BPB_BytesPerSec) - (lba - fileFirstSector);
    int readOffset = (position % fs->BPB_BytesPerSec);
    if ((position+length) % fs->BPB_BytesPerSec) sectorsToRead++;

    // check for error
    if ((position + length) > file->file_size) {
        kerror("fat32_read_file: Out of bound. File size %d, asked to read %d bytes from %d", file->file_size, length, position);
        return 0;                   // out of bound
    }
//    printf("reading file        %s\n", file->long_name_ascii);
//    printf("file size:          %d\n", file->file_size);
//    printf("file first sector:  %d\n", fileFirstSector);
//    printf("LBA:                %d\n", lba);
//    printf("sectors to read:    %d\n", sectorsToRead);
//    printf("read offset:       %d\n", readOffset);

    // read and copy data
    void * _buff = kmalloc(sectorsToRead * fs->BPB_BytesPerSec);
    sd_readblock(lba, _buff, sectorsToRead);
    memcpy(buffer, _buff + readOffset, length);
    kfree(_buff);
    return length;

}

DirectoryEntryID fat32_lookup_directory_entry(f32FS_HANDLER fs, DirectoryEntryID parentDir, unsigned char *name, unsigned short *unicode){
    PDirectoryCacheEntry entry = _do_lookup_directory_entry(fs, parentDir, name, unicode);
    if (entry == NULL) return FS_NOT_FOUND;
    return entry->cluster_num;
}

void print_file_system_info(f32FS_HANDLER fs) {
    printf("\n\n=================== [FILE SYSTEM INFORMATION] ==================\n");
    printf("OEM:                %s\n", fs->BS_OEMName);
    printf("Free Space:         %d KB\n", fs->fsi->FreeCount * fs->ClusterSize / 1024);
    printf("Next Free Cluster:  %d\n", fs->fsi->NextFree);
    printf("Number of FATs:     %d\n", fs->BPB_NumFATs);
    printf("Sector per Cluster: %d\n", fs->BPB_SecPerClus);
    printf("Root Entry Count:   %d KB\n", *(char *) &fs->bpb->BPB_RootEntCnt + 1);
    printf("Sectors Per FAT:    %ld\n", fs->BPB_FATSz32);
    printf("Usable Storage:     %d\n", (fs->BPB_BytesPerSec / 4) * fs->BPB_BytesPerSec * fs->BPB_SecPerClus);
    printf("================================================================\n\n");



    // read root directory
    do_directory_read_cluster(fs, fs->BPB_RootClus, 0);
   // dir_cache_dump();
}


/**
 * read a specific cluster's fat entry for cluster information. Usually next cluster number.
 * @param cluster_num   cluster number
 * @param fat_index     fat 1 or 2
 * @return
 */
int read_fat_entry(f32FS_HANDLER fs, int cluster_num, int fat_index) {
    if (fat_index) {
        kpanic("read_fat_entry: Please implement fat_index here when read fat entry");
    }
    int FATOffset = cluster_num * 4;
    int thisFATSecNum = fs->BPB_RsvdSecCnt + (FATOffset / fs->BPB_BytesPerSec);
    int thisFATEntOffset = (FATOffset % fs->BPB_BytesPerSec);

    if (current_FAT_sector != thisFATSecNum) {
        read_sectors(fs, FAT_sector, thisFATSecNum, 1);
    }
//    kdebug("read_fat_entry:----- HERE sect num %d, offset %d, (FATOffset / fs->BPB_BytesPerSec) %d", thisFATSecNum, thisFATEntOffset, (FATOffset / fs->BPB_BytesPerSec));
//    uart_dump(FAT_sector);

    int value = (*((int*) &FAT_sector[thisFATEntOffset])) & 0x0FFFFFFF;

//    printf("looking up fat for %d, offset: %d, value 0x%x\n", cluster_num, thisFATEntOffset, value);
//    uart_dump(FAT_sector);
    return value;
}

// write 32-bit value into fat entry
void write_fat_entry(f32FS_HANDLER fs, int cluster_num, int fat_index, int value) {
    /*
     * If(FATType == FAT16)
         FAT16ClusEntryVal = *((WORD *) &SecBuff[ThisFATEntOffset]);
        Else
         FAT32ClusEntryVal = (*((DWORD *) &SecBuff[ThisFATEntOffset])) & 0x0FFFFFFF;
        Fetches the contents of that cluster. To set the contents of this same cluster you do the following:
        If(FATType == FAT16)
         *((WORD *) &SecBuff[ThisFATEntOffset]) = FAT16ClusEntryVal;
        Else {
         FAT32ClusEntryVal = FAT32ClusEntryVal & 0x0FFFFFFF;
         *((DWORD *) &SecBuff[ThisFATEntOffset]) =
         (*((DWORD *) &SecBuff[ThisFATEntOffset])) & 0xF0000000;
         *((DWORD *) &SecBuff[ThisFATEntOffset]) =
         (*((DWORD *) &SecBuff[ThisFATEntOffset])) | FAT32ClusEntryVal;
        }
        Note h
     */
}


HDirectory fat32_read_root_directory(f32FS_HANDLER fs){
    return fat32_read_directory(fs, fs->BPB_RootClus);;
}

HDirectory fat32_read_directory(f32FS_HANDLER fs,int cluster_num){
    if (cluster_num == 0) cluster_num = fs->BPB_RootClus;
    do_directory_read_cluster(fs, cluster_num, 0);
    // now the content is in cache
    return (HDirectory)dir_cache_get_pointer();
}


// read a directory and return its content
// this is equivalent to change of directory
int do_directory_read_cluster(f32FS_HANDLER fs, int dir_start_cluster, int is_chain_load) {

    kdebug("do_directory_read_cluster: Reading cluster %d (%d)", dir_start_cluster, is_chain_load);
//    printf("cluster size address: 0x%x value (%d)\n", &fs->ClusterSize, fs->ClusterSize);
    unsigned char *buffer = fs->cluster_buffer;

    //kpanic("buffer is 0x%x, cluster_buffer 0x%x", buffer, fs->cluster_buffer);
    DirectoryCacheEntry cacheEntry;
    PFAT32DirectoryEntry     entry;
    PFAT32DirectoryEntry     LFNbaseentry;

    // check for size of directory entry. Note: Must be 32
//    void* a = entry;
//    void* b = ++entry;
//    printf("\n\nENTRY 1 0x%x, NEXT 0x%x, Delta %d, size %d\n\n", a, b, b-a, sizeof(FAT32DirectoryEntry));
//    asm volatile("brk #0");

    // first empty out cache

    if (!is_chain_load) dir_cache_reset();

    // read first cluster
    read_cluster(fs, buffer, dir_start_cluster);

    entry = (PFAT32DirectoryEntry) buffer;    // point first entry to first byte of buffer


    // iterate through all entries in this cluster
    for (int i = 0; i < fs->EntriesPerCluster; i++, entry++) {
        unsigned int LFNCount = 0;

        // skip if entry is deleted or last one
        if (entry->DIR_Name[0] == 0xE5) continue; // this entry is free. continue the loop
        if (entry->DIR_Name[0] == 0x00) continue; // should be break; // this should stop the process. becaus
        if (entry->DIR_Name[0] == 0x05) entry->DIR_Name[0] = 0xE5;  // first char of the name is a .
        if (entry->DIR_Name[0] == 0x20) continue;   // error, DIR_Name[0] can not be zero as Microsoft specs

        // zero out the cache
        bzero(&cacheEntry, sizeof(DirectoryCacheEntry));

        // check for long file name
        while (entry->DIR_Attr == ATTR_LFN) {
            //printf("Found a LFN sub entry\n");
            // dump previous
            //uart_dump_size(entry, 32);
            i++;
            LFNbaseentry = entry++;
            LFNCount++;
        }


        // short name
        memcpy(cacheEntry.short_name, entry->DIR_Name, 11);
        cacheEntry.short_name[11] = 0;

        // parse LFN if there is any
        if (LFNCount == 0) {
            memcpy((char*)cacheEntry.long_name_ascii, (char*)cacheEntry.short_name, strlen(cacheEntry.short_name));
            format_short_name(entry->DIR_Name, cacheEntry.long_name_ascii);

        }else{
            read_long_name((unsigned char *)LFNbaseentry, LFNCount, cacheEntry.long_name_ascii, cacheEntry.long_name);
        }


        if ((entry->DIR_Attr & ATTR_DIRECTORY) == ATTR_DIRECTORY) {
            cacheEntry.is_directory = 1;
        }else{
            cacheEntry.is_directory = 0;
        }

        // last thing, add to cache, so we index everything
        cacheEntry.cluster_num = entry->DIR_FstClusHI << 16 | entry->DIR_FstClusLO;
        cacheEntry.file_size = entry->DIR_FileSize;
        dir_cache_add(&cacheEntry);
    }
    kfree(buffer);

    // end of cluster, what's now?
    // lookup FAT and see if cluster continued
    int nextCluster = read_fat_entry(fs, dir_start_cluster, 0);
    if (nextCluster < 0xffffff7) {
        kdebug("do_directory_read_cluster: Next cluster in chain is Cluster [%d]", nextCluster);
        do_directory_read_cluster(fs, nextCluster, 1);
    }else {
        kdebug("do_directory_read_cluster: End of cluster chain");
    }
}

/**
 * read long file name from array of entries and set it to buffer
 * @param lfn_entries
 * @param count
 * @param buffer
 */
static void read_long_name(unsigned char *lfn_entries, unsigned char count, unsigned char *buffer_ascii, unsigned short* buffer_unicode){
    // iterate through the array of entries
    PLFNDirectoryEntry lfnEntry = (PLFNDirectoryEntry)lfn_entries;  // starting this address

    unsigned char name_segment_ascii[13]; //each entry contains 13 character, last character always 0
    unsigned short name_segment_unicode[13]; //each entry contains 13 character, last character always 0

    for (int i = 0; i < count; i++, lfnEntry--){
        unsigned char sequence_no = lfnEntry->LFN_SequenceNumber & 0x0F;
//        uart_dump_size(lfnEntry, 32);
        bzero(name_segment_ascii, sizeof(name_segment_ascii));      // clear the buffer
        bzero(name_segment_unicode, sizeof(name_segment_unicode));  // clear the buffer

        // read the name segment of this entry
        unsigned char *ascii_ptr;       // increase 1
        memcpy(&name_segment_unicode[0], lfnEntry->FLN_NameFirst5, sizeof(lfnEntry->FLN_NameFirst5));
        memcpy(&name_segment_unicode[5], lfnEntry->LFN_Name6to11, sizeof(lfnEntry->LFN_Name6to11));
        memcpy(&name_segment_unicode[11], lfnEntry->LFN_NameLast2, sizeof(lfnEntry->LFN_NameLast2));

        ascii_ptr = (char *)lfnEntry->FLN_NameFirst5;
        for (int j=0; j<5; j++,  ascii_ptr += 2)name_segment_ascii[j] = *ascii_ptr;
        ascii_ptr = (char *)lfnEntry->LFN_Name6to11;
        for (int j=5; j<11; j++,  ascii_ptr += 2) name_segment_ascii[j] = *ascii_ptr;
        ascii_ptr = (char *)lfnEntry->LFN_NameLast2;
        for (int j=11; j<13; j++,  ascii_ptr += 2) name_segment_ascii[j] = *ascii_ptr;

        // copy data to its right place
        unsigned short *dest_unicode = buffer_unicode + ((sequence_no-1)*13);
        unsigned char *dest_ascii = buffer_ascii + ((sequence_no-1)*13);
        memcpy(dest_ascii, name_segment_ascii, sizeof (name_segment_ascii));
        memcpy(dest_unicode, name_segment_unicode, sizeof (name_segment_unicode));
    }
}

// list a directory
void list_directory(f32FS_HANDLER fs, int dir_cluster_num) {

}

// find a file or sub directory in a directory and return its cluster number
int do_directory_search(f32FS_HANDLER fs, int dir_cluster_num) {

}

// read read_length of bytes from a  file at read_start into buffer
int do_file_read(f32FS_HANDLER fs, int cluster_num, unsigned char *buff, int read_start, int read_length) {

}

int get_file_stat(f32FS_HANDLER fs, int cluster_num) {

}

/**
 * return first sector number of a cluster N in a file system f32
 * @param f32
 * @param cluster_number
 * @return
 */
int get_clusters_first_sector(f32FS_HANDLER f32, int cluster_number) {
    return ((cluster_number - 2) * f32->BPB_SecPerClus) + f32->FirstDataSector;
}

/**
 * read the whole cluster to buffer
 * @param fs
 * @param buff
 * @param cluster_number
 */
void read_cluster(f32FS_HANDLER fs, unsigned char *buff, unsigned int cluster_number) { // static
    if (cluster_number >= EOC) {
        kpanic("Can't get cluster. Hit End Of Chain.");
        return;
    }

    int sect = get_clusters_first_sector(fs, cluster_number);
    read_sectors(fs, buff, sect, fs->BPB_SecPerClus * 2);       // we always want to read 2 clusters at the same time for LFN span across
}

/**
 * read [count] of sector
 * @param fs
 * @param buff
 * @param sector
 * @param count
 */
void read_sectors(f32FS_HANDLER fs, void *buff, unsigned int sector, unsigned int count) {
    sd_readblock(sector, buff, count);
}
