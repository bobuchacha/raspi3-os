#ifndef _FAT32_STRUCT_H
#define _FAT32_STRUCT_H

#include <ros.h>

struct FAT32BootSector {
	UByte jmp[3];
	UByte oemname[8]; 
	UWord bytes_per_sector;
	UByte sector_per_cluster;
	UWord reserved_sector;
	UByte fat_number;
	UWord root_entry;
	UWord total_sector_16;
	UByte media;
	UWord dat_size_16;
	UWord sector_per_track;
	UWord head_number;
	UInt hidden_sector;
	UInt total_sector;
	// FAT32 specific
	UInt fat_size;
	UWord ext_falgs;
	UWord fs_ver;
	UInt root_cluster;
	UWord fsinfo;
	UWord backup_bootsect;
	UByte reserved[12];
	UByte drvnum;
	UByte reserved1;
	UByte bootsig;
	UInt volume_id;
	char volume_label[11];
	char fstype[8];
} __attribute__((packed));

struct FAT32DirEntry {
	UByte name[11];
	UByte attr;
	UByte nt_reserved;
	UByte create_time_millis;
	union {
		struct {
			UWord second : 5;
			UWord minute : 6;
			UWord hour : 5;
		} create_time;
		UWord create_time_u16;
	};

	union {
		struct {
			UWord day : 5;
			UWord month : 4;
			UWord year : 7;
		} create_date;
		UWord create_date_u16;
	};

	union{
		struct {
			UWord day : 5;
			UWord month : 4;
			UWord year : 7;
		} access_date;
		UWord access_date_u16;
	};

	UWord cluster_hi;
	
	union{
		struct {
			UWord second : 5;
			UWord minute : 6;
			UWord hour : 5;
		} write_time;
		UWord write_time_u16;
	};

	union{
		struct {
			UWord day : 5;
			UWord month : 4;
			UWord year : 7;
		} write_date;
		UWord write_date_u16;
	};
	
	UWord cluster_lo;
	UInt size;
} __attribute__((packed));

// MICROSOFT STANDARD - DO NOT EDIT
struct FAT32LFNEntry {
    unsigned char LFN_SequenceNumber;
//     unsigned short FLN_NameFirst5[5];        // first 0-4 ketter of name unicode
    unsigned char FLN_NameFirst5[10];        // first 0-4 ketter of name unicode
    unsigned char FLN_Attr;                    // attrib FLN - always 0x0F
    unsigned char LFN_Type;                    // Long entry type, zero for name entries
    unsigned char FLN_Checksum;                // checksum generated of the short file name when file created. Used for system not support LFN
    unsigned char LFN_Name6to11[12];        // name from 5-10
//     unsigned short LFN_Name6to11[6];        // name from 5-10
    unsigned short LFN_Unused;                // always zero
    unsigned char LFN_NameLast2[4];            // last 2 character of this LFN entry
//     unsigned short LFN_NameLast2[2];            // last 2 character of this LFN entry
};


enum FAT32DirAttr {
	ATTR_READ_ONLY = 0x01,
	ATTR_HIDDEN = 0x02,
	ATTR_SYSTEM = 0x04,
	ATTR_VOLUME_ID = 0x08,
	ATTR_DIRECTORY = 0x10,
	ATTR_ARCHIVE = 0x20,
	ATTR_LONG_NAME = 0x0f,
};

#endif