#include <ros.h>
#include "vfs/vfs.h"
#include <memory.h>
#include <log.h>

int file_last_status;

PFileDesc file_open(char* path, int mode) {
        PFileDesc f = (Pointer)kmalloc(sizeof(FileDesc));
        if (!f) return null;
        file_last_status = vfs_fd_open(f, path, mode);
        if (file_last_status < 0) {
                log_error("Can not open file %s. Code %d", path, file_last_status);
                kfree((Address)f);
                return null;
        }
        return f;       
}

Buffer file_read(PFileDesc f, int position, int length){
        if (!f) return null;

        if (vfs_fd_seek(f, position, SEEK_SET) < 0) return null;

        Buffer buff = (Pointer)kmalloc(length);  
        file_last_status = vfs_fd_read(f, buff, length);

        if (file_last_status < 0) {
                kfree((Address)buff);
                return null;
        }

        return buff;
}

void file_close(PFileDesc f) {
        file_last_status = vfs_fd_close(f);
        kfree((Address)f);
}