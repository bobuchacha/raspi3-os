#include "ros.h"
#include "fat32.h"
#include "../../kernel/memory/paging.h"
#include "../../kernel/hardware/uart/uart.h"
#include "../../kernel/hardware/sdcard/sd.h"
#include "../../kernel/lib/string.h"

f32 *master_fs;

static void read_bpb(f32 *fs, struct bios_parameter_block *bpb);

static unsigned int sector_for_cluster(f32 *fs, unsigned int cluster);

static void trim_spaces(char *c, int max) {
    int i = 0;
    while (*c != ' ' && i++ < max) {
        c++;
    }
    if (*c == ' ') *c = 0;
}

static void getSector(f32 *fs, unsigned char *buff, unsigned int sector, unsigned int count) {
    sd_readblock(sector, buff, count);
}

static void putSector(f32 *fs, unsigned char *buff, unsigned int sector, unsigned int count) {
    unsigned int i;
    kpanic("implement here put sector");
    sd_writeblock(buff, sector, count);
}

static void flushFAT(f32 *fs) {
    // TODO: This is not endian-safe. Must marshal the integers into a byte buffer.
    putSector(fs, (unsigned char *) fs->FAT, fs->fat_begin_sector, fs->bpb.count_sectors_per_FAT32);
}

static unsigned short readi16(unsigned char *buff, unsigned int offset) {
    unsigned char *ubuff = buff + offset;
    return ubuff[1] << 8 | ubuff[0];
}

static unsigned int readi32(unsigned char *buff, unsigned int offset) {
    unsigned char *ubuff = buff + offset;
    return
            ((ubuff[3] << 24) & 0xFF000000) |
            ((ubuff[2] << 16) & 0x00FF0000) |
            ((ubuff[1] << 8) & 0x0000FF00) |
            (ubuff[0] & 0x000000FF);
}

/**
 * 11 2 The number of Bytes per sector (remember, all numbers are in the little-endian format).
 * 13 1 Number of sectors per cluster.
 * 14 2 Number of reserved sectors. The boot record sectors are included in this value.
 * 16 1 Number of File Allocation Tables (FAT's) on the storage media. Often this value is 2.
 * 17 2 Number of directory entries (must be set so that the root directory occupies entire sectors).
 * 19 2 The total sectors in the logical volume. If this value is 0, it means there are more than 65535 sectors in the volume, and the actual count is stored in "Large Sectors (bytes 32-35).
 * 21 1 This Byte indicates the media descriptor type.
 * 22 2 Number of sectors per FAT. FAT12/FAT16 only.
 * 24 2 Number of sectors per track.
 * 26 2 Number of heads or sides on the storage media.
 * 28 4 Number of hidden sectors. (i.e. the LBA of the beginning of the partition.)
 * 32 4 Large amount of sector on media. This field is set if there are more than 65535 sectors in the volume.
 */

/**
 * 36 4 Sectors per FAT. The size of the FAT in sectors.
 * 40 2 Flags.
 * 42 2 FAT version number. The high byte is the major version and the low byte is the minor version. FAT drivers should respect this field.
 * 44 4 The cluster number of the root directory. Often this field is set to 2.
 * 48 2 The sector number of the FSInfo structure.
 * 50 2 The sector number of the backup boot sector.
 * 52 12 Reserved. When the volume is formated these bytes should be zero.
 * 64 1 Drive number. The values here are identical to the values returned by the BIOS interrupt 0x13. 0x00 for a floppy disk and 0x80 for hard disks.
 * 65 1 Flags in Windows NT. Reserved otherwise.
 * 66 1 Signature (must be 0x28 or 0x29).
 * 67 4 VolumeID 'Serial' number. Used for tracking volumes between computers. You can ignore this if you want.
 * 71 11 Volume label string. This field is padded with spaces.
 * 82 8 System identifier string. Always "FAT32   ". The spec says never to trust the contents of this string for any use.
 * 90 420 Boot code.
 */

static void read_bpb(f32 *fs, struct bios_parameter_block *bpb) {
    unsigned char sector0[512];
    getSector(fs, sector0, 0, 1);
    bpb->bytes_per_sector              = sector0[12] << 8 + sector0[11];    //readi16(sector0, 11);
    bpb->sectors_per_cluster           = sector0[13];
    bpb->reserved_sectors              = readi16(sector0, 14);
    bpb->FAT_count                     = sector0[16];
    bpb->dir_entries                   = sector0[18] << 8 + sector0[17];    //readi16(sector0, 17);
    bpb->total_sectors                 = sector0[20] << 8 + sector0[19];
    bpb->media_descriptor_type         = sector0[21];
    bpb->count_sectors_per_FAT12_16    = readi16(sector0, 22);
    bpb->count_sectors_per_track       = readi16(sector0, 24);
    bpb->count_heads_or_sizes_on_media = readi16(sector0, 26);
    bpb->count_hidden_sectors          = readi32(sector0, 28);
    bpb->large_sectors_on_media        = readi32(sector0, 32);
    // EBR

    bpb->count_sectors_per_FAT32          = readi32(sector0, 36);
    bpb->flags                            = readi16(sector0, 40);
    bpb->FAT_version                      = readi16(sector0, 42);
    bpb->cluster_number_root_dir          = readi32(sector0, 44);
    bpb->sector_number_FSInfo             = readi16(sector0, 48);
    bpb->sector_number_backup_boot_sector = readi16(sector0, 50);

    // Skip 12 bytes
    bpb->drive_number  = sector0[64];
    bpb->windows_flags = sector0[65];
    bpb->signature     = sector0[66];

    memcpy(&bpb->volume_label, sector0 + 71, 11);
    bpb->volume_label[11] = 0;
    memcpy(&bpb->system_id, sector0 + 82, 8);
    bpb->system_id[8] = 0;
}

static unsigned int sector_for_cluster(f32 *fs, unsigned int cluster) {
    return fs->cluster_begin_sector + ((cluster - 2) * fs->bpb.sectors_per_cluster);
}

// CLUSTER NUMBERS START AT 2 (for some reason...)
void getCluster(f32 *fs, unsigned char *buff, unsigned int cluster_number) { // static
    if (cluster_number >= EOC) {
        kpanic("Can't get cluster. Hit End Of Chain.");
        return;
    }

    unsigned int sector       = sector_for_cluster(fs, cluster_number);
    unsigned int sector_count = fs->bpb.sectors_per_cluster;
    getSector(fs, buff, sector, sector_count);
}

static void putCluster(f32 *fs, unsigned char *buff, unsigned int cluster_number) {
    unsigned int sector       = sector_for_cluster(fs, cluster_number);
    unsigned int sector_count = fs->bpb.sectors_per_cluster;
    putSector(fs, buff, sector, sector_count);
}

unsigned int get_next_cluster_id(f32 *fs, unsigned int cluster) { // static
    return fs->FAT[cluster] & 0x0FFFFFFF;
}

static char *parse_long_name(unsigned char *entries, unsigned char entry_count) {
    // each entry can hold 13 characters.
    char *name = kmalloc(entry_count * 13);
    int  i, j;
    for (i = 0; i < entry_count; i++) {
        unsigned char *entry       = entries + (i * 32);
        unsigned char entry_no     = (unsigned char) entry[0] & 0x0F;
        char          *name_offset = name + ((entry_no - 1) * 13);

        for (j = 1; j < 10; j += 2) {
            if (entry[j] >= 32 && entry[j] <= 127) {
                *name_offset = entry[j];
            }
            else {
                *name_offset = 0;
            }
            name_offset++;
        }
        for (j = 14; j < 25; j += 2) {
            if (entry[j] >= 32 && entry[j] <= 127) {
                *name_offset = entry[j];
            }
            else {
                *name_offset = 0;
            }
            name_offset++;
        }
        for (j = 28; j < 31; j += 2) {
            if (entry[j] >= 32 && entry[j] <= 127) {
                *name_offset = entry[j];
            }
            else {
                *name_offset = 0;
            }
            name_offset++;
        }
    }
    return name;
}

static void clear_cluster(f32 *fs, unsigned int cluster) {
    unsigned char buffer[fs->cluster_size];
    memset(buffer, 0, fs->cluster_size);
    putCluster(fs, buffer, cluster);
}

static unsigned int allocateCluster(f32 *fs) {
    unsigned int i, ints_per_fat = (512 * fs->bpb.count_sectors_per_FAT32) / 4;
    for (i = fs->cluster_alloc_hint; i < ints_per_fat; i++) {
        if (fs->FAT[i] == 0) {
            fs->FAT[i] = 0x0FFFFFFF;
            clear_cluster(fs, i);
            fs->cluster_alloc_hint = i + 1;
            return i;
        }
    }
    for (i = 0; i < fs->cluster_alloc_hint; i++) {
        if (fs->FAT[i] == 0) {
            fs->FAT[i] = 0x0FFFFFFF;
            clear_cluster(fs, i);
            fs->cluster_alloc_hint = i + 1;
            return i;
        }
    }
    return 0;
}

// Creates a checksum for an 8.3 filename
// must be in directory-entry format, i.e.
// fat32.c -> "FAT32   C  "
static unsigned char checksum_fname(char *fname) {
    unsigned int  i;
    unsigned char checksum = 0;
    for (i = 0; i < 11; i++) {
        unsigned char highbit = (checksum & 0x1) << 7;
        checksum = ((checksum >> 1) & 0x7F) | highbit;
        checksum = checksum + fname[i];
    }
    return checksum;
}

static void write_8_3_filename(char *fname, unsigned char *buffer) {
    memset(buffer, ' ', 11);
    unsigned int namelen   = strlen(fname);
    // find the extension
    int          i;
    int          dot_index = -1;
    for (i = namelen - 1; i >= 0; i--) {
        if (fname[i] == '.') {
            // Found it!
            dot_index = i;
            break;
        }
    }

    // Write the extension
    if (dot_index >= 0) {
        for (i = 0; i < 3; i++) {
            unsigned int  c_index = dot_index + 1 + i;
            unsigned char c       = c_index >= namelen ? ' ' : k_toupper(fname[c_index]);
            buffer[8 + i] = c;
        }
    }
    else {
        for (i = 0; i < 3; i++) {
            buffer[8 + i] = ' ';
        }
    }

    // Write the filename.
    unsigned int firstpart_len = namelen;
    if (dot_index >= 0) {
        firstpart_len = dot_index;
    }
    if (firstpart_len > 8) {
        // Write the weird tilde thing.
        for (i    = 0; i < 6; i++) {
            buffer[i] = k_toupper(fname[i]);
        }
        buffer[6] = '~';
        buffer[7] = '1'; // probably need to enumerate like files and increment.
    }
    else {
        // Just write the file name.
        unsigned int j;
        for (j = 0; j < firstpart_len; j++) {
            buffer[j] = k_toupper(fname[j]);
        }
    }
}

static unsigned char *locate_entries(f32 *fs, unsigned char *cluster_buffer, struct directory *dir, unsigned int count,
                                     unsigned int *found_cluster) {
    unsigned int dirs_per_cluster = fs->cluster_size / 32;

    unsigned int i;
    long         index   = -1;
    unsigned int cluster = dir->cluster;
    while (1) {
        getCluster(fs, cluster_buffer, cluster);

        unsigned int in_a_row = 0;
        for (i = 0; i < dirs_per_cluster; i++) {
            unsigned char *entry     = cluster_buffer + (i * 32);
            unsigned char first_byte = entry[0];
            if (first_byte == 0x00 || first_byte == 0xE5) {
                in_a_row++;
            }
            else {
                in_a_row = 0;
            }

            if (in_a_row == count) {
                index = i - (in_a_row - 1);
                break;
            }
        }
        if (index >= 0) {
            // We found a spot to put our crap!
            break;
        }

        unsigned int next_cluster = fs->FAT[cluster];
        if (next_cluster >= EOC) {
            next_cluster = allocateCluster(fs);
            if (!next_cluster) {
                return 0;
            }
            fs->FAT[cluster] = next_cluster;
        }
        cluster = next_cluster;
    }
    *found_cluster = cluster;
    return cluster_buffer + (index * 32);
}

static void write_long_filename_entries(unsigned char *start, unsigned int num_entries, char *fname) {
    // Create a short filename to use for the checksum.
    char shortfname[12];
    shortfname[11] = 0;
    write_8_3_filename(fname, (unsigned char *) shortfname);
    unsigned char checksum = checksum_fname(shortfname);

    /* Write the long-filename entries */
    // tracks the number of characters we've written into
    // the long-filename entries.
    unsigned int  writtenchars = 0;
    char          *nameptr     = fname;
    unsigned int  namelen      = strlen(fname);
    unsigned char *entry       = NULL;
    unsigned int  i;
    for (i = 0; i < num_entries; i++) {
        // reverse the entry order
        entry = start + ((num_entries - 1 - i) * 32);
        // Set the entry number
        entry[0]  = i + 1;
        entry[13] = checksum;

        // Characters are 16 bytes in long-filename entries (j+=2)
        // And they only go in certain areas in the 32-byte
        // block. (why we have three loops)
        unsigned int j;
        for (j = 1; j < 10; j += 2) {
            if (writtenchars < namelen) {
                entry[j] = *nameptr;
            }
            else {
                entry[j] = 0;
            }
            nameptr++;
            writtenchars++;
        }
        for (j = 14; j < 25; j += 2) {
            if (writtenchars < namelen) {
                entry[j] = *nameptr;
            }
            else {
                entry[j] = 0;
            }
            nameptr++;
            writtenchars++;
        }
        for (j    = 28; j < 31; j += 2) {
            if (writtenchars < namelen) {
                entry[j] = *nameptr;
            }
            else {
                entry[j] = 0;
            }
            nameptr++;
            writtenchars++;
        }
        // Mark the attributes byte as LFN (Long File Name)
        entry[11] = LFN;
    }
    // Mark the last(first) entry with the end-of-long-filename bit
    entry[0] |= 0x40;
}

f32 *makeFilesystem(char *fatSystem) {
    if (sd_init() != SD_OK) {
        return NULL;
    }

    f32 *fs = kmalloc(sizeof(struct f32));

    printf("Filesystem identified!\n");
    read_bpb(fs, &fs->bpb);

    trim_spaces(fs->bpb.system_id, 8);
    if (strcmp(fs->bpb.system_id, "FAT32") != 0) {
        kfree(fs);
        return NULL;
    }

    printf("Sectors per cluster: %d\n", fs->bpb.sectors_per_cluster);

    fs->partition_begin_sector = 0;
    fs->fat_begin_sector       = fs->partition_begin_sector + fs->bpb.reserved_sectors;
    fs->cluster_begin_sector   = fs->fat_begin_sector + (fs->bpb.FAT_count * fs->bpb.count_sectors_per_FAT32);
    fs->cluster_size           = 512 * fs->bpb.sectors_per_cluster;
    fs->cluster_alloc_hint     = 0;

    // Load the FAT
    unsigned int bytes_per_fat = 512 * fs->bpb.count_sectors_per_FAT32;
    fs->FAT = kmalloc(bytes_per_fat);
    unsigned int sector_i;
    for (sector_i = 0; sector_i < fs->bpb.count_sectors_per_FAT32; sector_i++) {
        unsigned char sector[512];
        unsigned int  integer_j;
        getSector(fs, sector, fs->fat_begin_sector + sector_i, 1);
        for (integer_j = 0; integer_j < 512 / 4; integer_j++) {
            fs->FAT[sector_i * (512 / 4) + integer_j]
                    = readi32(sector, integer_j * 4);
        }
    }
    return fs;
}

void destroyFilesystem(f32 *fs) {
    printf("Destroying filesystem.\n");
    flushFAT(fs);
    kfree(fs->FAT);
    kfree(fs);
}

const struct bios_parameter_block *getBPB(f32 *fs) {
    return &fs->bpb;
}

void populate_root_dir(f32 *fs, struct directory *dir) {
    populate_dir(fs, dir, 2);           // first cluster always 2
}

// Populates dirent with the directory entry starting at start
// Returns a pointer to the next 32-byte chunk after the entry
// or NULL if either start does not point to a valid entry, or
// there are not enough entries to build a struct dir_entry
static unsigned char *read_dir_entry(f32 *fs, unsigned char *start, unsigned char *end, struct dir_entry *dirent) {
    unsigned char first_byte = start[0];
    unsigned char *entry     = start;
    if (first_byte == 0x00 || first_byte == 0xE5) {
        // NOT A VALID ENTRY!
        return NULL;
    }

    unsigned int LFNCount = 0;
    while (entry[11] == LFN) {
        LFNCount++;
        entry += 32;
        if (entry == end) {
            return NULL;
        }
    }
    if (LFNCount > 0) {
        dirent->name = parse_long_name(start, LFNCount);
    }
    else {
        // There's no long file name.
        // Trim up the short filename.
        dirent->name = kmalloc(13);
        memcpy(dirent->name, entry, 11);
        dirent->name[11] = 0;
        char extension[4];
        memcpy(extension, dirent->name + 8, 3);
        extension[3] = 0;
        trim_spaces(extension, 3);

        dirent->name[8] = 0;
        trim_spaces(dirent->name, 8);

        if (strlen(extension) > 0) {
            unsigned int len = strlen(dirent->name);
            dirent->name[len++] = '.';
            memcpy(dirent->name + len, extension, 4);
        }
    }

    dirent->dir_attrs = entry[11];;
    unsigned short first_cluster_high = readi16(entry, 20);
    unsigned short first_cluster_low  = readi16(entry, 26);
    dirent->first_cluster = first_cluster_high << 16 | first_cluster_low;
    dirent->file_size     = readi32(entry, 28);
    return entry + 32;
}

// This is a complicated one. It parses a directory entry into the dir_entry pointed to by target_dirent.
// root_cluster  must point to a buffer big enough for two clusters.
// entry         points to the entry the caller wants to parse, and must point to a spot within root_cluster.
// nextentry     will be modified to hold the next spot within root_entry to begin looking for entries.
// cluster       is the cluster number of the cluster loaded into root_cluster.
// secondcluster will be modified IF this function needs to load another cluster to continue parsing
//               the entry, in which case, it will be set to the value of the cluster loaded.
//
void next_dir_entry(f32 *fs, unsigned char *root_cluster, unsigned char *entry, unsigned char **nextentry,
                    struct dir_entry *target_dirent, unsigned int cluster, unsigned int *secondcluster) {

    unsigned char *end_of_cluster = root_cluster + fs->cluster_size;
    *nextentry = read_dir_entry(fs, entry, end_of_cluster, target_dirent);
    if (!*nextentry) {
        // Something went wrong!
        // Either the directory entry spans the bounds of a cluster,
        // or the directory entry is invalid.
        // Load the next cluster and retry.

        // Figure out how much of the last cluster to "replay"
        unsigned int bytes_from_prev_chunk = end_of_cluster - entry;

        *secondcluster = get_next_cluster_id(fs, cluster);
        if (*secondcluster >= EOC) {
            // There's not another directory cluster to load
            // and the previous entry was invalid!
            // It's possible the filesystem is corrupt or... you know...
            // my software could have bugs.
            kpanic("FOUND BAD DIRECTORY ENTRY!");
        }
        // Load the cluster after the previous saved entries.
        getCluster(fs, root_cluster + fs->cluster_size, *secondcluster);
        // Set entry to its new location at the beginning of root_cluster.
        entry = root_cluster + fs->cluster_size - bytes_from_prev_chunk;

        // Retry reading the entry.
        *nextentry = read_dir_entry(fs, entry, end_of_cluster + fs->cluster_size, target_dirent);
        if (!*nextentry) {
            // Still can't parse the directory entry.
            // Something is very wrong.
            kpanic("FAILED TO READ DIRECTORY ENTRY! THE SOFTWARE IS BUGGY!\n");
        }
    }
}

// TODO: Refactor this. It is so similar to delFile that it would be nice
// to combine the similar elements.
// WARN: If you fix a bug in this function, it's likely you will find the same
// bug in delFile.
void populate_dir(f32 *fs, struct directory *dir, unsigned int cluster) {
    dir->cluster                  = cluster;
    unsigned int dirs_per_cluster = fs->cluster_size / 32;
    unsigned int max_dirs         = 0;
    dir->entries = 0;
    unsigned int entry_count = 0;

    while (1) {
        max_dirs += dirs_per_cluster;
        dir->entries = krealloc(dir->entries, max_dirs * sizeof(struct dir_entry));
        // Double the size in case we need to read a directory entry that
        // spans the bounds of a cluster.
        unsigned char root_cluster[fs->cluster_size * 2];
        getCluster(fs, root_cluster, cluster);

        unsigned char *entry = root_cluster;
        while ((unsigned int) (entry - root_cluster) < fs->cluster_size) {
            unsigned char first_byte = *entry;
            if (first_byte == 0x00 || first_byte == 0xE5) {
                // This directory entry has never been written
                // or it has been deleted.
                entry += 32;
                continue;
            }

            unsigned int     secondcluster  = 0;
            unsigned char    *nextentry     = NULL;
            struct dir_entry *target_dirent = dir->entries + entry_count;
            next_dir_entry(fs, root_cluster, entry, &nextentry, target_dirent, cluster, &secondcluster);
            entry = nextentry;
            if (secondcluster) {
                cluster = secondcluster;
            }

            entry_count++;
        }
        cluster = get_next_cluster_id(fs, cluster);
        if (cluster >= EOC) break;
    }
    dir->num_entries = entry_count;
}

static void zero_FAT_chain(f32 *fs, unsigned int start_cluster) {
    unsigned int cluster = start_cluster;
    while (cluster < EOC && cluster != 0) {
        unsigned int next_cluster = fs->FAT[cluster];
        fs->FAT[cluster] = 0;
        cluster = next_cluster;
    }
    flushFAT(fs);
}

// TODO: Refactor this. It is so similar to populate_dir that it would be nice
// to combine the similar elements.
// WARN: If you fix a bug in this function, it's likely you will find the same
// bug in populate_dir.
void delFile(f32 *fs, struct directory *dir, char *filename) { //struct dir_entry *dirent) {
    unsigned int cluster = dir->cluster;

    // Double the size in case we need to read a directory entry that
    // spans the bounds of a cluster.
    unsigned char    root_cluster[fs->cluster_size * 2];
    struct dir_entry target_dirent;

    // Try to locate and invalidate the directory entries corresponding to the
    // filename in dirent.
    while (1) {
        getCluster(fs, root_cluster, cluster);

        unsigned char *entry = root_cluster;
        while ((unsigned int) (entry - root_cluster) < fs->cluster_size) {
            unsigned char first_byte = *entry;
            if (first_byte == 0x00 || first_byte == 0xE5) {
                // This directory entry has never been written
                // or it has been deleted.
                entry += 32;
                continue;
            }

            unsigned int  secondcluster = 0;
            unsigned char *nextentry    = NULL;
            next_dir_entry(fs, root_cluster, entry, &nextentry, &target_dirent, cluster, &secondcluster);

            // We have a target dirent! see if it's the one we want!
            if (strcmp(target_dirent.name, filename) == 0) {
                // We found it! Invalidate all the entries.
                memset(entry, 0, nextentry - entry);
                putCluster(fs, root_cluster, cluster);
                if (secondcluster) {
                    putCluster(fs, root_cluster + fs->cluster_size, secondcluster);
                }
                zero_FAT_chain(fs, target_dirent.first_cluster);
                kfree(target_dirent.name);
                return;
            }
            else {
                // We didn't find it. Continue.
                entry = nextentry;
                if (secondcluster) {
                    cluster = secondcluster;
                }
            }
            kfree(target_dirent.name);

        }
        cluster = get_next_cluster_id(fs, cluster);
        if (cluster >= EOC) return;
    }
}

void free_directory(f32 *fs, struct directory *dir) {
    unsigned int i;
    for (i = 0; i < dir->num_entries; i++) {
        kfree(dir->entries[i].name);
    }
    kfree(dir->entries);
}

unsigned char *readFile(f32 *fs, struct dir_entry *dirent) {
    unsigned char *file        = kmalloc(dirent->file_size);
    unsigned char *filecurrptr = file;
    unsigned int  cluster      = dirent->first_cluster;
    unsigned int  copiedbytes  = 0;
    while (1) {
        unsigned char cbytes[fs->cluster_size];
        getCluster(fs, cbytes, cluster);

        unsigned int remaining = dirent->file_size - copiedbytes;
        unsigned int to_copy   = remaining > fs->cluster_size ? fs->cluster_size : remaining;

        memcpy(filecurrptr, cbytes, to_copy);

        filecurrptr += fs->cluster_size;
        copiedbytes += to_copy;

        cluster = get_next_cluster_id(fs, cluster);
        if (cluster >= EOC) break;
    }
    return file;
}

static void
writeFile_impl(f32 *fs, struct directory *dir, unsigned char *file, char *fname, unsigned int flen, unsigned char attrs,
               unsigned int setcluster) {
    unsigned int required_clusters = flen / fs->cluster_size;
    if (flen % fs->cluster_size != 0) required_clusters++;
    if (required_clusters == 0) required_clusters++; // Allocate at least one cluster.
    // One for the traditional 8.3 name, one for each 13 charaters in the extended name.
    // Int division truncates, so if there's a remainder from length / 13, add another entry.
    unsigned int required_entries_long_fname = (strlen(fname) / 13);
    if (strlen(fname) % 13 > 0) {
        required_entries_long_fname++;
    }

    unsigned int required_entries_total = required_entries_long_fname + 1;

    unsigned int  cluster; // The cluster number that the entries are found in
    unsigned char root_cluster[fs->cluster_size];
    unsigned char *start_entries = locate_entries(fs, root_cluster, dir, required_entries_total, &cluster);
    write_long_filename_entries(start_entries, required_entries_long_fname, fname);

    // Write the actual file entry;
    unsigned char *actual_entry = start_entries + (required_entries_long_fname * 32);
    write_8_3_filename(fname, actual_entry);

    // Actually write the file!
    unsigned int writtenbytes = 0;
    unsigned int prevcluster  = 0;
    unsigned int firstcluster = 0;
    unsigned int i;
    if (setcluster) {
        // Caller knows where the first cluster is.
        // Don't allocate or write anything.
        firstcluster = setcluster;
    }
    else {
        for (i = 0; i < required_clusters; i++) {
            unsigned int currcluster = allocateCluster(fs);
            if (!firstcluster) {
                firstcluster = currcluster;
            }
            unsigned char cluster_buffer[fs->cluster_size];
            memset(cluster_buffer, 0, fs->cluster_size);
            unsigned int bytes_to_write = flen - writtenbytes;
            if (bytes_to_write > fs->cluster_size) {
                bytes_to_write = fs->cluster_size;
            }
            memcpy(cluster_buffer, file + writtenbytes, bytes_to_write);
            writtenbytes += bytes_to_write;
            putCluster(fs, cluster_buffer, currcluster);
            if (prevcluster) {
                fs->FAT[prevcluster] = currcluster;
            }
            prevcluster = currcluster;
        }
    }

    // Write the other fields of the actual entry
    // We do it down here because we need the first cluster
    // number.

    // attrs
    actual_entry[11] = attrs;

    // high cluster bits
    actual_entry[20] = (firstcluster >> 16) & 0xFF;
    actual_entry[21] = (firstcluster >> 24) & 0xFF;

    // low cluster bits
    actual_entry[26] = (firstcluster) & 0xFF;
    actual_entry[27] = (firstcluster >> 8) & 0xFF;

    // file size
    actual_entry[28] = flen & 0xFF;
    actual_entry[29] = (flen >> 8) & 0xFF;
    actual_entry[30] = (flen >> 16) & 0xFF;
    actual_entry[31] = (flen >> 24) & 0xFF;

    // Write the cluster back to disk
    putCluster(fs, root_cluster, cluster);
    flushFAT(fs);
}

void writeFile(f32 *fs, struct directory *dir, unsigned char *file, char *fname, unsigned int flen) {
    writeFile_impl(fs, dir, file, fname, flen, 0, 0);
}

static void mkdir_subdirs(f32 *fs, struct directory *dir, unsigned int parentcluster) {
    writeFile_impl(fs, dir, NULL, ".", 0, DIRECTORY, dir->cluster);
    writeFile_impl(fs, dir, NULL, "..", 0, DIRECTORY, parentcluster);
}

void mkdir(f32 *fs, struct directory *dir, char *dirname) {
    writeFile_impl(fs, dir, NULL, dirname, 0, DIRECTORY, 0);

    // We need to add the subdirectories '.' and '..'
    struct directory subdir;
    populate_dir(fs, &subdir, dir->cluster);
    unsigned int i;
    for (i = 0; i < subdir.num_entries; i++) {
        if (strcmp(subdir.entries[i].name, dirname) == 0) {
            struct directory newsubdir;
            populate_dir(fs, &newsubdir, subdir.entries[i].first_cluster);
            mkdir_subdirs(fs, &newsubdir, subdir.cluster);
            free_directory(fs, &newsubdir);
        }
    }
    free_directory(fs, &subdir);
}

void print_directory(f32 *fs, struct directory *dir) {
    unsigned int i;
    unsigned int max_name = 0;
    for (i = 0; i < dir->num_entries; i++) {
        unsigned int namelen = strlen(dir->entries[i].name);
        max_name = namelen > max_name ? namelen : max_name;
    }

    char *namebuff = kmalloc(max_name + 1);
    for (i = 0; i < dir->num_entries; i++) {
//        printf("[%d] %*s %c %8d bytes ",
//               i,
//               -max_name,
//               dir->entries[i].name,
//               dir->entries[i].dir_attrs & DIRECTORY?'D':' ',
//               dir->entries[i].file_size, dir->entries[i].first_cluster);
        printf("[%d] ", i);


        unsigned int j;
        for (j             = 0; j < max_name; j++) {
            namebuff[j] = ' ';
        }
        namebuff[max_name] = 0;
        for (j = 0; j < strlen(dir->entries[i].name); j++) {
            namebuff[j] = dir->entries[i].name[j];
        }

        printf("%s \t\t%c %d ",
               namebuff,
               dir->entries[i].dir_attrs & DIRECTORY ? 'D' : ' ',
               dir->entries[i].file_size);

        unsigned int cluster       = dir->entries[i].first_cluster;
        unsigned int cluster_count = 1;
        while (1) {
            cluster = fs->FAT[cluster];
            if (cluster >= EOC) break;
            if (cluster == 0) {
                kpanic("BAD CLUSTER CHAIN! FS IS CORRUPT!");
            }
            cluster_count++;
        }
        printf("\tClusters: [%d]\n", cluster_count);
    }
    kfree(namebuff);
}

unsigned int count_free_clusters(f32 *fs) {
    unsigned int clusters_in_fat = (fs->bpb.count_sectors_per_FAT32 * 512) / 4;
    unsigned int i;
    unsigned int count           = 0;
    for (i = 0; i < clusters_in_fat; i++) {
        if (fs->FAT[i] == 0) {
            count++;
        }
    }
    return count;
}

/**
 * find a directory in current parent directory
 * @param fs
 * @param parent_dir
 * @param name
 * @return
 */
unsigned int find_directory(f32 *fs, struct directory *parent_dir, unsigned char *name){
    writeFile_impl(fs, dir, NULL, dirname, 0, DIRECTORY, 0);
    unsigned int required_entries_long_fname = (strlen(fname) / 13);
    if (strlen(name) % 13 > 0) required_entries_long_fname++;


    unsigned int required_entries_total = required_entries_long_fname + 1;

    unsigned int  cluster; // The cluster number that the entries are found in
    unsigned char root_cluster[fs->cluster_size];
    unsigned char *start_entries = locate_entries(fs, root_cluster, dir, required_entries_total, &cluster);
    write_long_filename_entries(start_entries, required_entries_long_fname, fname);

    // Write the actual file entry;
    unsigned char *actual_entry = start_entries + (required_entries_long_fname * 32);
    write_8_3_filename(fname, actual_entry);

}

struct file_handler *open_file(unsigned char * filepath){
    struct file_handler *file = kmalloc(sizeof (file_handler));
    // search file


    // get file information

    // return
    return file;
}