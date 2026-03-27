#include "arch/cortex-a53/mmu.h"
#include "filesystem/vfs/vfs.h"
#include "log.h"
#include "memory.h"
#include "printf.h"
#include "system-ext.h"
#include "task.h"
#include "user-exe.h"
#include "utils.h"

int process_map_page(Task* task, Address pa, Address va, Flags flags);

#define SYSTEM_EXT_MAGIC "ROSXEXT"
#define SYSTEM_EXT_VERSION 1U
#define SYSTEM_EXT_BLOCK_SIZE 512U
#define SYSTEM_EXT_NAME_MAX 64U
#define SYSTEM_EXT_FUNC_NAME_MAX 56U
#define SYSTEM_EXT_MAX 8U
#define SYSTEM_EXT_MAX_FUNCS 16U

typedef struct __attribute__((packed)) {
    char magic[8];
    UInt version;
    UInt block_count;
    UInt function_count;
    UInt flags;
    char name[SYSTEM_EXT_NAME_MAX];
    ULong init_address;
    ULong loop_address;
    UByte reserved[SYSTEM_EXT_BLOCK_SIZE - 8 - 4 - 4 - 4 - 4 - SYSTEM_EXT_NAME_MAX - 8 - 8];
} SystemExtHeader;

typedef struct __attribute__((packed)) {
    char name[SYSTEM_EXT_FUNC_NAME_MAX];
    ULong address;
} SystemExtFunctionRecord;

typedef struct {
    char name[SYSTEM_EXT_FUNC_NAME_MAX];
    Address address;
} SystemExtFunction;

typedef struct {
    Bool used;
    char path[USER_EXEC_PATH_MAX];
    char name[SYSTEM_EXT_NAME_MAX];
    Task task;
    Address init_address;
    Address loop_address;
    Bool loop_logged;
    Bool loop_tick_valid;
    unsigned long last_idle_tick;
    UInt function_count;
    SystemExtFunction functions[SYSTEM_EXT_MAX_FUNCS];
} SystemExtension;

static SystemExtension system_extensions[SYSTEM_EXT_MAX];

static void system_ext_console_write(const char* text) {
    if (!text) {
        return;
    }

    console_lock();
    printf("%s", (char*)text);
    console_unlock();
}

static unsigned long system_ext_get_ticks(void) {
    return schedler_get_ticks();
}

static void system_ext_get_mem_info(SystemExtMemInfo* info) {
    if (!info) {
        return;
    }

    info->total_bytes = mem_get_size();
    info->page_size = PAGE_SIZE;
    info->free_pages = mem_get_free_pages();
    info->free_bytes = info->free_pages * PAGE_SIZE;
    info->heap_total_bytes = mem_heap_total_bytes();
    info->heap_used_bytes = mem_heap_used_bytes();
    info->heap_free_bytes = mem_heap_free_bytes();
}

static int ext_read_exact(struct FileDesc* fd, void* buf, unsigned int size) {
    return vfs_fd_read(fd, buf, size) == (int)size ? 0 : -1;
}

static int ext_validate_exe_header(const UserExeHeader* header) {
    if (strncmp(header->magic, USER_EXE_MAGIC, 8) != 0) {
        return -1;
    }
    if (header->version != USER_EXE_VERSION) {
        return -1;
    }
    if (header->header_size != sizeof(UserExeHeader)) {
        return -1;
    }
    if (header->segment_table_offset != USER_EXE_SEGMENT_TABLE_OFFSET) {
        return -1;
    }
    if (header->segment_count == 0 || header->segment_count > USER_EXE_MAX_SEGMENTS) {
        return -1;
    }

    for (UInt index = 0; index < header->segment_count; index++) {
        const UserExeSegment* segment = &header->segments[index];

        if (segment->file_size > segment->memory_size) {
            return -1;
        }
        if (segment->memory_size == 0) {
            continue;
        }
        if (segment->virtual_address < VA_USER_START) {
            return -1;
        }
    }

    return 0;
}

static Flags ext_segment_flags_to_pte(UInt flags) {
    if (flags & USER_EXE_SEGMENT_EXEC) {
        return PE_KERNEL_CODE;
    }
    if (flags & USER_EXE_SEGMENT_WRITE) {
        return PE_KERNEL_DATA;
    }
    return PE_KERNEL_RO;
}

static Address ext_find_mapped_page(Task* task, Address page_va) {
    for (int index = 0; index < task->mm.user_pages_count; index++) {
        if (task->mm.user_pages[index].virt_addr == page_va) {
            return task->mm.user_pages[index].phys_addr;
        }
    }

    return 0;
}

static int ext_map_segment_range(Task* task, Address start_va, Address end_va, Flags flags) {
    for (Address page_va = start_va & MM_PAGE_MASK; page_va < end_va; page_va += PAGE_SIZE) {
        Address page;

        if (ext_find_mapped_page(task, page_va)) {
            continue;
        }

        page = mem_alloc_page();
        if (!page) {
            return -1;
        }
        memzero(mem_phys_to_virt((PhysAddr)page), PAGE_SIZE);
        if (process_map_page(task, page, page_va, flags) != 0) {
            mem_free_page(page);
            return -1;
        }
    }

    return 0;
}

static int ext_load_segment(Task* task, struct FileDesc* fd, const UserExeSegment* segment) {
    Address segment_start;
    Address segment_end;
    Address current_va;
    Address remaining;
    Flags page_flags;

    if (segment->memory_size == 0) {
        return 0;
    }
    if (segment->file_size > segment->memory_size) {
        return -1;
    }

    segment_start = segment->virtual_address;
    segment_end = segment->virtual_address + segment->memory_size;
    if (segment_end < segment_start) {
        return -1;
    }

    page_flags = ext_segment_flags_to_pte(segment->flags);
    if (ext_map_segment_range(task, segment_start, segment_end, page_flags) != 0) {
        return -1;
    }
    if (segment->file_size == 0) {
        return 0;
    }
    if (vfs_fd_seek(fd, segment->file_offset, SEEK_SET) < 0) {
        return -1;
    }

    current_va = segment->virtual_address;
    remaining = segment->file_size;
    while (remaining > 0) {
        Address page_va = current_va & MM_PAGE_MASK;
        Address page = ext_find_mapped_page(task, page_va);
        Address page_offset = current_va - page_va;
        Address chunk = PAGE_SIZE - page_offset;

        if (!page) {
            return -1;
        }
        if (chunk > remaining) {
            chunk = remaining;
        }
        if (ext_read_exact(fd, (void*)(mem_phys_to_virt((PhysAddr)page) + page_offset), chunk) != 0) {
            return -1;
        }

        current_va += chunk;
        remaining -= chunk;
    }

    return 0;
}

static int ext_validate_descriptor(const SystemExtHeader* header) {
    if (strncmp(header->magic, SYSTEM_EXT_MAGIC, 7) != 0) {
        return -1;
    }
    if (header->version != SYSTEM_EXT_VERSION) {
        return -1;
    }
    if (header->block_count == 0) {
        return -1;
    }
    if (header->function_count > SYSTEM_EXT_MAX_FUNCS) {
        return -1;
    }
    if (header->name[0] == '\0') {
        return -1;
    }
    return 0;
}

static int ext_install_api_page(Task* task) {
    Address page;
    SystemExtKernelApi* api;

    if (!task) {
        return -1;
    }
    if (ext_find_mapped_page(task, SYSTEM_EXT_API_VA)) {
        return 0;
    }

    page = mem_alloc_page();
    if (!page) {
        return -1;
    }
    memzero(mem_phys_to_virt((PhysAddr)page), PAGE_SIZE);
    if (process_map_page(task, page, SYSTEM_EXT_API_VA, PE_KERNEL_RO) != 0) {
        mem_free_page(page);
        return -1;
    }

    api = (SystemExtKernelApi*)mem_phys_to_virt((PhysAddr)page);
    api->version = SYSTEM_EXT_API_VERSION;
    api->tick_msec = SYSTEM_EXT_TICK_MSEC;
    api->console_write = system_ext_console_write;
    api->get_ticks = system_ext_get_ticks;
    api->get_mem_info = system_ext_get_mem_info;
    return 0;
}

static SystemExtension* ext_reserve_slot(const char* path) {
    for (UInt index = 0; index < SYSTEM_EXT_MAX; index++) {
        if (!system_extensions[index].used) {
            memzero((Address)&system_extensions[index], sizeof(system_extensions[index]));
            system_extensions[index].used = true;
            strncpy(system_extensions[index].path, path, sizeof(system_extensions[index].path) - 1);
            return &system_extensions[index];
        }
    }

    return null;
}

static SystemExtension* ext_find_by_name(const char* name) {
    for (UInt index = 0; index < SYSTEM_EXT_MAX; index++) {
        if (system_extensions[index].used && strncmp(system_extensions[index].name, name, sizeof(system_extensions[index].name)) == 0) {
            return &system_extensions[index];
        }
    }

    return null;
}

static Address ext_enter_context(SystemExtension* ext) {
    Address restore_pgd;

    preempt_disable();
    restore_pgd = current_task ? current_task->mm.pgd : 0;
    if (restore_pgd != ext->task.mm.pgd) {
        set_pgd(ext->task.mm.pgd);
    }
    return restore_pgd;
}

static void ext_leave_context(SystemExtension* ext, Address restore_pgd) {
    if (restore_pgd && restore_pgd != ext->task.mm.pgd) {
        set_pgd(restore_pgd);
    }
    preempt_enable();
}

static int ext_run_init(SystemExtension* ext) {
    void (*init_fn)(void);
    Address restore_pgd;

    if (!ext || !ext->init_address || !ext->task.mm.pgd) {
        return 0;
    }

    restore_pgd = ext_enter_context(ext);
    init_fn = (void (*)(void))ext->init_address;
    init_fn();
    ext_leave_context(ext, restore_pgd);
    return 0;
}

static void ext_run_idle_loop(SystemExtension* ext) {
    void (*loop_fn)(void);
    Address restore_pgd;

    if (!ext || !ext->loop_address || !ext->task.mm.pgd) {
        return;
    }

    restore_pgd = ext_enter_context(ext);
    if (!ext->loop_logged) {
        log_info("System extension idle loop is running: %s", ext->name);
        ext->loop_logged = true;
    }
    loop_fn = (void (*)(void))ext->loop_address;
    loop_fn();
    ext_leave_context(ext, restore_pgd);
}

void system_extensions_run_idle_loops(void) {
    unsigned long tick;

    if (task_cpu_index() != 0) {
        return;
    }

    tick = schedler_get_ticks();
    for (UInt index = 0; index < SYSTEM_EXT_MAX; index++) {
        SystemExtension* ext = &system_extensions[index];

        if (!ext->used || !ext->loop_address) {
            continue;
        }
        if (ext->loop_tick_valid && ext->last_idle_tick == tick) {
            continue;
        }

        ext->last_idle_tick = tick;
        ext->loop_tick_valid = true;
        ext_run_idle_loop(ext);
    }
}

int register_system_extension_from_path(const char* path) {
    struct FileDesc fd;
    UserExeHeader exe_header;
    SystemExtHeader ext_header;
    SystemExtFunctionRecord records[SYSTEM_EXT_MAX_FUNCS];
    SystemExtension* ext;
    UInt record_count;

    if (!path || path[0] == '\0') {
        return -1;
    }
    if (vfs_fd_open(&fd, path, O_READ) != SUCCESS) {
        return -1;
    }
    if (ext_read_exact(&fd, &exe_header, sizeof(exe_header)) != 0 || ext_validate_exe_header(&exe_header) != 0) {
        vfs_fd_close(&fd);
        return -1;
    }
    if (ext_read_exact(&fd, &ext_header, sizeof(ext_header)) != 0 || ext_validate_descriptor(&ext_header) != 0) {
        vfs_fd_close(&fd);
        return -1;
    }
    if (ext_find_by_name(ext_header.name)) {
        vfs_fd_close(&fd);
        return 0;
    }

    ext = ext_reserve_slot(path);
    if (!ext) {
        vfs_fd_close(&fd);
        return -1;
    }

    strncpy(ext->name, ext_header.name, sizeof(ext->name) - 1);
    ext->init_address = ext_header.init_address;
    ext->loop_address = ext_header.loop_address;
    ext->function_count = ext_header.function_count;
    ext->task.id = -1;
    ext->task.state = TASK_SLEEPING;
    ext->task.priority = PRIORITY_NORMAL;
    ext->task.cpu_affinity = 0;
    ext->task.flags = PF_KTHREAD;
    ext->task.mm.heap_next = USER_HEAP_BASE;
    ext->task.mm.dll_local_next = USER_SHARED_LIBRARY_LOCAL_BASE;
    ext->task.name = (Buffer)ext->name;

    record_count = ext_header.function_count;
    if (record_count > 0) {
        if (ext_read_exact(&fd, records, record_count * sizeof(SystemExtFunctionRecord)) != 0) {
            vfs_fd_close(&fd);
            ext->used = false;
            return -1;
        }
        for (UInt index = 0; index < record_count; index++) {
            strncpy(ext->functions[index].name, records[index].name, sizeof(ext->functions[index].name) - 1);
            ext->functions[index].address = records[index].address;
        }
    }

    for (UInt index = 0; index < exe_header.segment_count; index++) {
        if (ext_load_segment(&ext->task, &fd, &exe_header.segments[index]) != 0) {
            vfs_fd_close(&fd);
            ext->used = false;
            return -1;
        }
    }

    if (ext_install_api_page(&ext->task) != 0) {
        vfs_fd_close(&fd);
        ext->used = false;
        return -1;
    }

    vfs_fd_close(&fd);

    if (ext_run_init(ext) != 0) {
        ext->used = false;
        return -1;
    }

    log_info("Registered system extension %s from %s", ext->name, path);
    return 0;
}

long extension_call_by_name(const char* ext_name, const char* func_name, unsigned long a, unsigned long b) {
    SystemExtension* ext;
    long (*fn)(unsigned long, unsigned long);
    Address restore_pgd;

    ext = ext_find_by_name(ext_name);
    if (!ext || !ext->task.mm.pgd) {
        return -1;
    }
    for (UInt index = 0; index < ext->function_count; index++) {
        if (strncmp(ext->functions[index].name, func_name, sizeof(ext->functions[index].name)) != 0) {
            continue;
        }

        restore_pgd = ext_enter_context(ext);
        fn = (long (*)(unsigned long, unsigned long))ext->functions[index].address;
        long result = fn(a, b);
        ext_leave_context(ext, restore_pgd);
        return result;
    }

    return -1;
}
