#include <ros.h>
#include "fat32.h"
#include "../filesystem.h"
#include "../../hal/hal.h"
#include "fat32-struct.h"
#include "memory.h"
#include "lib/string.h"
#include "log.h"
#include "utils.h"

static char* fat32_get_full_name(const struct FAT32DirEntry* dir, char* name) {
	// copy name, change to lower-case
	memmove(name, dir->name, 11);
	for (int i = 0; i < 11; i++) {
		if ((name[i] >= 'A') && (name[i] <= 'Z')) {
			name[i] += 'a' - 'A';
		}
	}
	// move extension to the right, reserve space for dot
	name[11] = name[10];
	name[10] = name[9];
	name[9] = name[8];
	// remove space at end of name
	unsigned int off = 7;
	while (name[off] == ' ') {
		off--;
	}
	// check if extension is empty, if so, do not put dot
	if (name[9] == ' ') {
		name[off + 1] = '\0';
		return name;
	}
	name[off + 1] = '.';
	for (int i = 2; i < 5; i++) {
		// copy extension,remove space at the end
		if (name[i + 7] == ' ') {
			name[off + i] = '\0';
			return name;
		}
		name[off + i] = name[i + 7];
	}
	name[off + 5] = '\0';
	return name;
}

static int fat32_dir_search(struct FAT32Private* priv, unsigned int cluster, const char* name,
							struct FAT32DirEntry* dir_dest) {
	
	struct FAT32LFNEntry lfn_entries[19];
	int lfn_entry_count = 0;
	memzero((Address)lfn_entries, sizeof(lfn_entries));

	for (int i = 0;; i += 32) {
		unsigned int clus = fat32_offset_cluster(priv, cluster, i);
		if (clus == 0) {
			break;
		}
		struct FAT32DirEntry dir;
		int errc = fat32_read_cluster(priv, &dir, clus,
									  i % (priv->boot_sector->sector_per_cluster * SECTORSIZE),
									  sizeof(dir));
		if (errc < 0) {
			return errc;
		}
		if (dir.name[0] == 0xe5) {
			continue;
		} else if (dir.name[0] == 0) {
			break;
		} else if (dir.attr == ATTR_LONG_NAME) {
			memcpy((Address)(&lfn_entries[lfn_entry_count++]), (Address)&dir, 32);
			continue;
		} else if (dir.attr == ATTR_VOLUME_ID) {
			continue;
		}
		
		unsigned short name_u16[255];
		UByte *name_u8 = null;
		
		if (lfn_entry_count) {
			fat32_get_long_name(lfn_entries, lfn_entry_count, name_u16);
			str_from_utf16(name_u16, 255, &name_u8);
		}else{
			name_u8 = (UByte*)kmalloc(12);
			fat32_get_full_name(&dir, name_u8);
		}
		
		if (strcmp(name, name_u8) == 0) {
			memmove(dir_dest, &dir, sizeof(struct FAT32DirEntry));
			kfree((Address)name_u8);
			return i / 32;
		}
		kfree((Address)name_u8);
	}
	return ERROR_NOT_EXIST;
}

static int fat32_path_search(struct FAT32Private* priv, struct VfsPath path,
							 struct FAT32DirEntry* dir_dest) {

	
	unsigned int cluster = priv->boot_sector->root_cluster;
	for (int i = 0; i < path.parts; i++) {
		int errc;
		if ((errc = fat32_dir_search(priv, cluster, path.pathbuf + i * VFS_NAME_LENGTH_MAX, dir_dest)) < 0) {
			return errc;
		}
		cluster = (dir_dest->cluster_hi << 16) | dir_dest->cluster_lo;
	}
	return 0;
}

int fat32_open(void* private, struct VfsPath path) {
	
	struct FAT32Private* priv = private;
	if (path.parts == 0) {
		return priv->boot_sector->root_cluster;
	}
	struct FAT32DirEntry dir;
	int errc;
	dir.cluster_hi = dir.cluster_lo = 0;
	if ((errc = fat32_path_search(priv, path, &dir)) < 0) {
		return errc;
	}
	return (dir.cluster_hi << 16) | dir.cluster_lo;
}

int fat32_dir_first_file(void* private, unsigned int cluster) {
	struct FAT32Private* priv = private;
	for (int i = 0;; i += 32) {
		unsigned int clus = fat32_offset_cluster(priv, cluster, i);
		if (clus == 0) {
			break;
		}
		struct FAT32DirEntry dir;
		int errc = fat32_read_cluster(priv, &dir, clus,
									  i % (priv->boot_sector->sector_per_cluster * SECTORSIZE),
									  sizeof(dir));
		if (errc < 0) {
			return errc;
		}
		if (dir.name[0] == 0xe5) {
			continue;
		} else if (dir.name[0] == 0) {
			break;
		} else if (dir.attr == ATTR_LONG_NAME) {
			continue;
		} else if (dir.attr == ATTR_VOLUME_ID) {
			continue;
		}
		return i / 32;
	}
	return -1;
}

int fat32_dir_read(void* private, char* buf, unsigned int cluster, unsigned int entry) {
	struct FAT32Private* priv = private;
	for (int i = entry * 32;; i += 32) {
		unsigned int clus = fat32_offset_cluster(priv, cluster, i);
		if (clus == 0) {
			break;
		}
		struct FAT32DirEntry dir;
		int errc = fat32_read_cluster(priv, &dir, clus,
									  i % (priv->boot_sector->sector_per_cluster * SECTORSIZE),
									  sizeof(dir));
		if (errc < 0) {
			return errc;
		}
		if (dir.name[0] == 0xe5) {
			continue;
		} else if (dir.name[0] == 0) {
			return 0;
		} else if (dir.attr == ATTR_LONG_NAME) {
			continue;
		} else if (dir.attr == ATTR_VOLUME_ID) {
			continue;
		}
		fat32_get_full_name(&dir, buf);
		return i / 32 + 1;
	}
	return 0;
}

/**
 * get Unicode long file name of an entry (entries)
*/
UWord *fat32_get_long_name(struct FAT32LFNEntry lfn_entries[], int count, UWord *target){

	for (int i = 0; i < count; i++){
//		_trace("Process long name entry %d", i);
		register ulong sequence_no = lfn_entries[i].LFN_SequenceNumber & 31;
		register ulong base = (sequence_no-1)*13;
		unsigned char *p = (unsigned char*)target + (base*2);
		int char_idx;
		for (char_idx = 0; char_idx < 10; char_idx++){
			*p++ = lfn_entries[i].FLN_NameFirst5[char_idx];
		}
		for (char_idx = 0; char_idx < 12; char_idx++){
			*p++ = lfn_entries[i].LFN_Name6to11[char_idx];
		}
		for (char_idx = 0; char_idx < 4; char_idx++){
			*p++ = lfn_entries[i].LFN_NameLast2[char_idx];
		}
//		kprint("	- type: %d\n", lfn_entries[i].LFN_Type);
//		kprint("	- sequence: %d\n", lfn_entries[i].LFN_SequenceNumber & 0x0F);
//		kprint("	- checksum: %d\n", lfn_entries[i].FLN_Checksum);
//		kprint("	- Attr: %x\n\n", lfn_entries[i].FLN_Attr);

//		kdump_size(&lfn_entries[i], 32);
	}  

	return target;
}

unsigned char _lfn_checksum(const unsigned char *pFCBName)
{
    int i;
    unsigned char sum = 0;

    for (i = 11; i; i--)
        sum = ((sum & 1) << 7) + (sum >> 1) + *pFCBName++;

    return sum;
}

int fat32_dir_read_ex(void* private, struct DirectoryEntry* vfs_dir_entry, unsigned int cluster, unsigned int entry) {
	struct FAT32Private* priv = private;
	struct FAT32LFNEntry lfn_entries[19];
	int lfn_entry_count = 0;

	memzero((Address)lfn_entries, sizeof(lfn_entries));

	for (int i = entry * 32;; i += 32) {
		unsigned int clus = fat32_offset_cluster(priv, cluster, i);
		if (clus == 0) {
			break;
		}
		struct FAT32DirEntry dir;
		int errc = fat32_read_cluster(priv, &dir, clus,
									  i % (priv->boot_sector->sector_per_cluster * SECTORSIZE),
									  sizeof(dir));
		if (errc < 0) {
			return errc;
		}
		if (dir.name[0] == 0xe5) {
			continue;
		} else if (dir.name[0] == 0) {
			return 0;
		} else if (dir.attr == ATTR_LONG_NAME) {
			memcpy((Address)(&lfn_entries[lfn_entry_count++]), (Address)&dir, 32);
			continue;
		} else if (dir.attr == ATTR_VOLUME_ID) {
			continue;
		}

		if (lfn_entry_count) {
			fat32_get_long_name(lfn_entries, lfn_entry_count, vfs_dir_entry->long_name);
		}else{
			unsigned char buff[12];
			buff[12] = '\0';
			fat32_get_full_name(&dir, buff);
			bytes_to_utf16(buff, 12, vfs_dir_entry->long_name);
		}

		vfs_dir_entry->attr = dir.attr;
		vfs_dir_entry->write_date_u16 = dir.write_date_u16;
		vfs_dir_entry->write_time_u16 = dir.write_time_u16;
		vfs_dir_entry->access_date_u16 = dir.access_date_u16;
		vfs_dir_entry->create_date_u16 = dir.create_date_u16;
		vfs_dir_entry->create_time_u16 = dir.create_time_u16;
		vfs_dir_entry->size = dir.size;
		
		return i / 32 + 1; // return current entry index
	}
	return 0;
}

int fat32_file_size(void* private, struct VfsPath path) {
	struct FAT32Private* priv = private;
	struct FAT32DirEntry dir;
	int errc = fat32_path_search(priv, path, &dir);
	if (errc < 0) {
		return errc;
	}
	return dir.size;
}

int fat32_file_mode(void* private, struct VfsPath path) {
	struct FAT32Private* priv = private;
	struct FAT32DirEntry dir;
	int errc = fat32_path_search(priv, path, &dir);
	if (errc < 0) {
		return errc;
	}
	int mode = priv->mode;
	if (dir.attr & ATTR_DIRECTORY) {
		mode |= 0040000;
	}
	return mode;
}

static char* fat32_file_get_short_name(char* shortname, const char* longname) {
	strncpy(shortname, "           ", 12);
	const char* s = longname;
	int p = 0;
	while (*s) {
		if (*s == '.') {
			s++;
			p = 8;
		}
		if (*s >= 'a' && *s <= 'z') {
			shortname[p] = *s - ('a' - 'A');
		} else {
			shortname[p] = *s;
		}
		p++;
		s++;
		if (p == 11) {
			break;
		}
	}
	return shortname;
}

static int fat32_dir_insert_entry(struct FAT32Private* priv, unsigned int cluster,
								  struct FAT32DirEntry* dirent) {
	for (int i = 0;; i += 32) {
		struct FAT32DirEntry dir;
		unsigned int clus = fat32_offset_cluster(priv, cluster, i);
		if (clus == 0) {
			break;
		}
		if (fat32_read_cluster(priv, &dir, clus,
							   i % (priv->boot_sector->sector_per_cluster * SECTORSIZE),
							   sizeof(dir))
			< 0) {
			return ERROR_READ_FAIL;
		}
		if (dir.name[0] == 0xe5 || dir.name[0] == 0x00) {
			if (fat32_write_cluster(priv, dirent, clus,
									i % (priv->boot_sector->sector_per_cluster * SECTORSIZE),
									sizeof(dir))
				< 0) {
				return ERROR_WRITE_FAIL;
			}
			return 0;
		}
	}
	// allocate a new cluster and attrach
	unsigned int alloc = fat32_allocate_cluster(priv);
	if (alloc == 0) {
		return ERROR_OUT_OF_SPACE;
	}
	fat32_write_cluster(priv, dirent, alloc, 0, sizeof(struct FAT32DirEntry));
	fat32_append_cluster(priv, cluster, alloc);
	return 0;
}

int _write_lfn(void* priv, unsigned int cluster, unsigned char *filename, struct FAT32DirEntry* dir){
    // long name conversion
    UWord longname[255];
    struct FAT32LFNEntry *lfnEntries;           // array hold the lfn entries
    struct FAT32LFNEntry *lfnEntry;             // pointer to an entry
    int name_length = strlen(filename);
    int lfn_entry_count = (name_length / 13)+ (name_length % 13 != 0);
    lfnEntries = (Pointer)kmalloc(lfn_entry_count * sizeof(struct FAT32LFNEntry));            // allocate enough memory for the entries
    utf8_to_utf16(filename, name_length, longname, 255);                    // convert utf8 to ucs-2 format
    unsigned char lfn_checksum = _lfn_checksum(dir->name);
    int _s = 0, ret;

    for (int i = 0; i < lfn_entry_count; i++){
        lfnEntry = lfnEntries + i;
        _trace("Preparing LFN entry %d\n", i);
        for (int j = 0, _d = 0; j < 10; j++, _d++, _s++) lfnEntry->FLN_NameFirst5[_d] = *(((UByte*)longname) + _s);
        for (int j = 0, _d = 0; j < 12; j++, _d++, _s++) lfnEntry->LFN_Name6to11[_d] = *(((UByte*)longname) + _s);
        for (int j = 0, _d = 0; j < 4; j++, _d++, _s++) lfnEntry->LFN_NameLast2[_d] = *(((UByte*)longname) + _s);
        lfnEntry->FLN_Attr = ATTR_LONG_NAME;
        lfnEntry->LFN_SequenceNumber = (i+1) & 0x1F;                        // sequence number
        if (i == lfn_entry_count - 1) lfnEntry->LFN_SequenceNumber |= 0x40; // mark last entry en the sequence
        lfnEntry->FLN_Checksum = lfn_checksum;
    }

    // write LFN entries in backward order
    for (int i = lfn_entry_count; i > 0; i--) {
        lfnEntry = lfnEntries + (i -1);
        if ((ret = fat32_dir_insert_entry(priv, cluster, (struct FAT32DirEntry*)lfnEntry)) < 0) return ret;
    }
}

int fat32_mkdir(void* private, struct VfsPath path) {
	struct FAT32Private* priv = private;
	// get target directory
	unsigned int cluster;
	char* filename;
	if (path.parts > 1) {
		struct VfsPath prevpath;
		prevpath.parts = path.parts - 1;
		prevpath.pathbuf = path.pathbuf;
		struct FAT32DirEntry dir;
		fat32_path_search(priv, prevpath, &dir);
		cluster = (dir.cluster_hi << 16) | dir.cluster_lo;
		filename = path.pathbuf + prevpath.parts * 128;
	} else {
		cluster = priv->boot_sector->root_cluster;
		filename = path.pathbuf;
	}
	// check exist
	struct FAT32DirEntry search_dir;
	if (fat32_dir_search(priv, cluster, filename, &search_dir) >= 0) {
		return ERROR_EXIST;
	}
	// create inode
	char shortname[12];
	fat32_file_get_short_name(shortname, filename);
	struct FAT32DirEntry dir;
	memset(&dir, 0, sizeof(dir));
	memmove(dir.name, shortname, 11);
	unsigned int alloc = fat32_allocate_cluster(priv);
	if (alloc == 0) {
		return ERROR_OUT_OF_SPACE;
	}
	dir.cluster_lo = alloc & 0xffff;
	dir.cluster_hi = (alloc >> 16) & 0xffff;
	dir.attr = ATTR_DIRECTORY;
	int ret;

    // write long file name entries
    if ((ret = _write_lfn(priv, cluster, filename, &dir)) < 0) {
        return ret;
    }

	// repeat this steps to create LFN entry
	if ((ret = fat32_dir_insert_entry(priv, cluster, &dir)) < 0) {
		return ret;
	}

	// dot entry
	memset(&dir, 0, sizeof(dir));
	memmove(dir.name, ".          ", 11);
	dir.cluster_lo = alloc & 0xffff;
	dir.cluster_hi = (alloc >> 16) & 0xffff;
	dir.attr = ATTR_DIRECTORY;
	if ((ret = fat32_dir_insert_entry(priv, alloc, &dir)) < 0) {
		return ret;
	}
	// dotdot entry
	memset(&dir, 0, sizeof(dir));
	memmove(dir.name, "..         ", 11);
	if (cluster != priv->boot_sector->root_cluster) {
		// not root cluster
		dir.cluster_lo = cluster & 0xffff;
		dir.cluster_hi = (cluster >> 16) & 0xffff;
	}
	dir.attr = ATTR_DIRECTORY;
	return fat32_dir_insert_entry(priv, alloc, &dir);
}

int fat32_file_remove(void* private, struct VfsPath path) {
	struct FAT32Private* priv = private;
	// get target directory
	unsigned int cluster;
	char* filename;
	if (path.parts > 1) {
		struct VfsPath prevpath;
		prevpath.parts = path.parts - 1;
		prevpath.pathbuf = path.pathbuf;
		struct FAT32DirEntry dir;
		fat32_path_search(priv, prevpath, &dir);
		cluster = (dir.cluster_hi << 16) | dir.cluster_lo;
		filename = path.pathbuf + prevpath.parts * 128;
	} else {
		cluster = priv->boot_sector->root_cluster;
		filename = path.pathbuf;
	}

    _trace("Removing file: %s", filename);

	// check exist
	struct FAT32DirEntry search_dir;
	int ent_idx;
	if ((ent_idx = fat32_dir_search(priv, cluster, filename, &search_dir)) < 0) {
		return ent_idx;
	}
	if (search_dir.attr & ATTR_DIRECTORY) {
		return ERROR_NOT_FILE;
	}
	if (fat32_free_chain(priv, (search_dir.cluster_hi << 16) | search_dir.cluster_lo) < 0) {
		return ERROR_WRITE_FAIL;
	}
	search_dir.name[0] = 0xe5;
	unsigned int clus = fat32_offset_cluster(priv, cluster, ent_idx * 32);
	if (fat32_write_cluster(priv, &search_dir, clus,
							ent_idx * 32 % (priv->boot_sector->sector_per_cluster * SECTORSIZE),
							sizeof(search_dir))
		< 0) {
		return ERROR_WRITE_FAIL;
	}
	return 0;
}

static int fat32_write_inode(struct FAT32Private* priv, struct VfsPath path,
							 const struct FAT32DirEntry* dir_dest) {
	unsigned int cluster = priv->boot_sector->root_cluster;
	int ino;
	struct FAT32DirEntry dir;
	for (int i = 0; i < path.parts - 1; i++) {
		if ((ino = fat32_dir_search(priv, cluster, path.pathbuf + i * 128, &dir)) < 0) {
			return ERROR_NOT_EXIST;
		}
		cluster = (dir.cluster_hi << 16) | dir.cluster_lo;
	}
	if ((ino = fat32_dir_search(priv, cluster, path.pathbuf + (path.parts - 1) * 128, &dir)) < 0) {
		return ERROR_NOT_EXIST;
	}
	return fat32_write_cluster(priv, dir_dest, fat32_offset_cluster(priv, cluster, ino * 32),
							   ino * 32 % (priv->boot_sector->sector_per_cluster * SECTORSIZE),
							   sizeof(struct FAT32DirEntry));
}

int fat32_update_size(void* private, struct VfsPath path, unsigned int size) {
	struct FAT32Private* priv = private;
	struct FAT32DirEntry dir;
	if (fat32_path_search(priv, path, &dir) < 0) {
		return ERROR_NOT_EXIST;
	}

	dir.size = size;
	return fat32_write_inode(priv, path, &dir);
}

int fat32_file_create(void* private, struct VfsPath path) {
	struct FAT32Private* priv = private;
	unsigned int cluster;
	char* filename;
	if (path.parts > 1) {
		struct VfsPath prevpath;
		prevpath.parts = path.parts - 1;
		prevpath.pathbuf = path.pathbuf;
		struct FAT32DirEntry dir;
		fat32_path_search(priv, prevpath, &dir);
		cluster = (dir.cluster_hi << 16) | dir.cluster_lo;
		filename = path.pathbuf + prevpath.parts * 128;
	} else {
		cluster = priv->boot_sector->root_cluster;
		filename = path.pathbuf;
	}

	char shortname[12];
	fat32_file_get_short_name(shortname, filename);
	struct FAT32DirEntry dir;
	memset(&dir, 0, sizeof(dir));
	memmove(dir.name, shortname, 11);
	unsigned int alloc = fat32_allocate_cluster(priv);
	if (alloc == 0) {
		return ERROR_OUT_OF_SPACE;
	}
	dir.cluster_lo = alloc & 0xffff;
	dir.cluster_hi = (alloc >> 16) & 0xffff;

    // write long file name entries
    int ret;
    if ((ret = _write_lfn(priv, cluster, filename, &dir)) < 0) {
        return ret;
    }

	return fat32_dir_insert_entry(priv, cluster, &dir);
}