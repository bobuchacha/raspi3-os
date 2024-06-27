#define SD_OK                0
#define SD_TIMEOUT          -1
#define SD_ERROR            -2

// these function should be declared in sdcard.c of specific device

int sd_init();
int sd_readblock(unsigned int lba, unsigned char *buffer, unsigned int num);
int sd_writeblock(unsigned char *buffer, unsigned int lba, unsigned int num);
int sd_block_read(void* private, unsigned int begin, int count, void* buf);
int sd_block_write(void* private, unsigned int begin, int count, const void* buf);