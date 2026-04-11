#ifndef KERNEL_INCLUDE_FILESYSTEM_FAT32_H
#define KERNEL_INCLUDE_FILESYSTEM_FAT32_H

#include "filesystem.h"

namespace filesystem {

    class Fat32Filesystem final {
    public:
        static FilesystemDriver* driver(void);
        static void* system_volume_state(void);
    };

} // namespace filesystem

#endif // KERNEL_INCLUDE_FILESYSTEM_FAT32_H