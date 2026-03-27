#include "arch/cortex-a53/mmu.h"
#include "filesystem/vfs/vfs.h"
#include "hal/hal.h"
#include "log.h"
#include "memory.h"
#include "printf.h"
#include "gui.h"
#include "module.h"
#include "module/module_format.h"
#include "task.h"
#include "timer.h"
#include "utils.h"

KernelModuleInfo* module_registry_reserve(const char* name, const char* path);
void module_registry_mark_failed(KernelModuleInfo* info);
void module_registry_mark_ready(KernelModuleInfo* info, unsigned int flags, unsigned long image_size, unsigned long bss_size);
int module_read_header(struct FileDesc* fd, ModuleBundleHeader* header);
int module_validate_header(const ModuleBundleHeader* header);

#define MODULE_RUNTIME_ABI_VERSION 1U
#define MODULE_MAX_RUNTIME_PAGES 256U
#define MODULE_MAX_SECTIONS 16U
#define MODULE_PAGE_BYTES 0x1000UL

typedef struct {
    UInt abi_version;
    UInt reserved0;
    void (*logf)(const char* level, const char* module_name, const char* fmt, ...);
    void* (*alloc)(ULong size, ULong align);
    void (*free)(void* ptr);
    ULong(*ticks_ms)(void);
} KernelModuleApi;

typedef int (*KernelModuleInit)(const KernelModuleApi* api);
typedef void (*KernelModuleShutdown)(void);
typedef void (*KernelModuleIdle)(ULong now_ms);
typedef long (*KernelModuleInvoke)(ULong a, ULong b);

typedef struct {
    char name[MODULE_BUNDLE_EXPORT_NAME_SIZE];
    KernelModuleInvoke fn;
} KernelModuleInvokeExport;

typedef struct {
    Bool used;
    Bool ready;
    char name[MODULE_MAX_NAME];
    Address base_va;
    Address load_bias;
    ULong image_size;
    ULong bss_size;
    KernelModuleApi api;
    UInt page_count;
    Address page_vas[MODULE_MAX_RUNTIME_PAGES];
    Address page_pas[MODULE_MAX_RUNTIME_PAGES];
    KernelModuleInit init_fn;
    KernelModuleShutdown shutdown_fn;
    KernelModuleIdle idle_fn;
    UInt export_count;
    KernelModuleInvokeExport exports[MODULE_BUNDLE_MAX_EXPORTS];
    Bool idle_logged;
    Bool idle_tick_valid;
    ULong last_idle_tick;
} KernelModuleRuntime;

typedef struct {
    char* buffer;
    UInt capacity;
    UInt used;
} ModuleLogBuffer;

static KernelModuleRuntime module_runtimes[MODULE_MAX_COUNT];

static void module_api_logf(const char* level, const char* module_name, const char* fmt, ...);
static void* module_api_alloc(ULong size, ULong align);
static void module_api_free(void* ptr);
static ULong module_api_ticks_ms(void);
static KernelModuleRuntime* module_find_runtime(const char* name);

typedef struct {
    const char* name;
    long (*fn)(unsigned long a, unsigned long b);
} KernelBuiltinExport;

static long module_kernel_gui_ready(unsigned long unused0, unsigned long unused1);
static long module_kernel_gui_reset(unsigned long unused0, unsigned long unused1);
static long module_kernel_usb_enabled(unsigned long unused0, unsigned long unused1);
static long module_kernel_gui_key(unsigned long key, unsigned long unused);
static long module_kernel_gui_pointer(unsigned long x, unsigned long y);

static const KernelBuiltinExport kernel_exports[] = {
    { "gui_ready", module_kernel_gui_ready },
    { "gui_reset", module_kernel_gui_reset },
    { "gui_key", module_kernel_gui_key },
    { "gui_pointer", module_kernel_gui_pointer },
    { "usb_enabled", module_kernel_usb_enabled },
};

typedef struct {
    const char* name;
    Address value;
} ModuleExport;

static const ModuleExport module_exports[] = {
    { "kmalloc", (Address)kmalloc },
    { "kfree", (Address)kfree },
    { "printf", (Address)printf },
    { "console_lock", (Address)console_lock },
    { "console_unlock", (Address)console_unlock },
    { "schedler_get_ticks", (Address)schedler_get_ticks },
    { "get_system_timer", (Address)get_system_timer },
    { "module_invoke", (Address)module_invoke },
    { "vfs_fd_open", (Address)vfs_fd_open },
    { "vfs_fd_read", (Address)vfs_fd_read },
    { "vfs_fd_write", (Address)vfs_fd_write },
    { "vfs_fd_close", (Address)vfs_fd_close },
    { "vfs_mkdir", (Address)vfs_mkdir },
    { "vfs_file_remove", (Address)vfs_file_remove },
    { "hal_partition_read", (Address)hal_partition_read },
    { "hal_partition_write", (Address)hal_partition_write },
    { "hal_partition_find_first", (Address)hal_partition_find_first },
    { "module_api_logf", (Address)module_api_logf },
    { "module_api_alloc", (Address)module_api_alloc },
    { "module_api_free", (Address)module_api_free },
    { "module_api_ticks_ms", (Address)module_api_ticks_ms },
};

static long module_kernel_gui_ready(unsigned long unused0, unsigned long unused1) {
    (void)unused0;
    (void)unused1;
    return gui_is_ready() ? 1L : 0L;
}

static long module_kernel_gui_reset(unsigned long unused0, unsigned long unused1) {
    (void)unused0;
    (void)unused1;
    gui_reset();
    return 0;
}

static long module_kernel_usb_enabled(unsigned long unused0, unsigned long unused1) {
    (void)unused0;
    (void)unused1;
    return gui_usb_enabled(0, 0);
}

static long module_kernel_gui_key(unsigned long key, unsigned long unused) {
    return gui_key(key, unused);
}

static long module_kernel_gui_pointer(unsigned long x, unsigned long y) {
    return gui_pointer(x, y);
}

/**
 * Round a value up to the requested alignment.
 *
 * Args:
 *   value: Base value to align.
 *   align: Required alignment, or zero to disable alignment.
 *
 * Behavior:
 *   Uses a power-of-two mask to produce the next aligned value.
 *
 * Returns:
 *   Aligned value, or the original value when `align` is zero.
 */
static ULong module_align_up(ULong value, ULong align) {
    if (align == 0) {
        return value;
    }
    return (value + align - 1U) & ~(align - 1U);
}

/**
 * Append one formatted character to a temporary module log buffer.
 *
 * Args:
 *   context: `ModuleLogBuffer` receiving characters.
 *   ch: Character to append.
 *
 * Behavior:
 *   Stops once the buffer would overflow and keeps the string NUL-terminated.
 *
 * Returns:
 *   Nothing.
 */
static void module_log_emit(void* context, char ch) {
    ModuleLogBuffer* buffer = (ModuleLogBuffer*)context;

    if (!buffer || buffer->used + 1U >= buffer->capacity) {
        return;
    }
    buffer->buffer[buffer->used++] = ch;
    buffer->buffer[buffer->used] = '\0';
}

/**
 * Emit a formatted log line on behalf of a loaded module.
 *
 * Args:
 *   level: Optional textual log level.
 *   module_name: Optional module name prefix.
 *   fmt: Printf-style format string.
 *   ...: Format arguments.
 *
 * Behavior:
 *   Formats the message into a bounded stack buffer, serializes console access,
 *   and prints a consistent `[KMOD]` prefix.
 *
 * Returns:
 *   Nothing.
 */
static void module_api_logf(const char* level, const char* module_name, const char* fmt, ...) {
    char message[192];
    ModuleLogBuffer buffer = { message, sizeof(message), 0 };
    va_list args;

    message[0] = '\0';
    va_start(args, fmt);
    // Reuse the tiny formatter already used by the kernel printf subsystem.
    tfp_format(&buffer, module_log_emit, (char*)fmt, args);
    va_end(args);

    // Serialize UART output so multi-core logs do not interleave mid-line.
    console_lock();
    if (level && module_name) {
        printf("[KMOD][%s][%s] %s\n", (char*)level, (char*)module_name, message);
    }
    else if (module_name) {
        printf("[KMOD][%s] %s\n", (char*)module_name, message);
    }
    else {
        printf("[KMOD] %s\n", message);
    }
    console_unlock();
}

/**
 * Allocate memory on behalf of module code.
 *
 * Args:
 *   size: Allocation size in bytes.
 *   align: Requested alignment, currently ignored.
 *
 * Behavior:
 *   Forwards directly to the kernel heap allocator.
 *
 * Returns:
 *   Allocated pointer, or `null` on failure.
 */
static void* module_api_alloc(ULong size, ULong align) {
    (void)align;
    return (void*)kmalloc((int)size);
}

/**
 * Free memory previously allocated through the module API.
 *
 * Args:
 *   ptr: Pointer to free.
 *
 * Behavior:
 *   Ignores null pointers and otherwise forwards to the kernel heap free path.
 *
 * Returns:
 *   Nothing.
 */
static void module_api_free(void* ptr) {
    if (!ptr) {
        return;
    }
    kfree((Address)ptr);
}

/**
 * Convert scheduler ticks into the module ABI millisecond clock.
 *
 * Args:
 *   None.
 *
 * Behavior:
 *   Multiplies the kernel tick counter by the fixed per-tick module duration.
 *
 * Returns:
 *   Millisecond timestamp used by module idle hooks and queries.
 */
static ULong module_api_ticks_ms(void) {
    return schedler_get_ticks() * MODULE_TICK_MSEC;
}

/**
 * Reserve a runtime slot for a module being loaded.
 *
 * Args:
 *   name: Module name to copy into the slot.
 *   path: Module path, currently unused.
 *
 * Behavior:
 *   Clears the first free runtime slot and marks it used so loading can attach
 *   pages, callbacks, and export metadata.
 *
 * Returns:
 *   Pointer to the reserved runtime slot, or `null` when all slots are busy.
 */
static KernelModuleRuntime* module_runtime_reserve(const char* name, const char* path) {
    (void)path;

    for (UInt index = 0; index < MODULE_MAX_COUNT; index++) {
        if (module_runtimes[index].used) {
            continue;
        }

        // Clear every field so failed loads cannot leak half-built runtime state.
        memzero((Address)&module_runtimes[index], sizeof(module_runtimes[index]));
        module_runtimes[index].used = true;
        if (name) {
            strncpy(module_runtimes[index].name, name, sizeof(module_runtimes[index].name) - 1);
        }
        return &module_runtimes[index];
    }

    return null;
}

/**
 * Tear down a partially or fully allocated runtime slot.
 *
 * Args:
 *   runtime: Runtime slot to release.
 *
 * Behavior:
 *   Unmaps every tracked virtual page, frees its backing physical page, and
 *   then marks the slot unused.
 *
 * Returns:
 *   Nothing.
 */
static void module_runtime_release(KernelModuleRuntime* runtime) {
    if (!runtime) {
        return;
    }

    // Walk only the tracked pages because sparse module images may not use every VA.
    for (UInt index = 0; index < runtime->page_count; index++) {
        if (runtime->page_vas[index] != 0) {
            module_vm_unmap_page(runtime->page_vas[index]);
        }
        if (runtime->page_pas[index] != 0) {
            mem_free_page(runtime->page_pas[index]);
        }
    }
    runtime->used = false;
}

/**
 * Find the physical page backing a mapped module virtual page.
 *
 * Args:
 *   runtime: Runtime slot that owns the mapping.
 *   page_va: Page-aligned module virtual address.
 *
 * Behavior:
 *   Performs a linear scan over the runtime's tracked mappings.
 *
 * Returns:
 *   Physical page address on success, or `0` when the page is not tracked.
 */
static Address module_find_runtime_page(const KernelModuleRuntime* runtime, Address page_va) {
    if (!runtime) {
        return 0;
    }

    for (UInt index = 0; index < runtime->page_count; index++) {
        if (runtime->page_vas[index] == page_va) {
            return runtime->page_pas[index];
        }
    }

    return 0;
}

/**
 * Record a newly mapped runtime page in the slot bookkeeping.
 *
 * Args:
 *   runtime: Runtime slot to update.
 *   page_va: Mapped virtual page address.
 *   page_pa: Backing physical page address.
 *
 * Behavior:
 *   Appends the mapping to the parallel VA/PA arrays used by teardown and copy
 *   helpers.
 *
 * Returns:
 *   `0` on success, or `-1` when the runtime is invalid or full.
 */
static int module_track_runtime_page(KernelModuleRuntime* runtime, Address page_va, Address page_pa) {
    if (!runtime || runtime->page_count >= MODULE_MAX_RUNTIME_PAGES) {
        return -1;
    }

    runtime->page_vas[runtime->page_count] = page_va;
    runtime->page_pas[runtime->page_count] = page_pa;
    runtime->page_count++;
    return 0;
}

/**
 * Translate module section flags into MMU page permissions.
 *
 * Args:
 *   flags: Module section access flags.
 *
 * Behavior:
 *   Prefers executable mappings over writable mappings, falling back to
 *   read-only pages for everything else.
 *
 * Returns:
 *   Page-table flags suitable for `module_vm_update_page_flags`.
 */
static Flags module_section_flags_to_pte(UInt flags) {
    if (flags & MOD_SECTION_FLAG_EXEC) {
        return PE_KERNEL_CODE;
    }
    if (flags & MOD_SECTION_FLAG_WRITE) {
        return PE_KERNEL_DATA;
    }
    return PE_KERNEL_RO;
}

/**
 * Ensure every page touched by a virtual segment has backing memory and a map.
 *
 * Args:
 *   runtime: Runtime slot that owns the segment.
 *   start_va: Inclusive start virtual address.
 *   end_va: Exclusive end virtual address.
 *   flags: Initial mapping flags to use.
 *
 * Behavior:
 *   Iterates page by page, allocates missing physical pages, zeroes them, maps
 *   them into the module region, and records them for later teardown.
 *
 * Returns:
 *   `0` on success, or `-1` when allocation, mapping, or bookkeeping fails.
 */
static int module_map_segment_range(KernelModuleRuntime* runtime, Address start_va, Address end_va, Flags flags) {
    for (Address page_va = start_va & MM_PAGE_MASK; page_va < end_va; page_va += MODULE_PAGE_BYTES) {
        Address page;

        // Skip pages already provisioned because adjacent sections may share one page.
        if (module_find_runtime_page(runtime, page_va)) {
            continue;
        }

        // Allocate zero-filled backing memory before exposing the mapping.
        page = mem_alloc_page();
        if (!page) {
            return -1;
        }
        memzero(page + VA_START, MODULE_PAGE_BYTES);
        // Map and track the page as one logical operation so teardown stays correct.
        if (module_vm_map_page(page_va, page, flags) != 0 || module_track_runtime_page(runtime, page_va, page) != 0) {
            mem_free_page(page);
            return -1;
        }
    }

    return 0;
}

/**
 * Copy bytes into previously mapped module pages.
 *
 * Args:
 *   runtime: Runtime slot containing the mapped pages.
 *   start_va: Destination virtual address.
 *   source: Source byte buffer.
 *   size: Number of bytes to copy.
 *
 * Behavior:
 *   Walks page boundaries manually so the copy can span multiple tracked pages.
 *
 * Returns:
 *   `0` on success, or `-1` when any destination page is missing.
 */
static int module_copy_bytes(KernelModuleRuntime* runtime, Address start_va, const UByte* source, ULong size) {
    Address current_va = start_va;
    ULong remaining = size;

    while (remaining > 0) {
        Address page_va = current_va & MM_PAGE_MASK;
        Address page = module_find_runtime_page(runtime, page_va);
        Address page_offset = current_va - page_va;
        Address chunk = MODULE_PAGE_BYTES - page_offset;

        if (!page) {
            return -1;
        }
        if (chunk > remaining) {
            chunk = remaining;
        }
        // Copy only the bytes that fit in the current page before advancing.
        memcpy(
            page + VA_START + page_offset,
            (Address)source,
            (int)chunk);
        source += chunk;
        current_va += chunk;
        remaining -= chunk;
    }

    return 0;
}

/**
 * Validate the flat payload embedded inside a `.sys` bundle.
 *
 * Args:
 *   image: Flat module payload bytes.
 *   image_size: Payload size in bytes.
 *   out_header: Optional pointer receiving the parsed header.
 *
 * Behavior:
 *   Verifies header fields, metadata tables, string references, and every
 *   section/import/relocation range before the loader maps anything.
 *
 * Returns:
 *   `0` on success, or `-1` when the flat image is malformed.
 */
static int module_validate_flat_image(const UByte* image, ULong image_size, const ModuleHeader** out_header) {
    const ModuleHeader* header;
    const UByte* metadata_end;
    const ModuleSection* sections;
    const ModuleImport* imports;
    const ModuleReloc* relocs;
    ULong cursor;

    if (!image || image_size < sizeof(ModuleHeader)) {
        return -1;
    }

    header = (const ModuleHeader*)image;
    if (header->magic[0] != MOD_MAGIC_0 ||
        header->magic[1] != MOD_MAGIC_1 ||
        header->magic[2] != MOD_MAGIC_2 ||
        header->magic[3] != MOD_MAGIC_3 ||
        header->magic[4] != MOD_MAGIC_4 ||
        header->magic[5] != MOD_MAGIC_5 ||
        header->magic[6] != MOD_MAGIC_6 ||
        header->magic[7] != MOD_MAGIC_7) {
        return -1;
    }
    if (header->abi_version != MOD_ABI_VERSION || header->machine != MOD_MACHINE_AARCH64) {
        return -1;
    }
    if (header->header_size < sizeof(ModuleHeader) || header->header_size > image_size) {
        return -1;
    }
    if (header->section_count == 0 || header->section_count > MODULE_MAX_SECTIONS) {
        return -1;
    }
    if (header->entry_section >= header->section_count) {
        return -1;
    }
    if (header->align == 0 || !mem_is_page_aligned(header->align)) {
        return -1;
    }
    if (header->image_size == 0 || !mem_is_page_aligned(header->image_size)) {
        return -1;
    }

    cursor = sizeof(ModuleHeader);
    if (cursor + ((ULong)header->section_count * sizeof(ModuleSection)) > header->header_size) {
        return -1;
    }
    sections = (const ModuleSection*)(image + cursor);
    cursor += (ULong)header->section_count * sizeof(ModuleSection);

    if (cursor + ((ULong)header->import_count * sizeof(ModuleImport)) > header->header_size) {
        return -1;
    }
    imports = (const ModuleImport*)(image + cursor);
    cursor += (ULong)header->import_count * sizeof(ModuleImport);

    if (cursor + ((ULong)header->reloc_count * sizeof(ModuleReloc)) > header->header_size) {
        return -1;
    }
    relocs = (const ModuleReloc*)(image + cursor);
    metadata_end = image + header->header_size;

    // Validate every section range before trusting runtime offsets or file data.
    for (UInt index = 0; index < header->section_count; index++) {
        const ModuleSection* section = &sections[index];

        if (section->type > MOD_SEC_DATA) {
            return 1;
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

    // Confirm that every import patch and import name stays inside the payload header.
    for (UInt index = 0; index < header->import_count; index++) {
        const ModuleImport* entry = &imports[index];
        const char* name;

        if (entry->type != MOD_IMPORT_ABS64 && entry->type != MOD_IMPORT_REL64) {
            return -1;
        }
        if (entry->patch_offset + sizeof(ULong) > header->image_size) {
            return -1;
        }
        if (entry->name_offset >= header->header_size) {
            return -1;
        }
        name = (const char*)(image + entry->name_offset);
        while ((const UByte*)name < metadata_end && *name != '\0') {
            name++;
        }
        if ((const UByte*)name >= metadata_end) {
            return -1;
        }
    }

    // Validate relocation types and bounds before applying them to mapped memory.
    for (UInt index = 0; index < header->reloc_count; index++) {
        const ModuleReloc* entry = &relocs[index];

        if (entry->type != MOD_RELOC_ABS64 && entry->type != MOD_RELOC_REL64 && entry->type != MOD_RELOC_SECTION) {
            return -1;
        }
        if (entry->section >= header->section_count) {
            return -1;
        }
        if (entry->offset + sizeof(ULong) > header->image_size) {
            return -1;
        }
    }

    if (out_header) {
        *out_header = header;
    }
    return 0;
}

/**
 * Resolve a kernel import name from the static export table.
 *
 * Args:
 *   name: Import symbol name.
 *
 * Behavior:
 *   Performs a linear search over the kernel-side symbols exposed to modules.
 *
 * Returns:
 *   Resolved address, or `0` when the import is unknown.
 */
static Address module_lookup_export(const char* name) {
    for (UInt index = 0; index < (UInt)(sizeof(module_exports) / sizeof(module_exports[0])); index++) {
        if (strcmp(name, module_exports[index].name) == 0) {
            return module_exports[index].value;
        }
    }
    return 0;
}

/**
 * Return the section-table pointer inside a flat module header.
 *
 * Args:
 *   header: Flat module header.
 *
 * Behavior:
 *   Interprets the metadata blob immediately after the fixed header.
 *
 * Returns:
 *   Pointer to the first `ModuleSection` entry.
 */
static const ModuleSection* module_sections(const ModuleHeader* header) {
    return (const ModuleSection*)(((const UByte*)header) + sizeof(ModuleHeader));
}

/**
 * Return the import-table pointer inside a flat module header.
 *
 * Args:
 *   header: Flat module header.
 *
 * Behavior:
 *   Skips over the section table to reach the packed import table.
 *
 * Returns:
 *   Pointer to the first `ModuleImport` entry.
 */
static const ModuleImport* module_imports(const ModuleHeader* header) {
    return (const ModuleImport*)(((const UByte*)module_sections(header)) + ((ULong)header->section_count * sizeof(ModuleSection)));
}

/**
 * Return the relocation-table pointer inside a flat module header.
 *
 * Args:
 *   header: Flat module header.
 *
 * Behavior:
 *   Skips over both section and import metadata to reach relocations.
 *
 * Returns:
 *   Pointer to the first `ModuleReloc` entry.
 */
static const ModuleReloc* module_relocs(const ModuleHeader* header) {
    return (const ModuleReloc*)(((const UByte*)module_imports(header)) + ((ULong)header->import_count * sizeof(ModuleImport)));
}

/**
 * Compute runtime offsets for each flat-module section.
 *
 * Args:
 *   header: Flat module header.
 *   section_offsets: Output array indexed by section number.
 *
 * Behavior:
 *   Copies each section's runtime offset into a scratch array and verifies that
 *   the resulting image end matches the header's declared image size.
 *
 * Returns:
 *   `0` on success, or `-1` when offsets are inconsistent.
 */
static int module_compute_runtime_offsets(const ModuleHeader* header, ULong* section_offsets) {
    const ModuleSection* sections = module_sections(header);
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

    image_end = module_align_up(image_end, header->align);
    return image_end == header->image_size ? 0 : -1;
}

/**
 * Translate a section-relative offset into a loaded module virtual address.
 *
 * Args:
 *   runtime: Loaded runtime slot.
 *   header: Flat module header.
 *   section_offsets: Computed per-section runtime offsets.
 *   section: Section index.
 *   offset: Offset inside the section.
 *
 * Behavior:
 *   Checks the requested section bounds and then adds the section base to the
 *   runtime load base.
 *
 * Returns:
 *   Virtual address for the requested location, or `0` when invalid.
 */
static Address module_section_runtime_va(const KernelModuleRuntime* runtime, const ModuleHeader* header, const ULong* section_offsets, UInt section, ULong offset) {
    const ModuleSection* sections = module_sections(header);

    if (!runtime || !header || !section_offsets || section >= header->section_count) {
        return 0;
    }
    if (offset >= sections[section].mem_size) {
        return 0;
    }
    return runtime->base_va + section_offsets[section] + offset;
}

/**
 * Store a 64-bit value into mapped module memory.
 *
 * Args:
 *   target_va: Destination virtual address.
 *   value: Value to write.
 *
 * Behavior:
 *   Performs a guarded volatile store used by relocation and import patching.
 *
 * Returns:
 *   `0` on success, or `-1` when the target address overflows or is null.
 */
static int module_patch_u64(Address target_va, ULong value) {
    if (target_va == 0 || target_va + sizeof(ULong) < target_va) {
        return -1;
    }
    *((volatile ULong*)target_va) = value;
    return 0;
}

/**
 * Read a 64-bit value from mapped module memory.
 *
 * Args:
 *   target_va: Source virtual address.
 *
 * Behavior:
 *   Returns zero for null addresses so callers can treat missing addends as 0.
 *
 * Returns:
 *   Value stored at the address, or `0` when the address is null.
 */
static ULong module_read_u64(Address target_va) {
    if (target_va == 0) {
        return 0;
    }
    return *((volatile ULong*)target_va);
}

/**
 * Apply section-relative relocations to a mapped module image.
 *
 * Args:
 *   runtime: Loaded runtime slot.
 *   header: Flat module header.
 *   section_offsets: Computed runtime offsets per section.
 *
 * Behavior:
 *   Resolves each relocation target and patches the mapped image with either an
 *   absolute or PC-relative value.
 *
 * Returns:
 *   `0` on success, or `-1` when any relocation is invalid or patching fails.
 */
static int module_apply_relocs(KernelModuleRuntime* runtime, const ModuleHeader* header, const ULong* section_offsets) {
    const ModuleReloc* relocs = module_relocs(header);

    for (UInt index = 0; index < header->reloc_count; index++) {
        const ModuleReloc* entry = &relocs[index];
        Address patch_va = runtime->base_va + entry->offset;
        Address target_va = module_section_runtime_va(runtime, header, section_offsets, entry->section, entry->addend);
        ULong patch_value;

        if (!target_va) {
            return -1;
        }

        // Encode the relocation according to the flat-module relocation type.
        if (entry->type == MOD_RELOC_SECTION || entry->type == MOD_RELOC_ABS64) {
            patch_value = target_va;
        }
        else if (entry->type == MOD_RELOC_REL64) {
            patch_value = (ULong)((Long)target_va - (Long)patch_va);
        }
        else {
            return -1;
        }

        if (module_patch_u64(patch_va, patch_value) != 0) {
            return -1;
        }
    }

    return 0;
}

/**
 * Apply kernel import fixups to a mapped module image.
 *
 * Args:
 *   runtime: Loaded runtime slot.
 *   header: Flat module header.
 *
 * Behavior:
 *   Resolves each imported symbol from the static kernel export table, reads any
 *   in-place addend, and patches the mapped site with the final address.
 *
 * Returns:
 *   `0` on success, or `-1` when an import is unknown or patching fails.
 */
static int module_apply_imports(KernelModuleRuntime* runtime, const ModuleHeader* header) {
    const ModuleImport* imports = module_imports(header);
    const UByte* image = (const UByte*)header;

    for (UInt index = 0; index < header->import_count; index++) {
        const ModuleImport* entry = &imports[index];
        const char* name = (const char*)(image + entry->name_offset);
        Address patch_va = runtime->base_va + entry->patch_offset;
        Address export_value = module_lookup_export(name);
        ULong addend;
        ULong patch_value;

        if (!export_value) {
            log_error("Kernel module %s import not found: %s", runtime->name, name);
            return -1;
        }

        // Read the linker-emitted addend from the mapped site before patching it.
        addend = module_read_u64(patch_va);
        if (entry->type == MOD_IMPORT_ABS64) {
            patch_value = export_value + addend;
        }
        else if (entry->type == MOD_IMPORT_REL64) {
            patch_value = (ULong)((Long)(export_value + addend) - (Long)patch_va);
        }
        else {
            return -1;
        }

        if (module_patch_u64(patch_va, patch_value) != 0) {
            return -1;
        }
    }

    return 0;
}

static int module_finalize_section_flags(KernelModuleRuntime* runtime, const ModuleHeader* header, const ULong* section_offsets) {
    const ModuleSection* sections = module_sections(header);

    // Recompute effective page permissions from overlapping sections one page at a time.
    for (Address page_offset = 0; page_offset < header->image_size; page_offset += MODULE_PAGE_BYTES) {
        UInt page_flags = 0;

        for (UInt index = 0; index < header->section_count; index++) {
            Address section_start = section_offsets[index];
            Address section_end = section_start + sections[index].mem_size;
            Address page_end = page_offset + MODULE_PAGE_BYTES;

            if (section_end <= page_offset || section_start >= page_end) {
                continue;
            }
            page_flags |= sections[index].flags;
        }

        if (page_flags != 0 && module_vm_update_page_flags(runtime->base_va + page_offset, module_section_flags_to_pte(page_flags)) != 0) {
            return -1;
        }
    }

    return 0;
}

/**
 * Resolve an optional lifecycle callback to a runtime virtual address.
 *
 * Args:
 *   runtime: Loaded runtime slot.
 *   header: Flat module header.
 *   section_offsets: Computed runtime offsets.
 *   section: Section that contains the callback.
 *   offset: Byte offset of the callback inside the section.
 *   out_address: Optional output pointer for the resolved address.
 *
 * Behavior:
 *   Treats `MOD_INVALID_SECTION` as an intentionally absent callback and
 *   otherwise resolves the function address from section metadata.
 *
 * Returns:
 *   `0` on success, or `-1` when the callback location is invalid.
 */
static int module_resolve_callback(const KernelModuleRuntime* runtime, const ModuleHeader* header, const ULong* section_offsets, UInt section, ULong offset, Address* out_address) {
    Address callback;

    if (section == MOD_INVALID_SECTION) {
        if (out_address) {
            *out_address = 0;
        }
        return 0;
    }

    callback = module_section_runtime_va(runtime, header, section_offsets, section, offset);
    if (!callback) {
        return -1;
    }
    if (out_address) {
        *out_address = callback;
    }
    return 0;
}

/**
 * Resolve manifest-declared user-callable exports for a loaded module.
 *
 * Args:
 *   runtime: Loaded runtime slot to populate.
 *   header: Flat module header.
 *   bundle: Bundle header containing export metadata.
 *   section_offsets: Computed runtime offsets.
 *
 * Behavior:
 *   Walks the export table from the bundle header, resolves each target into a
 *   function pointer, and stores it in the runtime slot.
 *
 * Returns:
 *   `0` on success, or `-1` when any export is invalid.
 */
static int module_resolve_exports(KernelModuleRuntime* runtime, const ModuleHeader* header, const ModuleBundleHeader* bundle, const ULong* section_offsets) {
    UInt index;

    if (!runtime || !header || !bundle || !section_offsets) {
        return -1;
    }

    runtime->export_count = 0;
    for (index = 0; index < bundle->export_count; index++) {
        Address export_va = 0;

        // Reuse callback resolution because exports share the same section+offset encoding.
        if (module_resolve_callback(runtime, header, section_offsets, bundle->exports[index].section, bundle->exports[index].offset, &export_va) != 0 || !export_va) {
            log_error("Kernel module %s export %s is invalid", runtime->name, bundle->exports[index].name);
            return -1;
        }

        strncpy(runtime->exports[index].name, bundle->exports[index].name, sizeof(runtime->exports[index].name) - 1);
        runtime->exports[index].fn = (KernelModuleInvoke)export_va;
        runtime->export_count++;
    }

    return 0;
}

/**
 * Map and initialize the flat payload for one kernel module.
 *
 * Args:
 *   runtime: Reserved runtime slot to populate.
 *   image: Flat payload bytes.
 *   image_size: Payload size in bytes.
 *   bundle: Parsed bundle header containing lifecycle metadata.
 *   loaded_image_size: Optional output for mapped image size.
 *   loaded_bss_size: Optional output for BSS size.
 *
 * Behavior:
 *   Validates the flat payload, reserves virtual space, maps and copies every
 *   section, applies relocations/imports, tightens page permissions, syncs the
 *   I-cache, and resolves callbacks plus exports.
 *
 * Returns:
 *   `0` on success, or `-1` when any stage of loading fails.
 */
static int module_load_flat_image(KernelModuleRuntime* runtime, const UByte* image, ULong image_size, const ModuleBundleHeader* bundle, ULong* loaded_image_size, ULong* loaded_bss_size) {
    const ModuleHeader* header = null;
    const ModuleSection* sections;
    ULong section_offsets[MODULE_MAX_SECTIONS];
    Address base_va;
    Address callback_va = 0;

    if (module_validate_flat_image(image, image_size, &header) != 0) {
        log_error("Kernel module %s payload is not a valid flat module image", runtime->name);
        return -1;
    }
    if (module_compute_runtime_offsets(header, section_offsets) != 0) {
        log_error("Kernel module %s payload layout is inconsistent", runtime->name);
        return -1;
    }

    base_va = module_vm_reserve(header->image_size, header->align);
    if (!base_va) {
        log_error("Kernel module region exhausted while loading %s", runtime->name);
        return -1;
    }

    // Cache key layout facts in the runtime slot before section mapping begins.
    runtime->base_va = base_va;
    runtime->load_bias = base_va;
    runtime->image_size = header->image_size;
    runtime->bss_size = header->bss_size;

    sections = module_sections(header);
    for (UInt index = 0; index < header->section_count; index++) {
        Address start_va = runtime->base_va + section_offsets[index];
        Address end_va = start_va + sections[index].mem_size;

        // Validate that the mapped section stays inside the reserved module window.
        if (end_va < start_va || end_va > MODULE_REGION_LIMIT) {
            log_error("Kernel module %s exceeds the reserved module address window", runtime->name);
            return -1;
        }
        // Start every section writable so relocations and import fixups can patch it.
        if (module_map_segment_range(runtime, start_va, end_va, PE_KERNEL_DATA) != 0) {
            return -1;
        }
        // Copy only the file-backed prefix; trailing bytes remain zero for BSS.
        if (sections[index].file_size != 0 && module_copy_bytes(runtime, start_va, image + sections[index].file_offset, sections[index].file_size) != 0) {
            return -1;
        }
    }

    // Perform all fixups before making code pages executable or read-only.
    if (module_apply_relocs(runtime, header, section_offsets) != 0) {
        log_error("Kernel module %s relocation processing failed", runtime->name);
        return -1;
    }
    if (module_apply_imports(runtime, header) != 0) {
        return -1;
    }
    if (module_finalize_section_flags(runtime, header, section_offsets) != 0) {
        return -1;
    }
    // Make freshly written code visible to instruction fetch.
    module_vm_sync_icache(runtime->base_va, header->image_size);

    if (module_resolve_callback(runtime, header, section_offsets, bundle->init_section, bundle->init_offset, &callback_va) != 0 || !callback_va) {
        log_error("Kernel module %s init callback is invalid", runtime->name);
        return -1;
    }
    runtime->init_fn = (KernelModuleInit)callback_va;

    if (module_resolve_callback(runtime, header, section_offsets, bundle->shutdown_section, bundle->shutdown_offset, &callback_va) != 0) {
        log_error("Kernel module %s shutdown callback is invalid", runtime->name);
        return -1;
    }
    runtime->shutdown_fn = (KernelModuleShutdown)callback_va;

    if (module_resolve_callback(runtime, header, section_offsets, bundle->idle_section, bundle->idle_offset, &callback_va) != 0) {
        log_error("Kernel module %s idle callback is invalid", runtime->name);
        return -1;
    }
    runtime->idle_fn = (KernelModuleIdle)callback_va;
    if (module_resolve_exports(runtime, header, bundle, section_offsets) != 0) {
        return -1;
    }

    if (loaded_image_size) {
        *loaded_image_size = header->image_size;
    }
    if (loaded_bss_size) {
        *loaded_bss_size = header->bss_size;
    }
    return 0;
}

/**
 * Invoke a module's required init callback with the runtime API table.
 *
 * Args:
 *   runtime: Loaded runtime slot.
 *
 * Behavior:
 *   Fills out the ABI table exposed to module code and calls the resolved init
 *   hook when one exists.
 *
 * Returns:
 *   Module init return code, or `0` when no init hook is present.
 */
static int module_run_init(KernelModuleRuntime* runtime) {
    if (!runtime || !runtime->init_fn) {
        return 0;
    }

    // Rebuild the API table each time so runtime state stays self-contained.
    memzero((Address)&runtime->api, sizeof(runtime->api));
    runtime->api.abi_version = MODULE_RUNTIME_ABI_VERSION;
    runtime->api.logf = module_api_logf;
    runtime->api.alloc = module_api_alloc;
    runtime->api.free = module_api_free;
    runtime->api.ticks_ms = module_api_ticks_ms;
    return runtime->init_fn(&runtime->api);
}

/**
 * Locate a ready runtime slot by module name.
 *
 * Args:
 *   name: Module name to search for.
 *
 * Behavior:
 *   Scans only slots that are both allocated and ready for calls.
 *
 * Returns:
 *   Pointer to the matching runtime slot, or `null` when absent.
 */
static KernelModuleRuntime* module_find_runtime(const char* name) {
    if (!name || name[0] == '\0') {
        return null;
    }

    for (UInt index = 0; index < MODULE_MAX_COUNT; index++) {
        if (!module_runtimes[index].used || !module_runtimes[index].ready) {
            continue;
        }
        if (strncmp(module_runtimes[index].name, name, sizeof(module_runtimes[index].name)) == 0) {
            return &module_runtimes[index];
        }
    }

    return null;
}

/**
 * Check whether a filename ends with the `.sys` extension.
 *
 * Args:
 *   name: Filename to inspect.
 *
 * Behavior:
 *   Performs a case-insensitive suffix check used while scanning `/system`.
 *
 * Returns:
 *   `true` when the filename ends in `.sys`, otherwise `false`.
 */
static Bool module_path_has_sys_suffix(const char* name) {
    int len;

    if (!name) {
        return false;
    }
    len = strlen(name);
    return len >= 4 && name[len - 4] == '.' &&
        (name[len - 3] == 's' || name[len - 3] == 'S') &&
        (name[len - 2] == 'y' || name[len - 2] == 'Y') &&
        (name[len - 1] == 's' || name[len - 1] == 'S');
}

/**
 * Probe and load one candidate module file from disk.
 *
 * Args:
 *   path: Full filesystem path to the module bundle.
 *   display_name: Fallback name to use if the bundle header name is empty.
 *
 * Behavior:
 *   Opens the file, reads and validates the bundle header, reserves registry and
 *   runtime slots, loads the flat payload, runs init, and publishes ready-state
 *   metadata. Any partial state is cleaned up on failure.
 *
 * Returns:
 *   `0` on success, or `-1` when probing or loading fails.
 */
static int module_probe_file(const char* path, const char* display_name) {
    struct FileDesc fd;
    ModuleBundleHeader header;
    KernelModuleInfo* info = null;
    KernelModuleRuntime* runtime = null;
    UByte* module_image = null;
    UInt module_offset;
    ULong image_size = 0;
    ULong bss_size = 0;
    Bool fd_open = false;
    int rc = -1;

    if (vfs_fd_open(&fd, path, O_READ) != SUCCESS) {
        return -1;
    }
    fd_open = true;
    // Read and validate the outer bundle before touching the payload body.
    if (module_read_header(&fd, &header) != 0) {
        goto cleanup;
    }

    if (module_validate_header(&header) != 0) {
        log_warning("Skipping legacy or invalid module image %s", path);
        goto cleanup;
    }

    if (module_find(header.name)) {
        log_info("Kernel module %s is already loaded", header.name);
        rc = 0;
        goto cleanup;
    }

    // Reserve registry and runtime bookkeeping before allocating the payload buffer.
    info = module_registry_reserve(header.name[0] ? header.name : display_name, path);
    if (!info) {
        log_error("Module registry is full; cannot track %s", path);
        return -1;
    }
    runtime = module_runtime_reserve(info->name, path);
    if (!runtime) {
        log_error("Module runtime slots are full; cannot load %s", path);
        module_registry_mark_failed(info);
        return -1;
    }

    module_offset = module_align_up(header.header_size + header.manifest_size, header.payload_align);
    module_image = (UByte*)kmalloc((int)header.module_size);
    if (!module_image) {
        goto cleanup;
    }
    // Seek past header + manifest padding to reach the packed flat payload.
    if (vfs_fd_seek(&fd, module_offset, SEEK_SET) < 0 || vfs_fd_read(&fd, module_image, header.module_size) != (int)header.module_size) {
        goto cleanup;
    }
    vfs_fd_close(&fd);
    fd_open = false;

    if (module_load_flat_image(runtime, module_image, header.module_size, &header, &image_size, &bss_size) != 0) {
        goto cleanup;
    }
    if (module_run_init(runtime) != 0) {
        log_error("Kernel module %s init failed", info->name);
        goto cleanup;
    }
    runtime->ready = true;

    // Publish registry state only after the runtime is fully initialized.
    module_registry_mark_ready(info, header.flags, image_size, bss_size);
    log_info("Loaded kernel module %s from %s (image=%u bss=%u)", info->name, path, image_size, bss_size);
    rc = 0;

cleanup:
    // Release transient resources no matter which stage failed.
    if (fd_open) {
        vfs_fd_close(&fd);
    }
    if (module_image) {
        kfree((Address)module_image);
    }
    if (rc != 0) {
        if (info) {
            module_registry_mark_failed(info);
        }
        if (runtime) {
            module_runtime_release(runtime);
        }
    }
    return rc;
}

/**
 * Scan `/system` and load every `.sys` kernel module found there.
 *
 * Args:
 *   None.
 *
 * Behavior:
 *   Enumerates directory entries, filters regular `.sys` files, converts long
 *   names to UTF-8, derives a short display name, and probes each candidate.
 *
 * Returns:
 *   Nothing.
 */
void module_load_boot_modules(void) {
    struct FileDesc dir;
    struct DirectoryEntry entry;

    if (vfs_dir_open(&dir, "/system") != SUCCESS) {
        log_warning("Module scan skipped: /system is unavailable");
        return;
    }

    for (;;) {
        UByte* name_utf8 = null;
        int next = vfs_dir_read_ex(&dir, &entry);

        if (next <= 0) {
            break;
        }
        // Skip directories because only regular files can be module bundles.
        if (entry.attr & 0x10) {
            continue;
        }
        // Convert the long UTF-16 directory entry into UTF-8 for path construction.
        if (str_from_utf16(entry.long_name, 255, &name_utf8) != 0) {
            continue;
        }
        if (module_path_has_sys_suffix((char*)name_utf8)) {
            char path[128];
            char shortname[MODULE_MAX_NAME];
            int len = strlen((char*)name_utf8);
            int copy_len = len - 4;

            if (copy_len > (int)sizeof(shortname) - 1) {
                copy_len = sizeof(shortname) - 1;
            }
            // Derive the module display name by trimming the `.sys` suffix.
            strncpy(shortname, (char*)name_utf8, copy_len);
            shortname[copy_len] = '\0';
            sprintf(path, "/system/%s", (char*)name_utf8);
            // Probe the bundle and let the loader decide whether it is valid.
            module_probe_file(path, shortname);
        }
        kfree((Address)name_utf8);
    }

    vfs_dir_close(&dir);
}

/**
 * Run idle callbacks for every ready module that registered one.
 *
 * Args:
 *   None.
 *
 * Behavior:
 *   Executes only on CPU0, computes the current module timebase, suppresses
 *   duplicate same-tick invocations, and logs the first successful idle run.
 *
 * Returns:
 *   Nothing.
 */
void module_run_idle_loops(void) {
    ULong tick_ms;

    // Keep idle hooks serialized on CPU0 until the module ABI grows SMP semantics.
    if (task_cpu_index() != 0) {
        return;
    }

    tick_ms = module_api_ticks_ms();
    for (UInt index = 0; index < MODULE_MAX_COUNT; index++) {
        KernelModuleRuntime* runtime = &module_runtimes[index];

        // Ignore unused slots and modules that did not provide idle work.
        if (!runtime->used || !runtime->ready || !runtime->idle_fn) {
            continue;
        }
        // Avoid running a module more than once for the same scheduler tick.
        if (runtime->idle_tick_valid && runtime->last_idle_tick == tick_ms) {
            continue;
        }

        runtime->last_idle_tick = tick_ms;
        runtime->idle_tick_valid = true;
        if (!runtime->idle_logged) {
            log_info("Kernel module idle loop is running: %s", runtime->name);
            runtime->idle_logged = true;
        }
        // Call the module's idle hook with the shared millisecond clock.
        runtime->idle_fn(tick_ms);
    }
}

/**
 * Fetch resolved runtime metadata for one loaded module.
 *
 * Args:
 *   module_name: Name of the loaded module.
 *   out: Output structure receiving runtime addresses and sizes.
 *
 * Behavior:
 *   Copies stable, read-only runtime facts out of the loader's private slot so
 *   diagnostics code can inspect them without depending on internal structs.
 *
 * Returns:
 *   `0` on success, or `-1` when the module or output pointer is invalid.
 */
int module_runtime_get(const char* module_name, KernelModuleRuntimeInfo* out) {
    KernelModuleRuntime* runtime = module_find_runtime(module_name);

    if (!runtime || !out) {
        return -1;
    }

    memzero((Address)out, sizeof(*out));
    out->ready = runtime->ready;
    out->base_va = runtime->base_va;
    out->load_bias = runtime->load_bias;
    out->init_va = (Address)runtime->init_fn;
    out->shutdown_va = (Address)runtime->shutdown_fn;
    out->idle_va = (Address)runtime->idle_fn;
    out->image_size = runtime->image_size;
    out->bss_size = runtime->bss_size;
    out->export_count = runtime->export_count;
    return 0;
}

/**
 * Count the resolved exports currently published by one ready module.
 *
 * Args:
 *   module_name: Name of the loaded module.
 *
 * Behavior:
 *   Looks up the ready runtime slot and reports how many exports were resolved
 *   during load.
 *
 * Returns:
 *   Export count for a ready module, or `0` when the module is absent.
 */
unsigned int module_export_count(const char* module_name) {
    KernelModuleRuntime* runtime = module_find_runtime(module_name);

    if (!runtime) {
        return 0;
    }
    return runtime->export_count;
}

/**
 * Fetch one resolved export entry from a ready module.
 *
 * Args:
 *   module_name: Name of the loaded module.
 *   index: Zero-based export index.
 *   out: Output buffer that receives the export name and callable address.
 *
 * Behavior:
 *   Copies one export descriptor out of the runtime slot without exposing the
 *   internal runtime structure to outside callers.
 *
 * Returns:
 *   `0` on success, or `-1` when the module, index, or output pointer is
 *   invalid.
 */
int module_export_get(const char* module_name, unsigned int index, KernelModuleExportInfo* out) {
    KernelModuleRuntime* runtime = module_find_runtime(module_name);

    if (!runtime || !out || index >= runtime->export_count) {
        return -1;
    }

    memzero((Address)out, sizeof(*out));
    strncpy(out->name, runtime->exports[index].name, sizeof(out->name) - 1);
    out->address = (Address)runtime->exports[index].fn;
    return 0;
}

/**
 * Invoke a named export on a loaded kernel module.
 *
 * Args:
 *   module_name: Name of the loaded module.
 *   export_name: Export name declared in the bundle header.
 *   a: First integer argument passed to the export.
 *   b: Second integer argument passed to the export.
 *   result: Output pointer receiving the export return value.
 *
 * Behavior:
 *   Looks up the ready runtime, scans its resolved export table, and dispatches
 *   the two-argument call if the export exists.
 *
 * Returns:
 *   `0` on success, or `-1` when the module/export/result pointer is invalid.
 */
int module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result) {
    KernelModuleRuntime* runtime;
    if (!module_name || module_name[0] == '\0' || !export_name || export_name[0] == '\0' || !result) {
        return -1;
    }

    if (strcmp(module_name, "kernel") == 0) {
        for (UInt index = 0; index < sizeof(kernel_exports) / sizeof(kernel_exports[0]); index++) {
            if (strcmp(kernel_exports[index].name, export_name) != 0) {
                continue;
            }
            *result = kernel_exports[index].fn(a, b);
            return 0;
        }
        return -1;
    }

    runtime = module_find_runtime(module_name);
    if (!runtime) {
        return -1;
    }

    // Match the exported public name, not the original symbol name from the ELF.
    for (UInt index = 0; index < runtime->export_count; index++) {
        if (strncmp(runtime->exports[index].name, export_name, sizeof(runtime->exports[index].name)) != 0) {
            continue;
        }
        if (!runtime->exports[index].fn) {
            return -1;
        }
        // Dispatch the generic two-argument export ABI and return its result to the caller.
        *result = runtime->exports[index].fn(a, b);
        return 0;
    }

    return -1;
}
