#ifndef FAT32_H
#define FAT32_H
#define READONLY  1
#define HIDDEN    (1 << 1)
#define SYSTEM    (1 << 2)
#define VolumeID  (1 << 3)
#define DIRECTORY (1 << 4)
#define ARCHIVE   (1 << 5)
#define LFN (READONLY | HIDDEN | SYSTEM | VolumeID)
#define EOC 0x0FFFFFF8          // EOC = End Of Chain

struct dir_entry {
    char          *name;
    unsigned char dir_attrs;
    unsigned int  first_cluster;
    unsigned int  file_size;
};

struct directory {
    unsigned int     cluster;
    struct dir_entry *entries;
    unsigned int     num_entries;
};

struct file_handler {
    unsigned long *handler;
    unsigned char *file_name;
    unsigned int filesize;
};

struct bios_parameter_block {
    unsigned short bytes_per_sector;          // IMPORTANT
    unsigned char  sectors_per_cluster;        // IMPORTANT
    unsigned short reserved_sectors;          // IMPORTANT
    unsigned char  FAT_count;                  // IMPORTANT
    unsigned short dir_entries;
    unsigned short total_sectors;
    unsigned char  media_descriptor_type;
    unsigned short count_sectors_per_FAT12_16; // FAT12/FAT16 only.
    unsigned short count_sectors_per_track;
    unsigned short count_heads_or_sizes_on_media;
    unsigned int   count_hidden_sectors;
    unsigned int   large_sectors_on_media;  // This is set instead of total_sectors if it's > 65535

    // Extended Boot Record
    unsigned int   count_sectors_per_FAT32;   // IMPORTANT
    unsigned short flags;
    unsigned short FAT_version;
    unsigned int   cluster_number_root_dir;   // IMPORTANT
    unsigned short sector_number_FSInfo;
    unsigned short sector_number_backup_boot_sector;
    unsigned char  drive_number;
    unsigned char  windows_flags;
    unsigned char  signature;                  // IMPORTANT
    unsigned int   volume_id;
    char           volume_label[12];
    char           system_id[9];
};

// REFACTOR
// I want to get rid of this from the header. This should be internal
// implementation, but for now, it's too convenient for stdio.c impl.

struct f32 {
    unsigned int                *FAT;
    struct bios_parameter_block bpb;
    unsigned int                partition_begin_sector;
    unsigned int                fat_begin_sector;
    unsigned int                cluster_begin_sector;
    unsigned int                cluster_size;
    unsigned int                cluster_alloc_hint;
};

typedef struct f32 f32;

void getCluster(f32 *fs, unsigned char *buff, unsigned int cluster_number);

unsigned int get_next_cluster_id(f32 *fs, unsigned int cluster);

// END REFACTOR

/**
 * create file system for manipulating
 * @param fatSystem
 * @return
 */
f32 *makeFilesystem(char *fatSystem);

/**
 * clean the memory
 * @param fs
 */
void destroyFilesystem(f32 *fs);

const struct bios_parameter_block *getBPB(f32 *fs);

void populate_root_dir(f32 *fs, struct directory *dir);

void populate_dir(f32 *fs, struct directory *dir, unsigned int cluster);

void free_directory(f32 *fs, struct directory *dir);

unsigned char *readFile(f32 *fs, struct dir_entry *dirent);

void writeFile(f32 *fs, struct directory *dir, unsigned char *file, char *fname, unsigned int flen);

void mkdir(f32 *fs, struct directory *dir, char *dirname);

void delFile(f32 *fs, struct directory *dir, char *filename);

void print_directory(f32 *fs, struct directory *dir);

unsigned int count_free_clusters(f32 *fs);

/**
 * opens a file and return a file handler struct pointer
 * @param filepath
 * @return
 */
struct file_handler *open_file(unsigned char * filepath);

extern f32 *master_fs;

#endif