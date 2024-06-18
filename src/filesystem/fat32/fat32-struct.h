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
	struct {
		UWord second : 5;
		UWord minute : 6;
		UWord hour : 5;
	} create_time;
	struct {
		UWord day : 5;
		UWord month : 4;
		UWord year : 7;
	} create_date;
	struct {
		UWord day : 5;
		UWord month : 4;
		UWord year : 7;
	} access_date;
	UWord cluster_hi;
	struct {
		UWord second : 5;
		UWord minute : 6;
		UWord hour : 5;
	} write_time;
	struct {
		UWord day : 5;
		UWord month : 4;
		UWord year : 7;
	} write_date;
	UWord cluster_lo;
	UInt size;
} __attribute__((packed));

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