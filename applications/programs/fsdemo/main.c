#include "app/fs_module.h"
#include "logger.h"

static void fsdemo_list_dir(const char* path) {
    UserDirectoryEntry entry;

    app_log_info("fsdemo", "listing %s", path);
    for (unsigned long index = 0;; index++) {
        long status = fs_module_dir_entry(path, index, &entry);

        if (status <= 0) {
            break;
        }
        app_log_info("fsdemo", "dir[%lu] = %s size=%lu attr=0x%lx", index, entry.name, entry.size, entry.attr);
    }
}

static void fsdemo_dump_file(const char* path) {
    char buffer[192];
    long read_status;

    read_status = fs_module_read_file(path, 0, buffer, sizeof(buffer) - 1);
    if (read_status > 0) {
        buffer[read_status] = '\0';
        app_log_info("fsdemo", "read %s:\n%s", path, buffer);
    }
    else {
        app_log_warn("fsdemo", "read %s failed: %ld", path, read_status);
    }
}

long main(void) {
    long backend_kind = fs_module_query(FS_MODULE_QUERY_BACKEND_KIND);
    long features = fs_module_query(FS_MODULE_QUERY_FEATURES);
    const char* file_path = "/DEMO.TXT";
    const char* file_text = "fsdemo wrote this file\n";
    long write_status;
    long remove_status;

    app_log_info("fsdemo", "fs backend kind=%ld features=0x%lx", backend_kind, features);
    if (backend_kind != FS_MODULE_BACKEND_FAT32) {
        app_log_error("fsdemo", "expected FAT32 backend but got %ld", backend_kind);
        return 1;
    }

    fsdemo_list_dir("/");

    if ((features & FS_MODULE_FEATURE_WRITE_FILE) == 0 ||
        (features & FS_MODULE_FEATURE_REMOVE) == 0 ||
        (features & FS_MODULE_FEATURE_LIST_DIR) == 0 ||
        (features & FS_MODULE_FEATURE_READ_FILE) == 0) {
        app_log_warn("fsdemo", "backend does not advertise full read/write/remove support");
        return 0;
    }

    write_status = fs_module_write_file(file_path, file_text, (unsigned long)sizeof("fsdemo wrote this file\n") - 1, FS_MODULE_WRITE_TRUNCATE);
    if (write_status < 0) {
        app_log_error("fsdemo", "write %s failed: %ld", file_path, write_status);
        return 1;
    }

    app_log_info("fsdemo", "wrote %ld bytes to %s", write_status, file_path);
    fsdemo_list_dir("/");
    fsdemo_dump_file(file_path);

    remove_status = fs_module_remove(file_path);
    if (remove_status < 0) {
        app_log_error("fsdemo", "remove %s failed: %ld", file_path, remove_status);
        return 1;
    }

    app_log_info("fsdemo", "removed %s", file_path);
    fsdemo_list_dir("/");

    return 0;
}
