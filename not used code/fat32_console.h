#ifndef FAT32_CONSOLE_H
#define FAT32_CONSOLE_H

#include "filesystem/FAT/fat32.h"

void fat32_console(f32 *fs);
int handle_commands(f32 *fs, struct directory *dir, char *buffer);
#endif