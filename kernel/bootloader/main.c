#include "block_device.h"
#include "boot_mmu.h"
#include "bootloader.h"
#include "console.h"
#include "fat32.h"
#include "kernel_format.h"

#ifndef BOOT_TARGET_BOARD_NAME
#define BOOT_TARGET_BOARD_NAME "virt"
#endif

#ifndef BOOT_TARGET_KERNEL_PATH
#define BOOT_TARGET_KERNEL_PATH "/kernel"
#endif

#ifndef BOOT_TARGET_LOAD_PHYS_BASE
#define BOOT_TARGET_LOAD_PHYS_BASE 0x40100000ULL
#endif

#define BOOT_COPY_CHUNK 4096U

static U8 g_copy_buffer[BOOT_COPY_CHUNK];
static BootFs g_boot_filesystem;

static void boot_trace(const char* message) {
    boot_console_puts("[bootloader] ");
    boot_console_puts(message);
    boot_console_puts("\n");
}

static void boot_panic(const char* message) {
    boot_trace(message);

    for (;;) {
        __asm__ volatile("wfe");
    }
}

static bool boot_header_magic_valid(const KernelImageHeader* header) {
    return (header != NULL) &&
        (header->magic[0] == KERNEL_IMAGE_MAGIC_0) &&
        (header->magic[1] == KERNEL_IMAGE_MAGIC_1) &&
        (header->magic[2] == KERNEL_IMAGE_MAGIC_2) &&
        (header->magic[3] == KERNEL_IMAGE_MAGIC_3) &&
        (header->magic[4] == KERNEL_IMAGE_MAGIC_4) &&
        (header->magic[5] == KERNEL_IMAGE_MAGIC_5) &&
        (header->magic[6] == KERNEL_IMAGE_MAGIC_6) &&
        (header->magic[7] == KERNEL_IMAGE_MAGIC_7);
}

static Status boot_validate_header(const KernelImageHeader* header, U32 file_size) {
    U64 image_end;

    if (header == NULL) {
        return StatusInvalidArgument;
    }
    if (!boot_header_magic_valid(header)) {
        return StatusFault;
    }
    if ((header->version != KERNEL_IMAGE_VERSION) || (header->machine != KERNEL_IMAGE_MACHINE_AARCH64)) {
        return StatusNotSupported;
    }
    if ((header->header_size < sizeof(KernelImageHeader)) || (header->payload_offset < header->header_size)) {
        return StatusFault;
    }
    if (header->payload_size > header->memory_size) {
        return StatusFault;
    }
    if ((header->payload_offset + header->payload_size) > (U64)file_size) {
        return StatusFault;
    }
    if ((header->memory_size == 0U) || ((header->load_address & 0xfffU) != 0U)) {
        return StatusFault;
    }
    if (header->load_address != BOOT_TARGET_LOAD_PHYS_BASE) {
        return StatusFault;
    }

    image_end = header->load_address + header->memory_size;
    if ((image_end < header->load_address) ||
        (header->entry_point < header->image_base) ||
        (header->entry_point >= (header->image_base + header->memory_size))) {
        return StatusFault;
    }

    return StatusOK;
}

static const BootTarget g_boot_target = {
    .board_name = BOOT_TARGET_BOARD_NAME,
    .kernel_path = BOOT_TARGET_KERNEL_PATH,
    .load_phys_base = BOOT_TARGET_LOAD_PHYS_BASE,
};

const BootTarget* boot_target(void) {
    return &g_boot_target;
}

static Status boot_copy_kernel(BootFs* filesystem, BootFsFile* file, const KernelImageHeader* header) {
    U8* destination;
    U64 copied = 0;
    SSize read_result;

    if ((filesystem == NULL) || (file == NULL) || (header == NULL)) {
        return StatusInvalidArgument;
    }
    if (bootfs_seek(filesystem, file, (U32)header->payload_offset) != StatusOK) {
        return StatusIoError;
    }

    destination = (U8*)(Uptr)header->load_address;
    while (copied < header->payload_size) {
        U32 chunk = (U32)(header->payload_size - copied);

        if (chunk > BOOT_COPY_CHUNK) {
            chunk = BOOT_COPY_CHUNK;
        }

        read_result = bootfs_read(filesystem, file, g_copy_buffer, chunk);
        if (read_result != (SSize)chunk) {
            return StatusIoError;
        }

        memcopy(destination + copied, g_copy_buffer, chunk);
        copied += chunk;
    }

    if (header->memory_size > header->payload_size) {
        memzero(destination + header->payload_size, header->memory_size - header->payload_size);
    }

    __asm__ volatile("dsb sy\n"
        "ic iallu\n"
        "dsb sy\n"
        "isb\n"
        ::: "memory");
    return StatusOK;
}

static void boot_jump_to_kernel(PhysAddr entry_point) {
    void (*entry)(void) = (void (*)(void))(Uptr)entry_point;

    entry();
}

void boot_main(void) {
    BootFsFile kernel_file;
    KernelImageHeader header;
    Status status;

    status = boot_console_init();
    if (status != StatusOK) {
        boot_panic("console init failed");
    }
    boot_trace("UART console ready");

    status = boot_block_init();
    if (status != StatusOK) {
        boot_panic("block device init failed");
    }
    boot_trace("block device ready");

    boot_trace("mounting FAT32 boot volume");
    status = bootfs_init(&g_boot_filesystem);
    if (status != StatusOK) {
        boot_panic("unable to mount FAT32 boot volume");
    }
    boot_trace("FAT32 boot volume ready");

    status = bootfs_open(&g_boot_filesystem, boot_target()->kernel_path, &kernel_file);
    if (status != StatusOK) {
        boot_panic("unable to open kernel image");
    }

    if (kernel_file.size < sizeof(KernelImageHeader)) {
        boot_panic("kernel image is smaller than its header");
    }

    if (bootfs_read(&g_boot_filesystem, &kernel_file, &header, sizeof(header)) != (SSize)sizeof(header)) {
        boot_panic("unable to read kernel image header");
    }

    status = boot_validate_header(&header, kernel_file.size);
    if (status != StatusOK) {
        boot_panic("kernel image header validation failed");
    }

    status = boot_copy_kernel(&g_boot_filesystem, &kernel_file, &header);
    if (status != StatusOK) {
        boot_panic("unable to copy kernel image payload");
    }

    boot_trace("enabling MMU for higher-half kernel handoff");
    boot_enable_mmu();

    boot_trace("jumping to loaded kernel entry point");
    boot_jump_to_kernel((PhysAddr)header.entry_point);

    for (;;) {
        __asm__ volatile("wfe");
    }
}
