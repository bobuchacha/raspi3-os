int fat_getpartition(void);
void fat_listdirectory(void);
char *fat_readfile(unsigned int cluster);
unsigned int fat_getcluster(char *fn);