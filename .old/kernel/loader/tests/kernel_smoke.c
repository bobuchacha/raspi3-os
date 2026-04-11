/*
 * kernel_smoke.c
 *
 * Kernel-side smoke coverage for the modular loader.
 *
 * This test avoids depending on the userspace loader bridge. Instead it builds
 * a minimal in-memory DLL image, parses it with the real loader, maps it into a
 * kmalloc-backed region through a test VM shim, registers it, resolves one
 * export, and unloads it again. That covers the core loader pipeline while
 * keeping the test self-contained inside a kernel thread.
 */
#include "../../include/ldr_internal.h"

#include "../../include/log.h"

#include <string.h>

 /* Keep this smoke self-contained so it does not need ros.h-backed headers. */
#define LOADER_TEST_PAGE_SIZE 0x1000UL

extern unsigned long kmalloc(int bytes);
extern void kfree(unsigned long ptr);

typedef struct LoaderKernelVmRegionStruct {
    Address base;
    Size size;
} LoaderKernelVmRegion;

/* Single active mapping is enough for this smoke suite's one-module flow. */
static LoaderKernelVmRegion g_loader_test_region = { 0, 0 };

/* Return uniform failure codes and emit the step that broke. */
static int loader_kernel_expect_true(int condition, const char* step) {
    if (!condition) {
        log_fail("loader smoke: assertion failed: %s", step);
        return -1;
    }

    return 0;
}

/* Return uniform failure codes and emit the result enum once. */
static int loader_kernel_expect_ok(LDR_RESULT result, const char* step) {
    if (result != LDR_OK) {
        log_fail("loader smoke: %s failed (result=%d)", step, (int)result);
        return -1;
    }

    return 0;
}

/* Reserve one contiguous kernel heap block as the module image backing store. */
static Int loader_kernel_vm_reserve(Address preferred_base, Size size, Flags flags, Address* out_base) {
    Address allocation;

    (void)preferred_base;
    (void)flags;
    if (!out_base || size == 0 || g_loader_test_region.base != 0) {
        return -1;
    }

    allocation = kmalloc((int)size);
    if (!allocation) {
        return -1;
    }

    g_loader_test_region.base = allocation;
    g_loader_test_region.size = size;
    *out_base = allocation;
    return 0;
}

/* Zero the reserved range once so later section copies start from a clean image. */
static Int loader_kernel_vm_commit(Address base_address, Size size, Flags flags) {
    (void)flags;
    if (base_address != g_loader_test_region.base || size > g_loader_test_region.size) {
        return -1;
    }

    memset((void*)base_address, 0, (size_t)size);
    return 0;
}

/* The smoke test does not enforce page permissions, so protect is a no-op. */
static Int loader_kernel_vm_protect(Address base_address, Size size, Flags flags) {
    (void)base_address;
    (void)size;
    (void)flags;

    return 0;
}

/* Release the kmalloc-backed image once the module unload path completes. */
static Int loader_kernel_vm_release(Address base_address, Size size) {
    if (base_address != g_loader_test_region.base || size != g_loader_test_region.size) {
        return -1;
    }

    kfree(base_address);
    g_loader_test_region.base = 0;
    g_loader_test_region.size = 0;
    return 0;
}

/* Use the normal kernel heap for loader-owned metadata. */
static Pointer loader_kernel_heap_alloc(Size size) {
    return (Pointer)kmalloc((int)(size ? size : 1));
}

/* Return loader-owned metadata to the normal kernel heap. */
static void loader_kernel_heap_free(Pointer memory) {
    if (memory) {
        kfree((Address)memory);
    }
}

/* This in-memory smoke does not read files, so file callbacks reject use. */
static Int loader_kernel_vfs_open(CONST char* path, Flags flags, PLDR_FILEHANDLE out_file) {
    (void)path;
    (void)flags;
    (void)out_file;
    return -1;
}

static Int loader_kernel_vfs_read_at(LDR_FILEHANDLE file_handle, ULong offset, Pointer buffer, Size size, Size* out_read) {
    (void)file_handle;
    (void)offset;
    (void)buffer;
    (void)size;
    (void)out_read;
    return -1;
}

static Int loader_kernel_vfs_size(LDR_FILEHANDLE file_handle, ULong* out_size) {
    (void)file_handle;
    (void)out_size;
    return -1;
}

static Int loader_kernel_vfs_close(LDR_FILEHANDLE file_handle) {
    (void)file_handle;
    return -1;
}

/* Graph-lock callbacks stay trivial because the smoke runs on one kernel thread. */
static Int loader_kernel_lock_create(PLDR_LOCKHANDLE out_lock) {
    if (!out_lock) {
        return -1;
    }

    out_lock->opaque = 1;
    return 0;
}

static Int loader_kernel_lock_acquire(LDR_LOCKHANDLE lock_handle) {
    (void)lock_handle;
    return 0;
}

static Int loader_kernel_lock_release(LDR_LOCKHANDLE lock_handle) {
    (void)lock_handle;
    return 0;
}

/* Small sink used by tfp_format to append into a stack buffer. */
static void loader_kernel_log_putc(void* context, char ch) {
    char** cursor = (char**)context;

    if (!cursor || !*cursor) {
        return;
    }

    **cursor = ch;
    (*cursor)++;
}

/* Forward loader diagnostics into the kernel log. */
static void loader_kernel_log_printf(Int level, CONST char* format, ...) {
    char message[256];
    char* cursor = message;
    va_list args;

    (void)level;
    memset(message, 0, sizeof(message));
    va_start(args, format);
    tfp_format(&cursor, loader_kernel_log_putc, (char*)format, args);
    va_end(args);
    *cursor = '\0';
    log_info("loader smoke: %s", message);
}

/* Minimal callback table sufficient for parse, map, register, and unload. */
static const LDR_KERNELAPI g_loader_kernel_test_api = {
    .vmReserve = loader_kernel_vm_reserve,
    .vmCommit = loader_kernel_vm_commit,
    .vmProtect = loader_kernel_vm_protect,
    .vmRelease = loader_kernel_vm_release,
    .heapAlloc = loader_kernel_heap_alloc,
    .heapFree = loader_kernel_heap_free,
    .vfsOpen = loader_kernel_vfs_open,
    .vfsReadAt = loader_kernel_vfs_read_at,
    .vfsSize = loader_kernel_vfs_size,
    .vfsClose = loader_kernel_vfs_close,
    .procCreate = NULL,
    .procSetEntry = NULL,
    .procAddModule = NULL,
    .procStart = NULL,
    .lockCreate = loader_kernel_lock_create,
    .lockAcquire = loader_kernel_lock_acquire,
    .lockRelease = loader_kernel_lock_release,
    .logPrintf = loader_kernel_log_printf,
};

/*
 * Build a tiny valid DLL image:
 * - one writable/readable section at RVA 0x200,
 * - one exported symbol named `hello_value`,
 * - one 64-bit payload inside the section.
 */
static Size loader_kernel_build_test_image(UByte* buffer, Size capacity, ULong payload_value) {
    static const char section_name[] = "text";
    static const char export_name[] = "hello_value";
    const UInt section_name_offset = 0u;
    const UInt export_name_offset = (UInt)sizeof(section_name);
    const UInt string_table_size = (UInt)(sizeof(section_name) + sizeof(export_name));
    const ULong section_rva = 0x200u;
    const ULong image_size = 0x1000u;
    const ULong payload_size = (ULong)sizeof(payload_value);
    const Size header_offset = 0u;
    const Size section_offset = header_offset + sizeof(LDR_IMAGEHEADER);
    const Size export_offset = section_offset + sizeof(LDR_SECTIONDESC);
    const Size string_offset = export_offset + sizeof(LDR_EXPORTDESC);
    const Size payload_offset = string_offset + string_table_size;
    LDR_IMAGEHEADER* header;
    LDR_SECTIONDESC* section;
    LDR_EXPORTDESC* export_desc;

    if (!buffer || capacity < payload_offset + payload_size) {
        return 0;
    }

    memset(buffer, 0, (size_t)capacity);
    header = (LDR_IMAGEHEADER*)(buffer + header_offset);
    section = (LDR_SECTIONDESC*)(buffer + section_offset);
    export_desc = (LDR_EXPORTDESC*)(buffer + export_offset);

    header->magic = LDR_IMAGE_MAGIC;
    header->version = (UWord)LDR_IMAGE_VERSION;
    header->kind = (UWord)LDR_IMAGE_DLL;
    header->imageSize = image_size;
    header->preferredBase = 0u;
    header->entryRva = 0u;
    header->sectionCount = 1u;
    header->relocCount = 0u;
    header->importCount = 0u;
    header->exportCount = 1u;
    header->stringTableSize = string_table_size;

    section->nameOffset = section_name_offset;
    section->flags = LDR_SEC_READ | LDR_SEC_WRITE;
    section->rva = section_rva;
    section->fileOffset = (ULong)payload_offset;
    section->fileSize = payload_size;
    section->virtualSize = payload_size;

    export_desc->symbolNameOffset = export_name_offset;
    export_desc->symbolRva = section_rva;

    memmove((void*)(buffer + string_offset), section_name, sizeof(section_name));
    memmove((void*)(buffer + string_offset + sizeof(section_name)), export_name, sizeof(export_name));
    memmove((void*)(buffer + payload_offset), &payload_value, sizeof(payload_value));
    return payload_offset + payload_size;
}

int loader_kernel_smoke_test_main(void) {
    static const char module_path[] = "/kernel/tests/hello_value.dll";
    static const ULong payload_value = 0x1122334455667788UL;
    UByte image_buffer[256];
    Size image_size;
    PLDR_CONTEXT context = NULL;
    PLDR_MODULE module = NULL;
    PLDR_MODULE found = NULL;
    Address export_address = 0;

    image_size = loader_kernel_build_test_image(image_buffer, sizeof(image_buffer), payload_value);
    if (loader_kernel_expect_true(image_size != 0, "test image should fit in the stack buffer") != 0) {
        return -1;
    }

    if (loader_kernel_expect_ok(ldr_init(&g_loader_kernel_test_api, &context), "ldr_init") != 0) {
        return -1;
    }
    if (loader_kernel_expect_ok(ldr_parse_image(context, image_buffer, image_size, module_path, &module), "ldr_parse_image") != 0) {
        (void)ldr_shutdown(context);
        return -1;
    }

    /* Force the loader down its kernel-resident mapping path for this smoke. */
    module->isKernelModule = true;

    if (loader_kernel_expect_ok(ldr_vm_map_image(context, module), "ldr_vm_map_image") != 0) {
        (void)ldr_shutdown(context);
        return -1;
    }
    if (loader_kernel_expect_ok(ldr_sections_load(context, module, image_buffer, image_size), "ldr_sections_load") != 0) {
        (void)ldr_shutdown(context);
        return -1;
    }
    if (loader_kernel_expect_ok(ldr_module_register(context, module), "ldr_module_register") != 0) {
        (void)ldr_shutdown(context);
        return -1;
    }
    if (loader_kernel_expect_ok(ldr_find_module_by_path(context, module_path, &found), "ldr_find_module_by_path") != 0) {
        (void)ldr_shutdown(context);
        return -1;
    }
    if (loader_kernel_expect_true(found == module, "find_module_by_path should return the registered module") != 0) {
        (void)ldr_shutdown(context);
        return -1;
    }
    if (loader_kernel_expect_ok(ldr_find_export(context, module, "hello_value", &export_address), "ldr_find_export") != 0) {
        (void)ldr_shutdown(context);
        return -1;
    }
    if (loader_kernel_expect_true(*(ULong*)export_address == payload_value, "export address should point at the loaded payload") != 0) {
        (void)ldr_shutdown(context);
        return -1;
    }

    log_info("loader smoke: module=%s export=0x%lX value=0x%lX",
        module_path,
        export_address,
        *(ULong*)export_address);

    if (loader_kernel_expect_ok(ldr_unload_module(context, module), "ldr_unload_module") != 0) {
        (void)ldr_shutdown(context);
        return -1;
    }
    if (loader_kernel_expect_ok(ldr_shutdown(context), "ldr_shutdown") != 0) {
        return -1;
    }
    if (loader_kernel_expect_true(g_loader_test_region.base == 0, "vm region should be released on unload") != 0) {
        return -1;
    }

    return 0;
}