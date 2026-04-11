#include "boot-handoff.h"
#include "device/virt.h"
#include "device/uart0.h"
#include "irq.h"
#include "log.h"
#include "memory.h"
#include "printf.h"
#include "ros.h"
#include "roskrnl_format.h"

#define BOOTLOADER_FWCFG_KERNEL_PATH "opt/org.raspi3os/roskrnl"
#define BOOTLOADER_COPY_CHUNK 4096U
#define BOOTLOADER_PROGRESS_STEP (64U * 1024U)

#define VIRT_FW_CFG_BASE 0x09020000UL
#define VIRT_FW_CFG_DATA ((volatile unsigned char*)(VIRT_FW_CFG_BASE + 0x00U))
#define VIRT_FW_CFG_CTL ((volatile unsigned short*)(VIRT_FW_CFG_BASE + 0x08U))

#define FW_CFG_SIGNATURE 0x0000U
#define FW_CFG_ID 0x0001U
#define FW_CFG_FILE_DIR 0x0019U

typedef struct __attribute__((packed)) VirtFwCfgFileStruct {
    UInt size_be;
    unsigned short select_be;
    unsigned short reserved;
    char name[56];
} VirtFwCfgFile;

static unsigned char bootloader_printf_buffer[1024];
static unsigned char bootloader_copy_buffer[BOOTLOADER_COPY_CHUNK];

extern void secondary_program_boot_entry(unsigned int cpu_index);
extern void secondary_publish_boot_entries(void);

static RosBootHandoff* bootloader_handoff(void) {
    return (RosBootHandoff*)mem_phys_to_virt((PhysAddr)ROS_BOOT_HANDOFF_PHYS_ADDR);
}

static UInt bootloader_bswap32(UInt value) {
    return (UInt)__builtin_bswap32(value);
}

static unsigned short bootloader_bswap16(unsigned short value) {
    return (unsigned short)__builtin_bswap16(value);
}

static void bootloader_trace(const char* message) {
    kprint("[bootloader] %s\r\n", message);
}

static void bootloader_tracef(const char* format, ...) {
    va_list args;

    va_start(args, format);
    console_lock();
    kprint("[bootloader] ");
    tfp_format(0, uart0_putc, format, args);
    kprint("\r\n");
    console_unlock();
    va_end(args);
}

static void bootloader_panic(const char* message) {
    log_error("bootloader: %s", message);
    while (1) {
    }
}

static void bootloader_zero_memory(UByte* begin, ULong size) {
    while (size-- > 0UL) {
        *begin++ = 0;
    }
}

static void bootloader_fwcfg_select(unsigned short selector) {
    *VIRT_FW_CFG_CTL = bootloader_bswap16(selector);
}

static void bootloader_fwcfg_read_current(void* buffer, UInt length) {
    unsigned char* out = (unsigned char*)buffer;

    while (length-- > 0U) {
        *out++ = *VIRT_FW_CFG_DATA;
    }
}

static void bootloader_fwcfg_read(unsigned short selector, void* buffer, UInt length) {
    bootloader_fwcfg_select(selector);
    bootloader_fwcfg_read_current(buffer, length);
}

static void bootloader_init_io(void) {
    char signature[4];
    UInt fwcfg_id = 0;

    uart0_init();
    init_printf(bootloader_printf_buffer, uart0_putc);
    disable_irq();
    bootloader_trace("UART console ready");

    bootloader_fwcfg_read(FW_CFG_SIGNATURE, signature, sizeof(signature));
    bootloader_fwcfg_read(FW_CFG_ID, &fwcfg_id, sizeof(fwcfg_id));
    if (signature[0] != 'Q' || signature[1] != 'E' || signature[2] != 'M' || signature[3] != 'U') {
        bootloader_panic("fw_cfg signature missing on virt machine");
    }

    bootloader_tracef("fw_cfg ready id=0x%X", fwcfg_id);
}

static int bootloader_validate_header(const RosKernelHeader* header, unsigned int file_size) {
    ULong image_end;
    ULong reserved_low_memory_limit = VIRT_RAM_BASE + LOW_MEMORY_CEILING;

    if (!header) {
        return -1;
    }
    if (header->magic[0] == 0x7F &&
        header->magic[1] == 'E' &&
        header->magic[2] == 'L' &&
        header->magic[3] == 'F') {
        log_error("bootloader: %s is ELF, expected custom ROSKRNL format", BOOTLOADER_FWCFG_KERNEL_PATH);
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
        log_error("bootloader: %s has invalid magic", BOOTLOADER_FWCFG_KERNEL_PATH);
        return -1;
    }
    if (header->version != ROSKRNL_VERSION || header->machine != ROSKRNL_MACHINE_AARCH64) {
        log_error("bootloader: %s has unsupported version or machine", BOOTLOADER_FWCFG_KERNEL_PATH);
        return -1;
    }
    if (header->header_size < sizeof(RosKernelHeader) || header->payload_offset < header->header_size) {
        log_error("bootloader: %s has invalid header layout", BOOTLOADER_FWCFG_KERNEL_PATH);
        return -1;
    }
    if (header->payload_size > header->memory_size) {
        log_error("bootloader: %s payload exceeds memory span", BOOTLOADER_FWCFG_KERNEL_PATH);
        return -1;
    }
    if (!mem_is_page_aligned(header->load_address)) {
        log_error("bootloader: %s load address is not page aligned", BOOTLOADER_FWCFG_KERNEL_PATH);
        return -1;
    }
    if (header->payload_offset + header->payload_size > (ULong)file_size) {
        log_error("bootloader: %s payload extends past end of fw_cfg file", BOOTLOADER_FWCFG_KERNEL_PATH);
        return -1;
    }
    if (header->memory_size == 0UL) {
        log_error("bootloader: %s memory span is empty", BOOTLOADER_FWCFG_KERNEL_PATH);
        return -1;
    }
    image_end = header->load_address + header->memory_size;
    if (image_end < header->load_address) {
        log_error("bootloader: %s load span overflows", BOOTLOADER_FWCFG_KERNEL_PATH);
        return -1;
    }
    if (image_end > reserved_low_memory_limit) {
        log_error("bootloader: %s load span 0x%lX-0x%lX exceeds reserved low memory", BOOTLOADER_FWCFG_KERNEL_PATH, header->load_address, image_end);
        return -1;
    }
    if (header->entry_point < header->image_base || header->entry_point >= (header->image_base + header->memory_size)) {
        log_error("bootloader: %s entry 0x%lX is outside image base 0x%lX", BOOTLOADER_FWCFG_KERNEL_PATH, header->entry_point, header->image_base);
        return -1;
    }
    return 0;
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
    handoff->flags = ROS_BOOT_HANDOFF_FLAG_UART0_READY;
    handoff->kernel_load_address = header->load_address;
    handoff->kernel_image_base = header->image_base;
    handoff->kernel_entry_point = header->entry_point;
    handoff->secondary_program_boot_entry_fn = (ULong)secondary_program_boot_entry;
    handoff->secondary_publish_boot_entries_fn = (ULong)secondary_publish_boot_entries;
    handoff->secondary_entry_point = 0UL;
    asm volatile("dsb ishst; isb" ::: "memory");
}

static void bootloader_trace_header(const RosKernelHeader* header) {
    if (!header) {
        return;
    }

    bootloader_tracef("ROSKRNL header version=%u machine=%u flags=0x%X", header->version, header->machine, header->flags);
    bootloader_tracef("ROSKRNL load=0x%lX image_base=0x%lX entry=0x%lX", header->load_address, header->image_base, header->entry_point);
    bootloader_tracef("ROSKRNL payload=%lu bytes memory=%lu bytes align=0x%lX", header->payload_size, header->memory_size, header->alignment);
}

static int bootloader_find_fwcfg_file(const char* name, unsigned short* selector, unsigned int* size) {
    UInt count = 0;

    if (!name || !selector || !size) {
        return -1;
    }

    bootloader_fwcfg_select(FW_CFG_FILE_DIR);
    bootloader_fwcfg_read_current(&count, sizeof(count));
    count = bootloader_bswap32(count);
    for (UInt index = 0; index < count; index++) {
        VirtFwCfgFile entry;

        bootloader_fwcfg_read_current(&entry, sizeof(entry));
        if (strcmp(entry.name, name) == 0) {
            *selector = bootloader_bswap16(entry.select_be);
            *size = bootloader_bswap32(entry.size_be);
            return 0;
        }
    }

    return -1;
}

static int bootloader_copy_kernel(unsigned short selector, unsigned int file_size, const RosKernelHeader* header) {
    ULong copied = 0UL;
    ULong next_progress = BOOTLOADER_PROGRESS_STEP;
    ULong remaining_prefix = header->payload_offset;
    UByte* destination = (UByte*)mem_phys_to_virt((PhysAddr)header->load_address);

    (void)file_size;
    bootloader_fwcfg_select(selector);

    while (remaining_prefix > 0UL) {
        UInt chunk = (remaining_prefix > BOOTLOADER_COPY_CHUNK) ? BOOTLOADER_COPY_CHUNK : (UInt)remaining_prefix;
        bootloader_fwcfg_read_current(bootloader_copy_buffer, chunk);
        remaining_prefix -= chunk;
    }

    while (copied < header->payload_size) {
        UInt chunk = (UInt)(header->payload_size - copied);

        if (chunk > BOOTLOADER_COPY_CHUNK) {
            chunk = BOOTLOADER_COPY_CHUNK;
        }
        bootloader_fwcfg_read_current(bootloader_copy_buffer, chunk);
        memmove(destination + copied, bootloader_copy_buffer, chunk);
        copied += chunk;
        if (copied >= next_progress || copied == header->payload_size) {
            bootloader_tracef("copy progress %lu/%lu bytes", copied, header->payload_size);
            next_progress += BOOTLOADER_PROGRESS_STEP;
        }
    }

    if (header->memory_size > header->payload_size) {
        bootloader_zero_memory(destination + header->payload_size, header->memory_size - header->payload_size);
        bootloader_tracef("zero filled %lu bytes", header->memory_size - header->payload_size);
    }

    asm volatile("dsb ish; ic iallu; dsb ish; isb" ::: "memory");
    return 0;
}

static void bootloader_jump_to_kernel(Address entry_point) {
    void (*entry)(void) = (void (*)(void))entry_point;

    log_info("bootloader: transferring control to ROSKRNL at 0x%lX", entry_point);
    entry();
}

void kernel_main() {
    RosKernelHeader header;
    unsigned short kernel_selector = 0;
    unsigned int kernel_size = 0;

    bootloader_init_io();
    bootloader_trace("opening ROSKRNL from fw_cfg");

    if (bootloader_find_fwcfg_file(BOOTLOADER_FWCFG_KERNEL_PATH, &kernel_selector, &kernel_size) != 0) {
        bootloader_panic("unable to open ROSKRNL fw_cfg file");
    }

    bootloader_tracef("%s size=%u bytes", BOOTLOADER_FWCFG_KERNEL_PATH, kernel_size);
    if (kernel_size < (unsigned int)sizeof(RosKernelHeader)) {
        bootloader_panic("ROSKRNL fw_cfg file is smaller than its header");
    }

    bootloader_fwcfg_read(kernel_selector, &header, sizeof(header));
    if (bootloader_validate_header(&header, kernel_size) != 0) {
        bootloader_panic("ROSKRNL header validation failed");
    }
    bootloader_trace_header(&header);

    if (bootloader_copy_kernel(kernel_selector, kernel_size, &header) != 0) {
        bootloader_panic("unable to copy ROSKRNL payload");
    }

    bootloader_publish_handoff(&header);
    bootloader_trace("jumping to loaded ROSKRNL entry point");
    bootloader_jump_to_kernel((Address)header.entry_point);

    while (1) {
    }
}