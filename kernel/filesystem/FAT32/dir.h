//
// Created by bobuc on 5/8/2024.
//

#ifndef DIR_H
#define DIR_H

// MICROSOFT STANDARD - DO NOT EDIT
typedef struct dir_entry_s {
    unsigned char DIR_Name[11];                // entry name 8+3 format
    unsigned char DIR_Attr;                    // attrib
    unsigned char DIR_NTRes;                    // NT reserved
    unsigned char DIR_CrtTimeTenth;            // Created Time Tenth
    unsigned short DIR_CrtTime;                // created time
    unsigned short DIR_CrtDate;                // created date
    unsigned short DIR_LstAccDate;            // date alst accessed
    unsigned short DIR_FstClusHI;            // First cluster HI
    unsigned short DIR_WrtTime;            // Last written time
    unsigned short DIR_WrtDate;            // last written date
    unsigned short DIR_FstClusLO;            // first cluster LO
    unsigned int DIR_FileSize;            // file size (in bytes)
} FAT32DirectoryEntry, *PFAT32DirectoryEntry;

// MICROSOFT STANDARD - DO NOT EDIT
typedef struct lfn_dir_entry_s {
    unsigned char LFN_SequenceNumber;
    unsigned char FLN_NameFirst5[10];        // first 0-4 ketter of name unicode
    unsigned char FLN_Attr;                    // attrib FLN - always 0x0F
    unsigned char LFN_Type;                    // Long entry type, zero for name entries
    unsigned char FLN_Checksum;                // checksum generated of the short file name when file created. Used for system not support LFN
    unsigned char LFN_Name6to11[12];        // name from 5-10
    unsigned short LFN_Unused;                // always zero
    unsigned char LFN_NameLast2[4];            // last 2 character of this LFN entry
} LFNDirectoryEntry, *PLFNDirectoryEntry;

#endif //DIR_H
