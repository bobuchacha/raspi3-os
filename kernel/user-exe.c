#define LOG_ENABLE_TRACE 0

#include "user-exe.h"
#include "arch/cortex-a53/mmu.h"
#include "filesystem/vfs/vfs.h"
#include "log.h"
#include "memory.h"
#include "ros.h"
#include "task.h"
#include "utils.h"

/**
 * Map a declaration from the MMU subsystem without pulling in another header layer here.
 *
 * Args:
 *   task: Task whose user address space is being updated.
 *   pa: Physical page base to map.
 *   va: User virtual address where the page should appear.
 *   flags: Final page-table attributes for the mapping.
 *
 * Returns:
 *   Nothing. The task page tables are updated in place.
 */
int process_map_page(Task* task, Address pa, Address va, Flags flags);
int process_map_shared_page(Task* task, Address pa, Address va, Flags flags);
int process_unmap_page(Task* task, Address va);
typedef struct SharedLibrary SharedLibrary;
static Address shared_library_find_page(const SharedLibrary* library, Address page_va);
static int shared_library_update_page_flags(SharedLibrary* library, Address page_va, Flags map_flags);
static int shared_library_track_page(SharedLibrary* library, Address phys_addr, Address virt_addr, Flags map_flags);

typedef struct {
    Address phys_addr;
    Address virt_addr;
    Flags map_flags;
} SharedLibraryPage;

typedef struct {
    char name[USER_SHARED_LIBRARY_EXPORT_NAME_MAX];
    Address address;
} SharedLibraryExport;

typedef struct SharedLibrary {
    Bool used;
    char path[USER_SHARED_LIBRARY_PATH_MAX];
    Address entry_point;
    Address base_va;
    Address load_bias;
    ULong image_size;
    UInt page_count;
    SharedLibraryPage pages[USER_SHARED_LIBRARY_MAX_PAGES];
    UInt export_count;
    SharedLibraryExport exports[USER_SHARED_LIBRARY_MAX_EXPORTS];
} SharedLibrary;

static SharedLibrary shared_libraries[USER_SHARED_LIBRARY_MAX_LOADED];
static Address shared_library_next_va = USER_SHARED_LIBRARY_BASE;

#define USER_DLL_MAGIC_0 'R'
#define USER_DLL_MAGIC_1 'O'
#define USER_DLL_MAGIC_2 'S'
#define USER_DLL_MAGIC_3 'D'
#define USER_DLL_MAGIC_4 'L'
#define USER_DLL_MAGIC_5 'L'
#define USER_DLL_MAGIC_6 '\0'
#define USER_DLL_MAGIC_7 '\0'
#define USER_DLL_MACHINE_AARCH64 183U
#define USER_DLL_MAX_SECTIONS 16U

typedef struct {
    char path[USER_SHARED_LIBRARY_PATH_MAX];
    Address virt_addr;
    ULong size;
    ULong page_count;
} TaskSharedLibraryLocal;

/**
 * Copy a small user-visible task name into the task descriptor.
 *
 * Args:
 *   task: Task receiving the name.
 *   name: Caller-supplied name string.
 *
 * Returns:
 *   Nothing. Empty or null names fall back to `user`.
 */
static void task_set_user_name(Task* task, const char* name) {
    const char* source = (name && name[0] != '\0') ? name : "user";
    _trace("Setting task %d name to '%s'", task->id, source);
    if (task->process) {
        strncpy(task->process->name, source, sizeof(task->process->name) - 1);
        task->process->name[sizeof(task->process->name) - 1] = '\0';
        task->name = (Buffer)task->process->name;
        task->thread_name[0] = '\0';
    }
    else {
        strncpy(task->thread_name, source, sizeof(task->thread_name) - 1);
        task->thread_name[sizeof(task->thread_name) - 1] = '\0';
        task->name = (Buffer)task->thread_name;
    }
}

/**
 * Read an exact number of bytes from an open executable file.
 *
 * Args:
 *   fd: Open VFS file descriptor.
 *   buf: Destination buffer.
 *   size: Number of bytes required.
 *
 * Returns:
 *   `0` when the requested byte count is read, or `-1` on short read/error.
 */
static int exec_read_exact(struct FileDesc* fd, void* buf, unsigned int size) {
    // Pull exactly size bytes from the open file; expects the descriptor to be valid and advances the file cursor on success.
    return vfs_fd_read(fd, buf, size) == (int)size ? 0 : -1;
}

static ULong exec_align_up(ULong value, ULong align) {
    if (align == 0) {
        return value;
    }
    return (value + align - 1U) & ~(align - 1U);
}

/**
 * Read one cache-line size from CTR_EL0 and convert it to bytes.
 *
 * Args:
 *   shift: Bit position of the cache-line field inside CTR_EL0.
 *
 * Returns:
 *   Cache line size in bytes, or `0` if the field reports zero.
 */
static ULong exec_cache_line_bytes(unsigned int shift) {
    ULong ctr_el0;
    ULong line_words;

    asm volatile("mrs %0, ctr_el0" : "=r"(ctr_el0));
    line_words = (ctr_el0 >> shift) & 0xFUL;
    return 4UL << line_words;
}

static Bool exec_is_power_of_two(ULong value) {
    return value != 0 && (value & (value - 1U)) == 0;
}

static Bool exec_ranges_overlap(Address start_a, Address end_a, Address start_b, Address end_b) {
    return start_a < end_b && start_b < end_a;
}

static Bool exec_segment_contains_entry(const UserExeSegment* segment, Address entry_point) {
    if (!segment || segment->memory_size == 0) {
        return false;
    }
    if (!(segment->flags & USER_EXE_SEGMENT_EXEC)) {
        return false;
    }

    return entry_point >= segment->virtual_address &&
        entry_point < segment->virtual_address + segment->memory_size;
}

static int exec_clamp_page_count(int count) {
    if (count < 0) {
        return 0;
    }
    if (count > MAX_PROCESS_PAGES) {
        return MAX_PROCESS_PAGES;
    }
    return count;
}

static Address exec_normalize_page(Address page) {
    Address normalized = (Address)(page & MM_PAGE_MASK);

    if (normalized == 0) {
        return 0;
    }
    if (mem_is_kernel_virt_addr((VirtAddr)normalized)) {
        PhysAddr phys = mem_virt_to_phys((VirtAddr)normalized);

        if (mem_is_valid_phys_page(phys)) {
            return (Address)phys;
        }
    }
    if (!mem_is_valid_phys_page((PhysAddr)normalized)) {
        return 0;
    }

    return normalized;
}

/*
 * Initialize a temporary task image used to stage a new executable before it
 * replaces the live task mappings.
 */
static void exec_init_staged_task(Task* staged_task, const Task* live_task) {
    Address task_page = 0;
    Address stack_page = 0;

    memzero((Address)staged_task, sizeof(*staged_task));
    if (!live_task) {
        return;
    }

    staged_task->id = live_task->id;
    staged_task->kernel_stack_page = live_task->kernel_stack_page;
    staged_task->mm.heap_next = USER_HEAP_BASE;
    staged_task->mm.dll_local_next = USER_SHARED_LIBRARY_LOCAL_BASE;

    if (live_task->mm.kernel_pages_count > 0) {
        task_page = (Address)(live_task->mm.kernel_pages[0] & MM_PAGE_MASK);
        if (task_page != 0) {
            staged_task->mm.kernel_pages[staged_task->mm.kernel_pages_count++] = task_page;
        }
    }

    stack_page = (Address)(live_task->kernel_stack_page & MM_PAGE_MASK);
    if (stack_page != 0 && (staged_task->mm.kernel_pages_count == 0 || staged_task->mm.kernel_pages[staged_task->mm.kernel_pages_count - 1] != stack_page)) {
        staged_task->mm.kernel_pages[staged_task->mm.kernel_pages_count++] = stack_page;
    }
}

/*
 * Release one staged task memory image through the existing cleanup path
 * without mutating the live task descriptor.
 */
static void exec_release_task_image(const Task* live_task, const TaskMemory* mm, Address kernel_stack_page) {
    Address task_page;
    Address stack_page;
    int user_page_count;
    int kernel_page_count;

    if (!live_task || !mm) {
        return;
    }

    task_page = exec_normalize_page(mm->kernel_pages_count > 0 ? mm->kernel_pages[0] : 0);
    stack_page = exec_normalize_page(kernel_stack_page);
    user_page_count = exec_clamp_page_count(mm->user_pages_count);
    kernel_page_count = exec_clamp_page_count(mm->kernel_pages_count);

    // Release staged or replaced user pages tracked by the temporary image.
    for (int index = 0; index < user_page_count; index++) {
        Address page = exec_normalize_page(mm->user_pages[index].phys_addr);

        if (page != 0) {
            mem_free_page(page);                                 // drop one tracked user-page reference
        }
    }

    // Release temporary page-table pages while keeping the live task and stack pages.
    for (int index = 0; index < kernel_page_count; index++) {
        Address page = exec_normalize_page(mm->kernel_pages[index]);

        if (page == 0 || page == task_page || page == stack_page) {
            continue;
        }
        mem_free_page(page);                                     // release one transient page-table page
    }
}

static Bool shared_library_has_flat_magic(const char magic[8]) {
    return magic[0] == USER_DLL_MAGIC_0 &&
        magic[1] == USER_DLL_MAGIC_1 &&
        magic[2] == USER_DLL_MAGIC_2 &&
        magic[3] == USER_DLL_MAGIC_3 &&
        magic[4] == USER_DLL_MAGIC_4 &&
        magic[5] == USER_DLL_MAGIC_5 &&
        magic[6] == USER_DLL_MAGIC_6 &&
        magic[7] == USER_DLL_MAGIC_7;
}

static Bool shared_library_has_legacy_magic(const char magic[8]) {
    return strncmp(magic, USER_EXE_MAGIC, 8) == 0;
}

/**
 * Translate executable segment permissions into user page-table flags.
 *
 * Args:
 *   flags: Packed executable segment permission bits.
 *
 * Returns:
 *   The MMU flags that should be used when mapping the segment.
 */
static Flags exe_segment_flags_to_pte(UInt flags) {
    if (flags & USER_EXE_SEGMENT_EXEC) {
        return PE_USER_CODE;
    }
    if (flags & USER_EXE_SEGMENT_WRITE) {
        return PE_USER_DATA;
    }
    return PE_USER_RO;
}

/**
 * Find the backing physical page already mapped at a user virtual page address.
 *
 * Args:
 *   task: Task whose tracked user pages are inspected.
 *   page_va: Page-aligned user virtual address.
 *
 * Returns:
 *   Physical page base when mapped, or `0` when the page is absent.
 */
static Address exec_find_mapped_page(Task* task, Address page_va) {
    for (int index = 0; index < task->mm.user_pages_count; index++) {
        if (task->mm.user_pages[index].virt_addr == page_va) {
            return task->mm.user_pages[index].phys_addr;
        }
    }

    return 0;
}

/**
 * Ensure every page touched by a segment exists in the current task address space.
 *
 * Args:
 *   task: Task receiving the executable image.
 *   start_va: First virtual address covered by the segment.
 *   end_va: One-past-the-end virtual address for the segment.
 *   flags: Page-table attributes for each mapped page.
 *
 * Returns:
 *   `0` on success, or `-1` when a backing page cannot be allocated.
 */
static int exec_map_segment_range(Task* task, Address start_va, Address end_va, Flags flags) {
    // Walk every touched user page so the segment has backing memory across its full virtual range.
    for (Address page_va = start_va & MM_PAGE_MASK; page_va < end_va; page_va += PAGE_SIZE) {
        // Skip pages that are already tracked for this task; expects user page metadata to reflect current mappings.
        if (exec_find_mapped_page(task, page_va)) {
            continue;
        }

        // Allocate one physical page for this virtual slot; affects the global page allocator state.
        Address page = mem_alloc_page();
        if (!page) {
            return -1;
        }

        // Insert the new user mapping into the task page tables; affects the task address space contents immediately.
        if (process_map_page(task, page, page_va, flags) != 0) {
            mem_free_page(page);                                 // return the page if task mapping/bookkeeping fails
            return -1;
        }
    }

    return 0;
}

static void exec_trace_loaded_segment(Task* task, const char* path, UInt segment_index, const UserExeSegment* segment) {
    Address segment_start;
    Address segment_end;

    if (!task || !segment || segment->memory_size == 0) {
        return;
    }

    segment_start = segment->virtual_address;
    segment_end = segment->virtual_address + segment->memory_size;
    if (segment_end < segment_start) {
        return;
    }

    for (Address page_va = segment_start & MM_PAGE_MASK; page_va < segment_end; page_va += PAGE_SIZE) {
        Address page_pa = exec_find_mapped_page(task, page_va);

        if (!page_pa) {
            continue;
        }

        _trace("Executable %s segment %u page PA \x1b[32m0x%lX\x1b[0m => VA \x1b[33m0x%lX\x1b[0m",
            path,
            segment_index,
            page_pa,
            page_va);
    }
}

static void exec_trace_entry_point(Task* task, const char* path, Address entry_point) {
    MmuWalkResult walk;

    if (!task || !path) {
        return;
    }
    if (process_walk_page(task, entry_point, &walk) != 0) {
        _trace("Executable %s entry VA \x1b[33m0x%lX\x1b[0m is not mapped", path, entry_point);
        return;
    }

    _trace("Executable %s entry PA \x1b[32m0x%lX\x1b[0m => VA \x1b[33m0x%lX\x1b[0m",
        path,
        walk.phys_addr + (entry_point & (PAGE_SIZE - 1)),
        entry_point);
}

/**
 * Synchronize the instruction cache for one executable segment.
 *
 * Args:
 *   task: Task that owns the freshly loaded segment pages.
 *   segment: Executable segment that was just copied into memory.
 *
 * Returns:
 *   Nothing. Missing mappings are ignored because segment loading already
 *   validated them.
 */
static void exec_sync_segment_icache(Task* task, const UserExeSegment* segment) {
    ULong dcache_line;
    ULong icache_line;
    Address segment_start;
    Address segment_end;

    if (!task || !segment || !(segment->flags & USER_EXE_SEGMENT_EXEC) || segment->memory_size == 0) {
        return;
    }

    segment_start = segment->virtual_address;
    segment_end = segment->virtual_address + segment->memory_size;
    if (segment_end <= segment_start) {
        return;
    }

    dcache_line = exec_cache_line_bytes(16);
    icache_line = exec_cache_line_bytes(0);
    if (dcache_line == 0) {
        dcache_line = 64;
    }
    if (icache_line == 0) {
        icache_line = 64;
    }

    // Push all executable bytes out through the kernel alias before EL0 fetches them.
    for (Address page_va = segment_start & MM_PAGE_MASK; page_va < segment_end; page_va += PAGE_SIZE) {
        Address page_pa = exec_find_mapped_page(task, page_va);
        Address copy_start;
        Address copy_end;

        if (!page_pa) {
            continue;
        }

        copy_start = page_pa + VA_START;
        if (page_va == (segment_start & MM_PAGE_MASK)) {
            copy_start += segment_start - page_va;
        }

        copy_end = (page_pa + VA_START) + PAGE_SIZE;
        if (page_va + PAGE_SIZE > segment_end) {
            copy_end = (page_pa + VA_START) + (segment_end - page_va);
        }

        for (Address va = copy_start & ~(dcache_line - 1UL); va < copy_end; va += dcache_line) {
            asm volatile("dc cvau, %0" :: "r"(va) : "memory");   // clean freshly written code to PoU
        }
    }
    asm volatile("dsb ish" ::: "memory");

    // Invalidate instruction-cache lines that overlap the executable bytes.
    for (Address page_va = segment_start & MM_PAGE_MASK; page_va < segment_end; page_va += PAGE_SIZE) {
        Address page_pa = exec_find_mapped_page(task, page_va);
        Address copy_start;
        Address copy_end;

        if (!page_pa) {
            continue;
        }

        copy_start = page_pa + VA_START;
        if (page_va == (segment_start & MM_PAGE_MASK)) {
            copy_start += segment_start - page_va;
        }

        copy_end = (page_pa + VA_START) + PAGE_SIZE;
        if (page_va + PAGE_SIZE > segment_end) {
            copy_end = (page_pa + VA_START) + (segment_end - page_va);
        }

        for (Address va = copy_start & ~(icache_line - 1UL); va < copy_end; va += icache_line) {
            asm volatile("ic ivau, %0" :: "r"(va) : "memory");   // invalidate old instructions for this range
        }
    }
    asm volatile(
        "dsb ish\n"
        "isb\n"
        ::: "memory");
}

/**
 * Populate one executable segment into already reset user memory.
 *
 * Args:
 *   task: Task receiving the program image.
 *   fd: Open executable file descriptor.
 *   segment: Segment metadata from the 512-byte executable header.
 *
 * Returns:
 *   `0` when the segment is mapped and loaded, or `-1` on validation or I/O failure.
 */
static int exec_load_segment(Task* task, struct FileDesc* fd, const UserExeSegment* segment) {
    if (segment->memory_size == 0) {
        return 0;
    }
    if (segment->file_size > segment->memory_size) {
        return -1;
    }

    // show tracing
    _trace("Loading segment: file offset 0x%lX, VA 0x%lX, file size 0x%lX, mem size 0x%lX, flags 0x%X",
        segment->file_offset, segment->virtual_address, segment->file_size, segment->memory_size, segment->flags);

    Address segment_start = segment->virtual_address;
    Address segment_end = segment->virtual_address + segment->memory_size;
    // Translate packed segment permissions into MMU attributes; expects loader flags to match the paging policy.
    Flags page_flags = exe_segment_flags_to_pte(segment->flags);

    if (segment_end < segment_start) {
        return -1;
    }
    // Materialize every page the segment can touch; affects task mappings before file bytes are copied.
    if (exec_map_segment_range(task, segment_start, segment_end, page_flags) != 0) {
        return -1;
    }
    if (segment->file_size == 0) {
        return 0;
    }
    // Reposition the descriptor at this segment's file payload; expects a valid packed offset and affects later reads.
    if (vfs_fd_seek(fd, segment->file_offset, SEEK_SET) < 0) {
        return -1;
    }

    Address current_va = segment->virtual_address;
    Address remaining = segment->file_size;
    // Stream the file-backed portion into the already mapped pages so BSS stays implicitly zero-filled.
    while (remaining > 0) {
        Address page_va = current_va & MM_PAGE_MASK;
        // Resolve which backing page owns the current virtual address; expects mapping setup to have succeeded earlier.
        Address page = exec_find_mapped_page(task, page_va);
        Address page_offset = current_va - page_va;
        Address chunk = PAGE_SIZE - page_offset;

        if (!page) {
            return -1;
        }
        if (chunk > remaining) {
            chunk = remaining;
        }
        // Copy the next chunk of file bytes into the kernel alias of the user page; affects the future EL0 image contents.
        if (exec_read_exact(fd, (void*)(page + VA_START + page_offset), chunk) != 0) {
            return -1;
        }

        current_va += chunk;
        remaining -= chunk;
    }

    return 0;
}

/**
 * Validate the fixed executable header before resetting the current task image.
 *
 * Args:
 *   header: Parsed executable header from the file start.
 *
 * Returns:
 *   `0` when the header is structurally valid, or `-1` when any field is inconsistent.
 */
static int exec_validate_header(const UserExeHeader* header, ULong file_size) {
    Bool entry_covered = false;

    // Verify the packed header signature first; expects the loader format magic and rejects unrelated files.
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
    if (header->image_size == 0) {
        return -1;
    }
    if (header->entry_point < header->image_base || header->entry_point >= header->image_base + header->image_size) {
        return -1;
    }

    for (UInt index = 0; index < header->segment_count; index++) {
        const UserExeSegment* segment = &header->segments[index];
        Address segment_start = segment->virtual_address;
        Address segment_end = segment->virtual_address + segment->memory_size;

        if (segment->file_size > segment->memory_size) {
            return -1;
        }
        if (segment->memory_size != 0 && segment_end < segment_start) {
            return -1;
        }
        if (segment->file_size != 0) {
            if (segment->file_offset < header->header_size || segment->file_offset + segment->file_size < segment->file_offset) {
                return -1;
            }
            if (segment->file_offset + segment->file_size > file_size) {
                return -1;
            }
        }
        if (segment->memory_size == 0) {
            continue;
        }
        if (segment->virtual_address < VA_USER_START) {
            return -1;
        }
        if (segment->alignment != 0 && (!exec_is_power_of_two(segment->alignment) || (segment->virtual_address & (segment->alignment - 1U)) != 0)) {
            return -1;
        }
        if (segment_start < header->image_base || segment_end > header->image_base + header->image_size) {
            return -1;
        }
        if ((segment->flags & (USER_EXE_SEGMENT_READ | USER_EXE_SEGMENT_WRITE | USER_EXE_SEGMENT_EXEC)) == 0) {
            return -1;
        }
        if (exec_segment_contains_entry(segment, header->entry_point)) {
            entry_covered = true;
        }
    }

    for (UInt left = 0; left < header->segment_count; left++) {
        const UserExeSegment* a = &header->segments[left];

        if (a->memory_size == 0) {
            continue;
        }

        for (UInt right = left + 1; right < header->segment_count; right++) {
            const UserExeSegment* b = &header->segments[right];

            if (b->memory_size == 0) {
                continue;
            }
            if (exec_ranges_overlap(a->virtual_address, a->virtual_address + a->memory_size, b->virtual_address, b->virtual_address + b->memory_size)) {
                return -1;
            }
        }
    }

    if (!entry_covered) {
        return -1;
    }

    return 0;
}

static const UserDllSection* dll_sections(const UserDllHeader* header) {
    return (const UserDllSection*)(((const UByte*)header) + sizeof(UserDllHeader));
}

static const UserDllReloc* dll_relocs(const UserDllHeader* header) {
    return (const UserDllReloc*)(((const UByte*)dll_sections(header)) + ((ULong)header->section_count * sizeof(UserDllSection)));
}

static const UserDllExport* dll_exports(const UserDllHeader* header) {
    return (const UserDllExport*)(((const UByte*)dll_relocs(header)) + ((ULong)header->reloc_count * sizeof(UserDllReloc)));
}

static int dll_validate_image(const UByte* image, ULong image_size, const UserDllHeader** out_header) {
    const UserDllHeader* header;
    const UserDllSection* sections;
    const UserDllReloc* relocs;
    const UserDllExport* exports;
    ULong cursor;

    if (!image || image_size < sizeof(UserDllHeader)) {
        return -1;
    }

    header = (const UserDllHeader*)image;
    if (!shared_library_has_flat_magic(header->magic)) {
        return -1;
    }
    if (header->version != USER_DLL_VERSION || header->machine != USER_DLL_MACHINE_AARCH64) {
        return -1;
    }
    if (header->header_size < sizeof(UserDllHeader) || header->header_size > image_size) {
        return -1;
    }
    if (header->section_count == 0 || header->section_count > USER_DLL_MAX_SECTIONS) {
        return -1;
    }
    if (header->export_count > USER_SHARED_LIBRARY_MAX_EXPORTS) {
        return -1;
    }
    if (header->entry_section >= header->section_count) {
        return -1;
    }
    if (!exec_is_power_of_two(header->align) || header->align < PAGE_SIZE) {
        return -1;
    }
    if (header->image_size == 0 || !exec_is_power_of_two(PAGE_SIZE) || (header->image_size & (PAGE_SIZE - 1U)) != 0) {
        return -1;
    }

    cursor = sizeof(UserDllHeader);
    if (cursor + ((ULong)header->section_count * sizeof(UserDllSection)) > header->header_size) {
        return -1;
    }
    sections = (const UserDllSection*)(image + cursor);
    cursor += (ULong)header->section_count * sizeof(UserDllSection);

    if (cursor + ((ULong)header->reloc_count * sizeof(UserDllReloc)) > header->header_size) {
        return -1;
    }
    relocs = (const UserDllReloc*)(image + cursor);
    cursor += (ULong)header->reloc_count * sizeof(UserDllReloc);

    if (cursor + ((ULong)header->export_count * sizeof(UserDllExport)) > header->header_size) {
        return -1;
    }
    exports = (const UserDllExport*)(image + cursor);

    for (UInt index = 0; index < header->section_count; index++) {
        const UserDllSection* section = &sections[index];

        if (section->type > USER_DLL_SEC_DATA) {
            return -1;
        }
        if (section->align == 0) {
            return -1;
        }
        if (section->runtime_offset + section->mem_size < section->runtime_offset) {
            return -1;
        }
        if (section->runtime_offset + section->mem_size > header->image_size) {
            return -1;
        }
        if (section->file_size > section->mem_size) {
            return -1;
        }
        if (section->file_offset < header->header_size) {
            return -1;
        }
        if (section->file_offset + section->file_size > image_size) {
            return -1;
        }
    }

    for (UInt index = 0; index < header->reloc_count; index++) {
        const UserDllReloc* reloc = &relocs[index];

        if (reloc->type != USER_DLL_RELOC_ABS64 && reloc->type != USER_DLL_RELOC_REL64 && reloc->type != USER_DLL_RELOC_SECTION) {
            return -1;
        }
        if (reloc->section >= header->section_count) {
            return -1;
        }
        if (reloc->offset + sizeof(ULong) > header->image_size) {
            return -1;
        }
    }

    for (UInt index = 0; index < header->export_count; index++) {
        const UserDllExport* export_entry = &exports[index];

        if (export_entry->name[0] == '\0') {
            return -1;
        }
        if (export_entry->section >= header->section_count) {
            return -1;
        }
        if (export_entry->offset >= sections[export_entry->section].mem_size) {
            return -1;
        }
    }

    if (out_header) {
        *out_header = header;
    }
    return 0;
}

static int dll_compute_runtime_offsets(const UserDllHeader* header, ULong* section_offsets) {
    const UserDllSection* sections = dll_sections(header);
    ULong image_end = 0;

    if (!section_offsets) {
        return -1;
    }

    for (UInt index = 0; index < header->section_count; index++) {
        section_offsets[index] = sections[index].runtime_offset;
        if (sections[index].runtime_offset + sections[index].mem_size > image_end) {
            image_end = sections[index].runtime_offset + sections[index].mem_size;
        }
    }

    image_end = exec_align_up(image_end, header->align);
    return image_end == header->image_size ? 0 : -1;
}

static Address shared_library_reserve_image(ULong size, ULong align) {
    Address base = exec_align_up(shared_library_next_va, align ? align : PAGE_SIZE);
    Address end = base + size;

    if (end < base || end > USER_SHARED_LIBRARY_LIMIT) {
        return 0;
    }

    shared_library_next_va = end;
    return base;
}

static Address shared_library_section_runtime_va(const SharedLibrary* library, const UserDllHeader* header, const ULong* section_offsets, UInt section, ULong offset) {
    const UserDllSection* sections = dll_sections(header);

    if (!library || !header || !section_offsets || section >= header->section_count) {
        return 0;
    }
    if (offset >= sections[section].mem_size) {
        return 0;
    }
    return library->base_va + section_offsets[section] + offset;
}

static int shared_library_write_bytes(const SharedLibrary* library, Address start_va, const UByte* source, ULong size) {
    Address current_va = start_va;
    ULong remaining = size;

    while (remaining > 0) {
        Address page_va = current_va & MM_PAGE_MASK;
        Address page = shared_library_find_page(library, page_va);
        Address page_offset = current_va - page_va;
        Address chunk = PAGE_SIZE - page_offset;

        if (!page) {
            return -1;
        }
        if (chunk > remaining) {
            chunk = remaining;
        }

        memcpy(page + VA_START + page_offset, (Address)source, (int)chunk);
        source += chunk;
        current_va += chunk;
        remaining -= chunk;
    }

    return 0;
}

static int shared_library_patch_u64(const SharedLibrary* library, Address target_va, ULong value) {
    UByte raw[sizeof(ULong)];

    for (UInt index = 0; index < sizeof(raw); index++) {
        raw[index] = (UByte)((value >> (index * 8U)) & 0xFFU);
    }
    return shared_library_write_bytes(library, target_va, raw, sizeof(raw));
}

static int shared_library_copy_bytes(const SharedLibrary* library, Address start_va, const UByte* source, ULong size) {
    return shared_library_write_bytes(library, start_va, source, size);
}

static int shared_library_apply_relocs(SharedLibrary* library, const UserDllHeader* header, const ULong* section_offsets) {
    const UserDllReloc* relocs = dll_relocs(header);

    for (UInt index = 0; index < header->reloc_count; index++) {
        const UserDllReloc* entry = &relocs[index];
        Address patch_va = library->base_va + entry->offset;
        Address target_va = shared_library_section_runtime_va(library, header, section_offsets, entry->section, entry->addend);
        ULong patch_value;

        if (!target_va) {
            return -1;
        }

        if (entry->type == USER_DLL_RELOC_SECTION || entry->type == USER_DLL_RELOC_ABS64) {
            patch_value = target_va;
        }
        else if (entry->type == USER_DLL_RELOC_REL64) {
            patch_value = (ULong)((Long)target_va - (Long)patch_va);
        }
        else {
            return -1;
        }

        if (shared_library_patch_u64(library, patch_va, patch_value) != 0) {
            return -1;
        }
    }

    return 0;
}

static int shared_library_resolve_exports(SharedLibrary* library, const UserDllHeader* header, const ULong* section_offsets) {
    const UserDllExport* exports = dll_exports(header);

    if (!library || !header || !section_offsets) {
        return -1;
    }

    library->export_count = 0;
    for (UInt index = 0; index < header->export_count; index++) {
        const UserDllExport* export_entry = &exports[index];
        Address export_va = shared_library_section_runtime_va(library, header, section_offsets, export_entry->section, export_entry->offset);

        if (!export_va) {
            return -1;
        }

        memzero((Address)&library->exports[index], sizeof(library->exports[index]));
        strncpy(library->exports[index].name, export_entry->name, sizeof(library->exports[index].name) - 1);
        library->exports[index].name[sizeof(library->exports[index].name) - 1] = '\0';
        library->exports[index].address = export_va;
        library->export_count++;
    }

    return 0;
}

static int shared_library_load_flat_section(SharedLibrary* library, const UByte* image, const UserDllSection* section) {
    Address start_va;
    Address end_va;
    Flags page_flags;

    if (!library || !image || !section || section->mem_size == 0) {
        return 0;
    }

    start_va = library->base_va + section->runtime_offset;
    end_va = start_va + section->mem_size;
    page_flags = exe_segment_flags_to_pte(section->flags);

    if (start_va < USER_SHARED_LIBRARY_BASE || end_va < start_va || end_va > USER_SHARED_LIBRARY_LIMIT) {
        return -1;
    }

    for (Address page_va = start_va & MM_PAGE_MASK; page_va < end_va; page_va += PAGE_SIZE) {
        if (shared_library_find_page(library, page_va)) {
            if (shared_library_update_page_flags(library, page_va, page_flags) != 0) {
                return -1;
            }
            continue;
        }

        Address page = mem_alloc_page();
        if (!page) {
            return -1;
        }

        memzero(page + VA_START, PAGE_SIZE);
        if (shared_library_track_page(library, page, page_va, page_flags) != 0) {
            mem_free_page(page);
            return -1;
        }
    }

    if (section->file_size != 0 && shared_library_copy_bytes(library, start_va, image + section->file_offset, section->file_size) != 0) {
        return -1;
    }

    return 0;
}

static int shared_library_load_flat_image(SharedLibrary* library, const UByte* image, ULong image_size) {
    const UserDllHeader* header = null;
    const UserDllSection* sections;
    ULong section_offsets[USER_DLL_MAX_SECTIONS];
    Address entry_va;

    if (dll_validate_image(image, image_size, &header) != 0) {
        return -1;
    }
    if (dll_compute_runtime_offsets(header, section_offsets) != 0) {
        return -1;
    }

    library->base_va = shared_library_reserve_image(header->image_size, header->align);
    if (!library->base_va) {
        return -1;
    }
    library->load_bias = library->base_va;
    library->image_size = header->image_size;

    sections = dll_sections(header);
    for (UInt index = 0; index < header->section_count; index++) {
        if (shared_library_load_flat_section(library, image, &sections[index]) != 0) {
            return -1;
        }
    }
    if (shared_library_apply_relocs(library, header, section_offsets) != 0) {
        return -1;
    }

    if (shared_library_resolve_exports(library, header, section_offsets) != 0) {
        return -1;
    }

    entry_va = shared_library_section_runtime_va(library, header, section_offsets, header->entry_section, header->entry_offset);
    if (!entry_va) {
        return -1;
    }
    library->entry_point = entry_va;
    return 0;
}

/**
 * Find a per-task DLL-local storage record by shared-library path.
 *
 * Args:
 *   task: Task whose DLL-local table should be searched.
 *   path: Absolute shared-library path.
 *
 * Returns:
 *   Matching record on success, or `null` when the task has not allocated one yet.
 */
static TaskSharedLibraryLocal* shared_library_find_task_local(Task* task, const char* path) {
    for (UInt index = 0; index < USER_SHARED_LIBRARY_MAX_TASK_LOCALS; index++) {
        if (task->mm.dll_locals[index].virt_addr == 0) {
            continue;
        }
        if (strncmp(task->mm.dll_locals[index].path, path, USER_SHARED_LIBRARY_PATH_MAX) == 0) {
            return (TaskSharedLibraryLocal*)&task->mm.dll_locals[index];
        }
    }

    return null;
}

/**
 * Find a free per-task DLL-local bookkeeping slot.
 *
 * Args:
 *   task: Task whose DLL-local table should be searched.
 *
 * Returns:
 *   Empty record on success, or `null` when the task-local DLL table is full.
 */
static TaskSharedLibraryLocal* shared_library_reserve_task_local(Task* task) {
    for (UInt index = 0; index < USER_SHARED_LIBRARY_MAX_TASK_LOCALS; index++) {
        if (task->mm.dll_locals[index].virt_addr != 0) {
            continue;
        }

        memzero((Address)&task->mm.dll_locals[index], sizeof(task->mm.dll_locals[index]));
        return (TaskSharedLibraryLocal*)&task->mm.dll_locals[index];
    }

    return null;
}

/**
 * Roll back partially created DLL-local pages for the current task.
 *
 * Args:
 *   task: Task whose mappings should be unwound.
 *   base: Base virtual address of the partially created range.
 *   page_count: Number of pages that should be removed.
 *
 * Returns:
 *   Nothing. Missing pages are ignored.
 */
static void shared_library_release_task_local_pages(Task* task, Address base, ULong page_count) {
    for (ULong index = 0; index < page_count; index++) {
        process_unmap_page(task, base + (index * PAGE_SIZE));
    }
}

/**
 * Find a cached shared library by its VFS path.
 *
 * Args:
 *   path: Absolute VFS path used when the library was first loaded.
 *
 * Returns:
 *   Pointer to the cached library entry, or `null` when it has not been loaded yet.
 */
static SharedLibrary* shared_library_find(const char* path) {
    for (UInt index = 0; index < USER_SHARED_LIBRARY_MAX_LOADED; index++) {
        if (!shared_libraries[index].used) {
            continue;
        }
        // Match the requested VFS path against a populated cache slot; affects whether the library is reused or reloaded.
        if (strncmp(shared_libraries[index].path, path, USER_SHARED_LIBRARY_PATH_MAX) == 0) {
            return &shared_libraries[index];
        }
    }

    return null;
}

/**
 * Reserve one global cache slot for a newly loaded shared library.
 *
 * Args:
 *   path: Absolute VFS path that identifies the library.
 *
 * Returns:
 *   Empty cache slot on success, or `null` when the global library cache is full.
 */
static SharedLibrary* shared_library_reserve(const char* path) {
    for (UInt index = 0; index < USER_SHARED_LIBRARY_MAX_LOADED; index++) {
        if (shared_libraries[index].used) {
            continue;
        }

        // Clear stale metadata in the chosen slot; affects the global library cache entry before it becomes visible.
        memzero((Address)&shared_libraries[index], sizeof(shared_libraries[index]));
        shared_libraries[index].used = true;
        // Persist the identifying path for later cache lookups; expects a null-terminated absolute path.
        strncpy(shared_libraries[index].path, path, USER_SHARED_LIBRARY_PATH_MAX - 1);
        shared_libraries[index].path[USER_SHARED_LIBRARY_PATH_MAX - 1] = '\0';
        return &shared_libraries[index];
    }

    return null;
}

UInt user_shared_library_snapshot(UserSharedLibraryInfo* infos, UInt max_infos) {
    UInt count = 0;

    for (UInt index = 0; index < USER_SHARED_LIBRARY_MAX_LOADED; index++) {
        const SharedLibrary* library = &shared_libraries[index];

        if (!library->used) {
            continue;
        }

        if (infos && count < max_infos) {
            memzero((Address)&infos[count], sizeof(infos[count]));
            infos[count].used = true;
            strncpy(infos[count].path, library->path, sizeof(infos[count].path) - 1);
            infos[count].path[sizeof(infos[count].path) - 1] = '\0';
            infos[count].base_va = library->base_va;
            infos[count].entry_point = library->entry_point;
            infos[count].image_size = library->image_size;
            infos[count].page_count = library->page_count;
        }
        count++;
    }

    return count;
}

UInt user_shared_library_export_snapshot(const char* path, UserSharedLibraryExportInfo* exports, UInt max_exports) {
    SharedLibrary* library;
    UInt count = 0;

    if (!path || path[0] == '\0') {
        return 0;
    }

    library = shared_library_find(path);
    if (!library) {
        return 0;
    }

    for (UInt index = 0; index < library->export_count; index++) {
        if (exports && count < max_exports) {
            memzero((Address)&exports[count], sizeof(exports[count]));
            strncpy(exports[count].name, library->exports[index].name, sizeof(exports[count].name) - 1);
            exports[count].name[sizeof(exports[count].name) - 1] = '\0';
            exports[count].address = library->exports[index].address;
        }
        count++;
    }

    return count;
}

Address load_user_shared_library_export(const char* path, const char* export_name) {
    SharedLibrary* library;

    if (!path || path[0] == '\0' || !export_name || export_name[0] == '\0') {
        return 0;
    }

    library = shared_library_find(path);
    if (!library) {
        return 0;
    }

    for (UInt index = 0; index < library->export_count; index++) {
        if (strncmp(library->exports[index].name, export_name, sizeof(library->exports[index].name)) == 0) {
            return library->exports[index].address;
        }
    }

    return 0;
}

/**
 * Release all global pages that were allocated for a partially loaded shared library.
 *
 * Args:
 *   library: Cache entry being discarded.
 *
 * Returns:
 *   Nothing. The slot is reset to the unused state.
 */
static void shared_library_release(SharedLibrary* library) {
    Address reserved_end = 0;

    if (!library) {
        return;
    }

    if (library->base_va != 0 && library->image_size != 0) {
        reserved_end = library->base_va + library->image_size;
    }

    // Return every allocated page from a failed partial load so the global allocator and cache stay consistent.
    for (UInt index = 0; index < library->page_count; index++) {
        if (library->pages[index].phys_addr) {
            // Free the backing page owned by this cache entry; affects system page availability.
            mem_free_page(library->pages[index].phys_addr);
        }
    }

    if (reserved_end != 0 && shared_library_next_va == reserved_end) {
        shared_library_next_va = library->base_va;               // roll back the last failed VA reservation
    }

    // Erase the slot metadata so later lookups treat it as unused; affects global shared library bookkeeping.
    memzero((Address)library, sizeof(*library));
}

/**
 * Find one cached physical page inside a globally loaded shared library image.
 *
 * Args:
 *   library: Global shared-library cache entry.
 *   page_va: Page-aligned virtual address inside the library image.
 *
 * Returns:
 *   Physical page base when the page is already cached, or `0` when absent.
 */
static Address shared_library_find_page(const SharedLibrary* library, Address page_va) {
    for (UInt index = 0; index < library->page_count; index++) {
        if (library->pages[index].virt_addr == page_va) {
            return library->pages[index].phys_addr;
        }
    }

    return 0;
}

/**
 * Return the MMU attributes previously recorded for one shared library page.
 *
 * Args:
 *   library: Global shared-library cache entry.
 *   page_va: Page-aligned virtual address inside the library image.
 *
 * Returns:
 *   The page-table flags for that page, or `0` when it is not cached.
 */
static Flags shared_library_find_page_flags(const SharedLibrary* library, Address page_va) {
    for (UInt index = 0; index < library->page_count; index++) {
        if (library->pages[index].virt_addr == page_va) {
            return library->pages[index].map_flags;
        }
    }

    return 0;
}

/**
 * Merge page attributes for overlapping shared-library segments that land in one 4 KiB page.
 *
 * Args:
 *   current_flags: Existing cached page-table attributes.
 *   new_flags: Attributes required by the segment currently being loaded.
 *
 * Returns:
 *   The least restrictive flags needed to satisfy both segments on the same page.
 */
static Flags shared_library_merge_page_flags(Flags current_flags, Flags new_flags) {
    if (current_flags == 0) {
        return new_flags;
    }
    if (current_flags == PE_USER_DATA || new_flags == PE_USER_DATA) {
        return PE_USER_DATA;
    }
    if (current_flags == PE_USER_CODE || new_flags == PE_USER_CODE) {
        return PE_USER_CODE;
    }

    return PE_USER_RO;
}

/**
 * Update the cached page flags for an already tracked shared-library page.
 *
 * Args:
 *   library: Global shared-library cache entry.
 *   page_va: Page-aligned virtual address inside the library image.
 *   map_flags: Additional MMU attributes required for that page.
 *
 * Returns:
 *   `0` on success, or `-1` when the page is not present in the cache yet.
 */
static int shared_library_update_page_flags(SharedLibrary* library, Address page_va, Flags map_flags) {
    for (UInt index = 0; index < library->page_count; index++) {
        if (library->pages[index].virt_addr != page_va) {
            continue;
        }

        library->pages[index].map_flags = shared_library_merge_page_flags(library->pages[index].map_flags, map_flags);
        return 0;
    }

    return -1;
}

/**
 * Record one globally cached shared-library page.
 *
 * Args:
 *   library: Global cache entry receiving the page metadata.
 *   phys_addr: Backing physical page address.
 *   virt_addr: User virtual page address used by the library image.
 *   map_flags: MMU attributes needed when mapping this page into a task.
 *
 * Returns:
 *   `0` on success, or `-1` when the library exceeds the simple page-cache limit.
 */
static int shared_library_track_page(SharedLibrary* library, Address phys_addr, Address virt_addr, Flags map_flags) {
    if (library->page_count >= USER_SHARED_LIBRARY_MAX_PAGES) {
        return -1;
    }

    library->pages[library->page_count].phys_addr = phys_addr;
    library->pages[library->page_count].virt_addr = virt_addr;
    library->pages[library->page_count].map_flags = map_flags;
    library->page_count++;
    return 0;
}

/**
 * Load one read-only shared-library segment into the global page cache.
 *
 * Args:
 *   library: Global cache entry receiving the segment pages.
 *   fd: Open VFS file descriptor for the library file.
 *   segment: Segment metadata from the packed file header.
 *
 * Returns:
 *   `0` on success, or `-1` when the segment is incompatible with the simple shared-library model.
 */
static int shared_library_load_segment(SharedLibrary* library, struct FileDesc* fd, const UserExeSegment* segment) {
    if (segment->memory_size == 0) {
        return 0;
    }
    if (segment->file_size > segment->memory_size) {
        return -1;
    }

    Address segment_start = segment->virtual_address;
    Address segment_end = segment->virtual_address + segment->memory_size;
    // Translate loader permissions into final shared mapping attributes; expects library segments to be read-only or executable.
    Flags page_flags = exe_segment_flags_to_pte(segment->flags);

    if (segment->virtual_address < USER_SHARED_LIBRARY_BASE || segment_end < segment_start) {
        return -1;
    }

    // Ensure the library cache owns one backing page for each virtual page in the segment.
    for (Address page_va = segment_start & MM_PAGE_MASK; page_va < segment_end; page_va += PAGE_SIZE) {
        // Reuse previously cached pages so overlapping segments share the same backing storage.
        if (shared_library_find_page(library, page_va)) {
            if (shared_library_update_page_flags(library, page_va, page_flags) != 0) {
                return -1;
            }
            continue;
        }

        // Allocate a global physical page for this shared mapping; affects system free-page count.
        Address page = mem_alloc_page();
        if (!page) {
            return -1;
        }

        // Pre-clear the page so any non-file-backed tail remains zero-initialized in every task.
        memzero(page + VA_START, PAGE_SIZE);
        // Track the cached page metadata for future mappings into tasks; affects library page inventory.
        if (shared_library_track_page(library, page, page_va, page_flags) != 0) {
            // Release the page immediately when the metadata table is full so it does not leak.
            mem_free_page(page);
            return -1;
        }
    }

    if (segment->file_size == 0) {
        return 0;
    }
    // Seek to the segment payload before copying bytes into the shared cache pages; affects descriptor position.
    if (vfs_fd_seek(fd, segment->file_offset, SEEK_SET) < 0) {
        return -1;
    }

    Address current_va = segment->virtual_address;
    Address remaining = segment->file_size;
    // Copy the file-backed portion of the library into its globally shared physical pages.
    while (remaining > 0) {
        Address page_va = current_va & MM_PAGE_MASK;
        // Resolve which cached page backs the current virtual address; expects the earlier allocation loop to have populated it.
        Address page = shared_library_find_page(library, page_va);
        Address page_offset = current_va - page_va;
        Address chunk = PAGE_SIZE - page_offset;

        if (!page) {
            return -1;
        }
        if (chunk > remaining) {
            chunk = remaining;
        }
        // Read bytes directly into the cached page image so every task later sees the same library contents.
        if (exec_read_exact(fd, (void*)(page + VA_START + page_offset), chunk) != 0) {
            return -1;
        }

        current_va += chunk;
        remaining -= chunk;
    }

    return 0;
}

/**
 * Kernel-thread entry used by the spawn syscall to exec a new user image with a preset name.
 *
 * Args:
 *   arg: Unused. The child reads its spawn metadata from `current_task`.
 *
 * Returns:
 *   Nothing. On success the thread transitions into the new EL0 image.
 */
static void exec_spawned_program(Pointer arg) {
    char path_copy[USER_EXEC_PATH_MAX];
    char name_copy[TASK_USER_NAME_MAX];
    char args_copy[TASK_USER_LAUNCH_ARGS_MAX];

    (void)arg;

    // Snapshot the pending executable path before exec mutates task memory; affects which image the child will load.
    strncpy(path_copy, current_process ? current_process->program_path : "", sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    // Snapshot the requested user-visible name so it survives later task metadata updates.
    strncpy(name_copy, current_process ? current_process->name : "", sizeof(name_copy) - 1);
    name_copy[sizeof(name_copy) - 1] = '\0';
    // Preserve the raw launch-argument string so the exec path does not discard shell-provided metadata.
    strncpy(args_copy, current_process ? current_process->launch_args : "", sizeof(args_copy) - 1);
    args_copy[sizeof(args_copy) - 1] = '\0';

    _trace("exec_spawned_program: start pid=%d path='%s' name='%s' args='%s'", current_task ? current_task->id : -1, path_copy, name_copy, args_copy);

    if (path_copy[0] == '\0') {
        // Abort the child immediately when no executable was staged; affects process lifetime and exit status.
        _trace("exec_spawned_program: no path staged, exiting");
        exit_current_process(-1);
        return;
    }

    // Replace the kernel-thread stub with the requested user program; expects a valid packed executable at path_copy.
    _trace("exec_spawned_program: calling exec_user_program('%s')", path_copy);
    if (exec_user_program(path_copy) != 0) {
        // Record the spawn failure for diagnosis; affects kernel logs only.
        log_error("Unable to spawn %s as %s", path_copy, name_copy);
        _trace("exec_spawned_program: exec_user_program failed for %s", path_copy);
        // Terminate the child when exec fails so callers do not observe a half-initialized task.
        exit_current_process(-1);
    }
    _trace("exec_spawned_program: exec_user_program succeeded for %s", path_copy);

    // Publish the staged child name after exec has rebuilt the task image but before returning to EL0.
    task_set_user_name(current_task, name_copy);
    if (current_process) {
        strncpy(current_process->launch_args, args_copy, sizeof(current_process->launch_args) - 1);
        current_process->launch_args[sizeof(current_process->launch_args) - 1] = '\0';
        current_process->program_path[0] = '\0';
    }
}

/**
 * Load a shared library file into the global page cache the first time it is requested.
 *
 * Args:
 *   library: Reserved cache slot that will own the shared pages.
 *   path: Absolute VFS path to the library file.
 *
 * Returns:
 *   `0` on success, or `-1` when the file cannot be opened or is incompatible with the simple DLL format.
 */
static int shared_library_load_legacy(SharedLibrary* library, const char* path) {
    struct FileDesc fd;
    UserExeHeader header;

    // Open the library file from VFS so its header and segments can be parsed; affects descriptor state on success.
    if (vfs_fd_open(&fd, path, O_READ) < 0) {
        return -1;
    }
    // Read and validate the fixed header before allocating shared pages; expects a compatible packed DLL image.
    if (exec_read_exact(&fd, &header, sizeof(header)) != 0 || exec_validate_header(&header, fd.size) != 0) {
        // Close the descriptor on malformed input so no VFS handle leaks remain.
        vfs_fd_close(&fd);
        return -1;
    }
    if (header.entry_point < USER_SHARED_LIBRARY_BASE || header.entry_point >= USER_SHARED_LIBRARY_LIMIT) {
        // Close the descriptor when the linked base falls outside the shared-library window.
        vfs_fd_close(&fd);
        return -1;
    }

    // Load each declared segment into the global shared-page cache before publishing the entry point.
    for (UInt index = 0; index < header.segment_count; index++) {
        if (shared_library_load_segment(library, &fd, &header.segments[index]) != 0) {
            // Close the descriptor when any segment load fails so cleanup can continue without leaked handles.
            vfs_fd_close(&fd);
            return -1;
        }
    }

    library->entry_point = header.entry_point;
    library->base_va = header.image_base;
    library->load_bias = header.image_base;
    library->image_size = header.image_size;
    // Release the VFS handle once the shared image is fully cached in physical memory.
    vfs_fd_close(&fd);
    return 0;
}

static int shared_library_load(SharedLibrary* library, const char* path) {
    struct FileDesc fd;
    char magic[8];
    UByte* image = null;
    int rc = -1;

    if (vfs_fd_open(&fd, path, O_READ) < 0) {
        return -1;
    }
    if (exec_read_exact(&fd, magic, sizeof(magic)) != 0) {
        vfs_fd_close(&fd);
        return -1;
    }
    vfs_fd_close(&fd);

    if (shared_library_has_legacy_magic(magic)) {
        return shared_library_load_legacy(library, path);
    }
    if (!shared_library_has_flat_magic(magic)) {
        return -1;
    }

    if (vfs_fd_open(&fd, path, O_READ) < 0) {
        return -1;
    }
    image = (UByte*)kmalloc((int)fd.size);
    if (!image) {
        vfs_fd_close(&fd);
        return -1;
    }
    if (vfs_fd_seek(&fd, 0, SEEK_SET) < 0 || vfs_fd_read(&fd, image, fd.size) != (int)fd.size) {
        goto cleanup;
    }

    rc = shared_library_load_flat_image(library, image, fd.size);

cleanup:
    vfs_fd_close(&fd);
    if (image) {
        kfree((Address)image);
    }
    return rc;
}

/**
 * Map a cached shared library into the current task at the virtual addresses it was linked for.
 *
 * Args:
 *   task: Task that wants to call into the shared library.
 *   library: Global shared-library cache entry.
 *
 * Returns:
 *   `0` on success, or `-1` when the task already uses the target virtual range for something else.
 */
static int shared_library_map_task(Task* task, const SharedLibrary* library) {
    // Mirror each cached library page into the requesting task at its linked virtual address.
    for (UInt index = 0; index < library->page_count; index++) {
        // Detect whether the task already owns this virtual page so accidental alias conflicts are rejected.
        Address existing_page = exec_find_mapped_page(task, library->pages[index].virt_addr);

        if (existing_page) {
            if (existing_page != library->pages[index].phys_addr) {
                return -1;
            }
            continue;
        }

        // Map the shared physical page into this task; affects the task page tables but not the global cache contents.
        if (process_map_shared_page(task,
            library->pages[index].phys_addr,
            library->pages[index].virt_addr,
            // Reuse the cached MMU attributes so each task observes the same access permissions.
            shared_library_find_page_flags(library, library->pages[index].virt_addr)) != 0) {
            return -1;
        }
    }

    return 0;
}

/**
 * Load and map a fixed-base shared library into the current task.
 *
 * This is a deliberately simple DLL model. Libraries are linked for a fixed
 * virtual base, loaded once into global physical pages, and then mapped into
 * each requesting task at that same address. That avoids a full relocation
 * engine while still sharing code and read-only data across applications.
 *
 * Args:
 *   path: Absolute VFS path to the packed shared library file.
 *
 * Returns:
 *   Entry-point virtual address on success, or `0` when the library cannot be loaded or mapped.
 */
Address load_user_shared_library(const char* path) {
    Task* task = current_task;
    SharedLibrary* library;
    char path_copy[USER_SHARED_LIBRARY_PATH_MAX];

    if (!task || !path || path[0] == '\0') {
        return 0;
    }

    _trace("load_user_shared_library: start pid=%d path='%s'", task->id, path);

    // Snapshot the library path locally so later logging survives caller-side buffer reuse.
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    // Prevent task switching while the global cache and this task's mappings are updated together.
    preempt_disable();

    // Reuse an existing cache entry when the library was already loaded earlier in the system lifetime.
    library = shared_library_find(path_copy);
    if (!library) {
        // Reserve a cache slot before loading pages so no other request can claim the same entry mid-load.
        library = shared_library_reserve(path_copy);
        if (!library) {
            // Re-enable scheduling before returning to keep the system runnable after the reservation failure.
            preempt_enable();
            // Record that the global DLL cache limit blocked the request; affects diagnostics only.
            log_error("Shared library cache is full while loading %s", path_copy);
            return 0;
        }
        // Populate the reserved slot with shared physical pages from the library file.
        if (shared_library_load(library, path_copy) != 0) {
            // Roll back any partially cached pages when the load fails so the slot can be reused cleanly.
            shared_library_release(library);
            // Re-enable scheduling after the failed load path finishes touching shared state.
            preempt_enable();
            // Emit a failure record for the rejected library image; affects logs but not task state.
            log_error("Unable to load shared library %s", path_copy);
            return 0;
        }
    }

    // Insert the shared library mappings into the current task at their fixed linked virtual addresses.
    if (shared_library_map_task(task, library) != 0) {
        // Re-enable scheduling before returning from a virtual-address conflict.
        preempt_enable();
        // Report that the current task already uses part of the required shared-library range.
        log_error("Shared library %s conflicts with the current user address space", path_copy);
        return 0;
    }

    if (task->mm.pgd) {
        // Switch to the task page tables so the new shared mappings become active on the current CPU.
        set_pgd(task->mm.pgd);
    }

    // Allow rescheduling again now that both cache state and task mappings are consistent.
    preempt_enable();

    // Trace the successful mapping result for debugging; affects logs only.
    _trace("Shared library %s mapped at entry 0x%lX", path_copy, library->entry_point);
    return library->entry_point;
}

/**
 * Allocate or retrieve task-private DLL-local storage for one shared library.
 *
 * Args:
 *   path: Absolute VFS path to the shared library.
 *   size: Minimum required byte size.
 *
 * Returns:
 *   User virtual address of the task-local block on success, or `0` when the request cannot be satisfied.
 */
Address load_user_shared_library_local(const char* path, ULong size) {
    Task* task = current_task;
    TaskSharedLibraryLocal* local;
    Address base;
    Address end;
    ULong page_count;
    ULong mapped_pages = 0;

    if (!task || !path || path[0] == '\0' || size == 0) {
        return 0;
    }

    preempt_disable();

    local = shared_library_find_task_local(task, path);
    if (local) {
        Address addr = local->virt_addr;

        if (size > local->size) {
            preempt_enable();
            log_error("DLL-local block for %s is too small (%lu < %lu)", path, local->size, size);
            return 0;
        }

        preempt_enable();
        return addr;
    }

    local = shared_library_reserve_task_local(task);
    if (!local) {
        preempt_enable();
        log_error("Task-local DLL storage table is full while loading %s", path);
        return 0;
    }

    page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    base = task->mm.dll_local_next ? task->mm.dll_local_next : USER_SHARED_LIBRARY_LOCAL_BASE;
    end = base + (page_count * PAGE_SIZE);
    if (end <= base || end > USER_SHARED_LIBRARY_LOCAL_LIMIT) {
        preempt_enable();
        log_error("Task-local DLL storage for %s exceeds the reserved DLL-local range", path);
        return 0;
    }

    for (ULong index = 0; index < page_count; index++) {
        Address page = mem_alloc_page();
        if (!page) {
            shared_library_release_task_local_pages(task, base, mapped_pages);
            preempt_enable();
            return 0;
        }

        memzero(page + VA_START, PAGE_SIZE);
        if (process_map_page(task, page, base + (index * PAGE_SIZE), PE_USER_DATA) != 0) {
            mem_free_page(page);                                 // return the page if task-local tracking cannot record it
            shared_library_release_task_local_pages(task, base, mapped_pages);
            preempt_enable();
            return 0;
        }
        mapped_pages++;
    }

    strncpy(local->path, path, USER_SHARED_LIBRARY_PATH_MAX - 1);
    local->path[USER_SHARED_LIBRARY_PATH_MAX - 1] = '\0';
    local->virt_addr = base;
    local->size = size;
    local->page_count = page_count;
    task->mm.dll_local_next = end;

    if (task->mm.pgd) {
        set_pgd(task->mm.pgd);
    }
    preempt_enable();

    _trace("DLL-local storage %s mapped at 0x%lX (%lu bytes)", path, base, size);
    return base;
}

/**
 * Start a new task that will exec the given user program and expose a small name string to it.
 *
 * Args:
 *   path: Absolute VFS path to the executable file.
 *   name: Small user-visible name assigned to the child task.
 *
 * Returns:
 *   Child PID on success, or `-1` when the spawn request cannot be prepared.
 */
int spawn_user_program(const char* path, const char* name, const char* args) {
    _trace("spawn_user_program: Spawning user program %s as %s", path ? path : "<null>", name ? name : "<null>");
    int pid;
    Task* child;

    if (!path || path[0] == '\0') {
        return -1;
    }

    // Hold off preemption while the new child task is created and initialized.
    preempt_disable();

    // Clone a kernel thread that will immediately exec the requested user program; affects the global task table.
    pid = (int)process_create_main_thread(PF_KTHREAD, (Address)&exec_spawned_program, 0);
    _trace("spawn_user_program: process_create_main_thread returned pid=%d", pid);
    if (pid < 0) {
        // Restore scheduling when task creation fails so the caller does not leave preemption disabled.
        preempt_enable();
        return -1;
    }

    child = tasks[pid];
    if (!child) {
        // Restore scheduling if the new PID did not yield a task slot as expected.
        preempt_enable();
        return -1;
    }

    if (!child->process) {
        preempt_enable();
        return -1;
    }
    memzero((Address)child->process->program_path, sizeof(child->process->program_path));
    memzero((Address)child->process->name, sizeof(child->process->name));
    memzero((Address)child->process->launch_args, sizeof(child->process->launch_args));

    // Stage the executable path inside the child descriptor so its bootstrap thread knows what to exec.
    strncpy(child->process->program_path, path, sizeof(child->process->program_path) - 1);
    child->process->program_path[sizeof(child->process->program_path) - 1] = '\0';
    // Publish the requested short name on the child task before it starts running.
    task_set_user_name(child, name);
    if (args && args[0] != '\0') {
        strncpy(child->process->launch_args, args, sizeof(child->process->launch_args) - 1);
        child->process->launch_args[sizeof(child->process->launch_args) - 1] = '\0';
    }

    // Let the scheduler run again now that the child bootstrap metadata is fully populated.
    preempt_enable();

    _trace("spawn_user_program: staged child %d program_path=%s name=%s", pid, child->process->program_path, child->process->name);

    return pid;
}

/**
 * Replace the current task image with a custom packed user executable.
 *
 * Args:
 *   path: Absolute VFS path to the executable file.
 *
 * Returns:
 *   `0` on success, or `-1` when the file cannot be opened, validated, or mapped.
 */
int exec_user_program(const char* path) {
    struct FileDesc fd;
    UserExeHeader header;
    Task* task = current_task;
    Task* staged_task;
    TaskMemory* old_mm;
    Address old_kernel_stack_page;
    Address stack_page;
    struct pt_regs* regs;
    char path_copy[128];

    // Snapshot the executable path for logging and error handling before VFS operations begin.
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    // Emit a trace that this task is about to replace its EL0 image; affects logs only.
    _trace("Loading executable %s", path_copy);

    // Block rescheduling while the current task image is being replaced in place.
    preempt_disable();

    staged_task = (Task*)kmalloc(sizeof(*staged_task));
    old_mm = (TaskMemory*)kmalloc(sizeof(*old_mm));
    old_kernel_stack_page = 0;
    if (!staged_task || !old_mm) {
        if (old_mm) {
            kfree((Address)old_mm);
        }
        if (staged_task) {
            kfree((Address)staged_task);
        }
        preempt_enable();
        log_error("Unable to allocate exec staging state for %s", path_copy);
        return -1;
    }

    // Open the executable from VFS so the loader can read its header and segments.
    if (vfs_fd_open(&fd, path, O_READ) < 0) {
        kfree((Address)old_mm);                                  // release staging snapshot storage on open failure
        kfree((Address)staged_task);                             // release the temporary task image container
        // Restore scheduling before returning from an open failure.
        preempt_enable();
        // Log the open failure so invalid paths are visible during bring-up.
        log_error("Can not open executable %s", path_copy);
        return -1;
    }
    // Read and validate the fixed-size header before tearing down the current user image.
    if (exec_read_exact(&fd, &header, sizeof(header)) != 0 || exec_validate_header(&header, fd.size) != 0) {
        // Release the descriptor on invalid input so the failed exec path does not leak VFS state.
        vfs_fd_close(&fd);
        kfree((Address)old_mm);                                  // return the saved-mm buffer on header failure
        kfree((Address)staged_task);                             // return the staging task buffer on header failure
        // Restore scheduling before returning control to the caller.
        preempt_enable();
        // Record that the file is not a compatible packed executable.
        log_error("Invalid executable %s", path_copy);
        return -1;
    }

    exec_init_staged_task(staged_task, task);                    // build the new user image in a temporary task context

    // Allocate one stack page for the staged image; affects physical page availability only after commit.
    stack_page = mem_alloc_page();
    if (!stack_page) {
        // Close the executable before aborting the load due to memory pressure.
        vfs_fd_close(&fd);
        kfree((Address)old_mm);                                  // release the old-mm snapshot buffer
        kfree((Address)staged_task);                             // release the temporary task image container
        // Re-enable scheduling because exec will no longer touch shared task state on this path.
        preempt_enable();
        return -1;
    }

    // Give the new image a single page stack at the fixed top-of-user-space stack address.
    if (process_map_page(staged_task, stack_page, (Address)(VA_USER_STACK - PAGE_SIZE), PE_USER_DATA) != 0) {
        mem_free_page(stack_page);                               // return the page if the staged task cannot map or track it
        vfs_fd_close(&fd);
        kfree((Address)old_mm);                                  // release the old-mm snapshot buffer
        kfree((Address)staged_task);                             // release the temporary task image container
        preempt_enable();
        return -1;
    }

    // show trace log where we place the application code .text in physical memory address and its virtual memory address
    // it should be 0x3000 virtual address of the application code .text and 0x3000 physical address of the application code .text because we use identity mapping for the user space in this kernel
    _trace("Executable %s stack page PA \x1b[32m0x%lX\x1b[0m => VA \x1b[33m0x%lX\x1b[0m",
        path_copy,
        stack_page,
        VA_USER_STACK - PAGE_SIZE);

    // Load every declared program segment into the freshly reset user address space.
    for (UInt index = 0; index < header.segment_count; index++) {
        if (exec_load_segment(staged_task, &fd, &header.segments[index]) != 0) {
            // Close the descriptor before unwinding a segment load failure.
            vfs_fd_close(&fd);
            // Restore scheduling because the in-place exec replacement is being abandoned.
            exec_release_task_image(task, &staged_task->mm, staged_task->kernel_stack_page); // free the partially staged image
            kfree((Address)old_mm);                              // release the saved-mm buffer after a staged load failure
            kfree((Address)staged_task);                         // release the temporary task image container
            preempt_enable();
            // Identify which segment failed so malformed images are easier to diagnose.
            log_error("Failed to load executable segment %u for %s", index, path_copy);
            return -1;
        }

        exec_trace_loaded_segment(staged_task, path_copy, index, &header.segments[index]);
        exec_sync_segment_icache(staged_task, &header.segments[index]);      // make newly copied EL0 code visible to instruction fetch
    }

    // trace what we just did
    _trace("Executable %s loaded. Entry 0x%lX", path_copy, header.entry_point);
    exec_trace_entry_point(staged_task, path_copy, header.entry_point);

    *old_mm = task->mm;                                           // snapshot the old address-space bookkeeping before the commit
    old_kernel_stack_page = task->kernel_stack_page;

    task->mm = staged_task->mm;                                  // publish the fully built image in one step
    task->flags = 0;
    task->cpu_context.x19 = 0;
    task->cpu_context.x20 = 0;
    task->cpu_context.x21 = 0;
    task->wakeup_tick = 0;


    // Seed the EL0 register frame so the scheduler returns directly into the new program entry point.
    // Fetch the saved register frame for this task; affects which EL0 CPU state is rewritten.
    regs = task_pt_regs(task);
    regs->pstate = PSR_MODE_EL0t;
    regs->pc = header.entry_point;
    regs->sp = VA_USER_STACK;

    // Activate the task page tables now that the replacement image is fully loaded.
    // Install the updated page tables on the current CPU so the new user mappings become live immediately.
    set_pgd(task->mm.pgd);
    exec_release_task_image(task, old_mm, old_kernel_stack_page); // free the old image after commit
    kfree((Address)old_mm);                                      // release the saved-mm snapshot buffer after cleanup
    kfree((Address)staged_task);                                 // release the staging task container after publish
    // Close the executable because all required bytes are resident in memory now.
    vfs_fd_close(&fd);
    log_info("Exec committed for %s: pgd=0x%lX pc=0x%lX sp=0x%lX", path_copy, task->mm.pgd, regs->pc, regs->sp);
    // Re-enable scheduling after the task image and MMU state are consistent again.
    preempt_enable();

    return 0;
}
