#include "fs/fat32.h"
#include "boot-handoff.h"
#include "irq.h"
#include "log.h"
#include "memory.h"
#include "printf.h"
#include "ros.h"
#include "device/sd.h"
#include "device/uart0.h"
#include "roskrnl_format.h"

#define BOOTLOADER_KERNEL_PATH "/roskrnl"
#define BOOTLOADER_COPY_CHUNK 4096U
#define BOOTLOADER_PROGRESS_STEP (64U * 1024U)

static unsigned char bootloader_printf_buffer[1024];
static unsigned char bootloader_copy_buffer[BOOTLOADER_COPY_CHUNK];
static BootFs boot_filesystem;

extern void secondary_program_boot_entry(unsigned int cpu_index);
extern void secondary_publish_boot_entries(void);

static RosBootHandoff* bootloader_handoff(void) {
    return (RosBootHandoff*)mem_phys_to_virt((PhysAddr)ROS_BOOT_HANDOFF_PHYS_ADDR);
}

// Emit a small bootloader-prefixed progress record so early boot is visible on UART.
static void bootloader_trace(const char* message) {
    kprint("[bootloader] %s\r\n", message); // write a readable UART breadcrumb
}

// Emit a formatted bootloader progress record for the current stage.
static void bootloader_tracef(const char* format, ...) {
    va_list args;

    va_start(args, format); // start the variadic formatter
    console_lock(); // keep the whole progress line together on UART
    kprint("[bootloader] "); // add a consistent loader prefix
    tfp_format(0, uart0_putc, format, args); // print the stage details directly to UART
    kprint("\r\n"); // terminate the UART line cleanly
    console_unlock(); // release the UART lock after the full line is written
    va_end(args); // finish the variadic formatter
}

// Stop the machine in place after logging the failure reason.
static void bootloader_panic(const char* message) {
    log_error("bootloader: %s", message);
    while (1) {
    }
}

// Bring up only the minimal hardware needed to print logs and read the SD card.
static void bootloader_init_io(void) {
    uart0_init();
    init_printf(bootloader_printf_buffer, uart0_putc);
    disable_irq();
    bootloader_trace("UART console ready");

    if (sd_init() != SD_OK) {
        bootloader_panic("SD card initialization failed");
    }
    bootloader_trace("SD card controller ready");
}

// Validate the custom ROSKRNL header before the loader touches any destination memory.
static int bootloader_validate_header(const RosKernelHeader* header, unsigned int file_size) {
    ULong image_end;

    if (!header) {
        return -1;
    }
    if (header->magic[0] == 0x7F &&
        header->magic[1] == 'E' &&
        header->magic[2] == 'L' &&
        header->magic[3] == 'F') {
        log_error("bootloader: %s is ELF, expected custom ROSKRNL format", BOOTLOADER_KERNEL_PATH);
        return -1;
    }
    if (header->magic[0] != ROSKRNL_MAGIC_0 ||
        header->magic[1] != ROSKRNL_MAGIC_1 ||
        header->magic[2] != ROSKRNL_MAGIC_2 ||
        header->magic[3] != ROSKRNL_MAGIC_3 ||
        header->magic[4] != ROSKRNL_MAGIC_4 ||
        header->magic[5] != ROSKRNL_MAGIC_5 ||
        header->magic[6] != ROSKRNL_MAGIC_6 ||
        header->magic[7] != ROSKRNL_MAGIC_7) {
        log_error("bootloader: %s has invalid magic", BOOTLOADER_KERNEL_PATH);
        return -1;
    }
    if (header->version != ROSKRNL_VERSION || header->machine != ROSKRNL_MACHINE_AARCH64) {
        log_error("bootloader: %s has unsupported version or machine", BOOTLOADER_KERNEL_PATH);
        return -1;
    }
    if (header->header_size < sizeof(RosKernelHeader) || header->payload_offset < header->header_size) {
        log_error("bootloader: %s has invalid header layout", BOOTLOADER_KERNEL_PATH);
        return -1;
    }
    if (header->payload_size > header->memory_size) {
        log_error("bootloader: %s payload exceeds memory span", BOOTLOADER_KERNEL_PATH);
        return -1;
    }
    if (!mem_is_page_aligned(header->load_address)) {
        log_error("bootloader: %s load address is not page aligned", BOOTLOADER_KERNEL_PATH);
        return -1;
    }
    if (header->payload_offset + header->payload_size > (ULong)file_size) {
        log_error("bootloader: %s payload extends past end of file", BOOTLOADER_KERNEL_PATH);
        return -1;
    }
    if (header->memory_size == 0) {
        log_error("bootloader: %s memory span is empty", BOOTLOADER_KERNEL_PATH);
        return -1;
    }
    image_end = header->load_address + header->memory_size;
    if (image_end < header->load_address) {
        log_error("bootloader: %s load span overflows", BOOTLOADER_KERNEL_PATH);
        return -1;
    }
    if (image_end > LOW_MEMORY_CEILING) {
        log_error("bootloader: %s load span 0x%lX-0x%lX exceeds reserved low memory", BOOTLOADER_KERNEL_PATH, header->load_address, image_end);
        return -1;
    }
    if (header->entry_point < header->image_base || header->entry_point >= (header->image_base + header->memory_size)) {
        log_error("bootloader: %s entry 0x%lX is outside image base 0x%lX", BOOTLOADER_KERNEL_PATH, header->entry_point, header->image_base);
        return -1;
    }
    return 0;
}

static void bootloader_zero_memory(UByte* begin, ULong size) {
    while (size-- > 0) {
        *begin++ = 0;
    }
}

static void bootloader_publish_handoff(const RosKernelHeader* header) {
    RosBootHandoff* handoff = bootloader_handoff();

    if (!handoff || !header) {
        return;
    }

    bootloader_zero_memory((UByte*)handoff, sizeof(*handoff));
    handoff->magic = ROS_BOOT_HANDOFF_MAGIC;
    handoff->version = ROS_BOOT_HANDOFF_VERSION;
    handoff->size = sizeof(*handoff);
    handoff->flags = ROS_BOOT_HANDOFF_FLAG_UART0_READY | ROS_BOOT_HANDOFF_FLAG_SD_READY;
    handoff->kernel_load_address = header->load_address;
    handoff->kernel_image_base = header->image_base;
    handoff->kernel_entry_point = header->entry_point;
    handoff->secondary_program_boot_entry_fn = (ULong)secondary_program_boot_entry;
    handoff->secondary_publish_boot_entries_fn = (ULong)secondary_publish_boot_entries;
    handoff->secondary_entry_point = 0;
    sd_fill_handoff(handoff);
    asm volatile("dsb ishst; isb" ::: "memory");
}

// Log the parsed ROSKRNL fields so boot failures can be traced from UART output.
static void bootloader_trace_header(const RosKernelHeader* header) {
    if (!header) {
        return;
    }

    bootloader_tracef("ROSKRNL header version=%u machine=%u flags=0x%X", header->version, header->machine, header->flags); // show the basic image identity
    bootloader_tracef("ROSKRNL load=0x%lX image_base=0x%lX entry=0x%lX", header->load_address, header->image_base, header->entry_point); // show the load and execution addresses
    bootloader_tracef("ROSKRNL payload=%lu bytes memory=%lu bytes align=0x%lX", header->payload_size, header->memory_size, header->alignment); // show the copied and zero-filled spans
}

// Copy the ROSKRNL payload into its final physical destination and clear any BSS tail.
static int bootloader_copy_kernel(BootFsFile* file, const RosKernelHeader* header) {
    ULong copied = 0;
    ULong next_progress = BOOTLOADER_PROGRESS_STEP;
    UByte* destination;

    if (!file || !header) {
        return -1;
    }
    if (bootfs_seek(&boot_filesystem, file, (UInt)header->payload_offset) != 0) {
        log_error("bootloader: unable to seek to ROSKRNL payload");
        return -1;
    }

    destination = (UByte*)mem_phys_to_virt((PhysAddr)header->load_address);
    while (copied < header->payload_size) {
        unsigned int chunk = (unsigned int)(header->payload_size - copied);
        int read_size;

        if (chunk > BOOTLOADER_COPY_CHUNK) {
            chunk = BOOTLOADER_COPY_CHUNK;
        }

        read_size = bootfs_read(&boot_filesystem, file, bootloader_copy_buffer, chunk);
        if (read_size != (int)chunk) {
            log_error("bootloader: short read while copying ROSKRNL (%d != %d)", read_size, chunk);
            return -1;
        }

        memmove(destination + copied, bootloader_copy_buffer, chunk); // stage the next chunk into the kernel load window
        copied += chunk; // advance the total bytes copied so far
        if (copied >= next_progress || copied == header->payload_size) {
            bootloader_tracef("copy progress %lu/%lu bytes", copied, header->payload_size); // show forward progress while copying larger kernels
            next_progress += BOOTLOADER_PROGRESS_STEP; // move the next progress watermark forward
        }
    }

    if (header->memory_size > header->payload_size) {
        bootloader_zero_memory(destination + header->payload_size, header->memory_size - header->payload_size); // zero-fill the in-memory tail for BSS
        bootloader_tracef("zero filled %lu bytes", header->memory_size - header->payload_size); // show the BSS clear span
    }

    asm volatile("dsb ish; ic iallu; dsb ish; isb" ::: "memory"); // publish data writes and invalidate stale instruction cache lines
    return 0;
}

// Branch into the already copied higher-half kernel entry point.
static void bootloader_jump_to_kernel(Address entry_point) {
    void (*entry)(void) = (void (*)(void))entry_point;

    log_info("bootloader: transferring control to ROSKRNL at 0x%lX", entry_point);
    entry();
}

// Load the custom ROSKRNL image from the FAT32 root and transfer control to it.
void kernel_main() {
    BootFsFile kernel_file;
    RosKernelHeader header;

    bootloader_init_io();
    if (bootfs_init(&boot_filesystem) != 0) {
        bootloader_panic("unable to mount FAT32 boot volume");
    }
    bootloader_trace("FAT32 boot volume ready");

    bootloader_trace("opening /roskrnl from the FAT32 root");
    if (bootfs_open(&boot_filesystem, BOOTLOADER_KERNEL_PATH, &kernel_file) != 0) {
        bootloader_panic("unable to open /roskrnl");
    }

    bootloader_tracef("%s size=%u bytes", BOOTLOADER_KERNEL_PATH, kernel_file.size); // show the on-disk image size before validation
    if (kernel_file.size < (unsigned int)sizeof(RosKernelHeader)) {
        bootloader_panic("ROSKRNL file is smaller than its header");
    }
    bootloader_trace("reading ROSKRNL header");
    if (bootfs_read(&boot_filesystem, &kernel_file, &header, sizeof(header)) != (int)sizeof(header)) {
        bootloader_panic("unable to read ROSKRNL header");
    }
    if (bootloader_validate_header(&header, kernel_file.size) != 0) {
        bootloader_panic("ROSKRNL header validation failed");
    }
    bootloader_trace_header(&header);

    bootloader_trace("copying ROSKRNL payload into memory");
    if (bootloader_copy_kernel(&kernel_file, &header) != 0) {
        bootloader_panic("unable to copy ROSKRNL payload");
    }

    bootloader_publish_handoff(&header);
    bootloader_trace("jumping to loaded ROSKRNL entry point");
    bootloader_jump_to_kernel((Address)header.entry_point);

    while (1) {
    }
}
