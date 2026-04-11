#include "graphics.h"
#include "log.h"
#include "memory.h"
#include "ros.h"

#define VIRT_FW_CFG_BASE (VA_START + 0x09020000UL)
#define VIRT_FW_CFG_DATA ((volatile unsigned char*)(VIRT_FW_CFG_BASE + 0x00UL))
#define VIRT_FW_CFG_CTL ((volatile unsigned short*)(VIRT_FW_CFG_BASE + 0x08UL))
#define VIRT_FW_CFG_DMA ((volatile unsigned long*)(VIRT_FW_CFG_BASE + 0x10UL))

#define FW_CFG_SIGNATURE 0x0000U
#define FW_CFG_ID 0x0001U
#define FW_CFG_FILE_DIR 0x0019U
#define FW_CFG_VERSION_DMA 0x02U
#define FW_CFG_DMA_CTL_ERROR 0x01U
#define FW_CFG_DMA_CTL_READ 0x02U
#define FW_CFG_DMA_CTL_SELECT 0x08U
#define FW_CFG_DMA_CTL_WRITE 0x10U

#define VIRT_FRAMEBUFFER_WIDTH 1024U
#define VIRT_FRAMEBUFFER_HEIGHT 768U
#define VIRT_FRAMEBUFFER_BPP 4U
#define VIRT_FRAMEBUFFER_STRIDE (VIRT_FRAMEBUFFER_WIDTH * VIRT_FRAMEBUFFER_BPP)
#define VIRT_FRAMEBUFFER_SIZE (VIRT_FRAMEBUFFER_STRIDE * VIRT_FRAMEBUFFER_HEIGHT)
#define VIRT_RAMFB_FILE "etc/ramfb"
#define VIRT_DRM_FORMAT_XRGB8888 0x34325258U

typedef struct __attribute__((packed)) VirtFwCfgFileStruct {
    UInt size_be;
    unsigned short select_be;
    unsigned short reserved;
    char name[56];
} VirtFwCfgFile;

typedef struct __attribute__((packed, aligned(16))) VirtFwCfgDmaAccessStruct {
    UInt control_be;
    UInt length_be;
    ULong address_be;
} VirtFwCfgDmaAccess;

typedef struct __attribute__((packed)) VirtRamfbConfigStruct {
    ULong addr_be;
    UInt fourcc_be;
    UInt flags_be;
    UInt width_be;
    UInt height_be;
    UInt stride_be;
} VirtRamfbConfig;

static unsigned int* virt_framebuffer_storage;
static unsigned char virt_fwcfg_directory[4096];
static VirtFwCfgDmaAccess virt_fwcfg_dma_access;
static unsigned int framebuffer_width_value;
static unsigned int framebuffer_height_value;
static unsigned int framebuffer_pitch_value;
static unsigned int framebuffer_is_rgb_value;
static Address framebuffer_address_value;
static int framebuffer_ready;

static unsigned short virt_bswap16(unsigned short value) {
    return (unsigned short)__builtin_bswap16(value);
}

static UInt virt_bswap32(UInt value) {
    return (UInt)__builtin_bswap32(value);
}

static ULong virt_bswap64(ULong value) {
    return (ULong)__builtin_bswap64(value);
}

static PhysAddr virt_fwcfg_virt_to_phys(const void* address) {
    return mem_virt_to_phys((VirtAddr)address);
}

static void virt_fwcfg_select(unsigned short selector) {
    *VIRT_FW_CFG_CTL = virt_bswap16(selector);
}

static void virt_fwcfg_read_raw(unsigned short selector, void* data, UInt length) {
    unsigned char* out = (unsigned char*)data;

    virt_fwcfg_select(selector);
    while (length-- > 0U) {
        *out++ = *VIRT_FW_CFG_DATA;
    }
}

static int virt_fwcfg_dma(unsigned short selector, UInt control, void* data, UInt length) {
    UInt status;

    virt_fwcfg_dma_access.control_be = virt_bswap32(((UInt)selector << 16) | FW_CFG_DMA_CTL_SELECT | control);
    virt_fwcfg_dma_access.length_be = virt_bswap32(length);
    virt_fwcfg_dma_access.address_be = virt_bswap64((ULong)virt_fwcfg_virt_to_phys(data));

    asm volatile("dsb ishst" ::: "memory");
    *VIRT_FW_CFG_DMA = virt_bswap64((ULong)virt_fwcfg_virt_to_phys(&virt_fwcfg_dma_access));

    do {
        asm volatile("dsb ish" ::: "memory");
        status = virt_bswap32(virt_fwcfg_dma_access.control_be);
    } while ((status & ~(UInt)FW_CFG_DMA_CTL_ERROR) != 0U);

    return (status & FW_CFG_DMA_CTL_ERROR) ? -1 : 0;
}

static int virt_fwcfg_find_file(const char* name, unsigned short* selector) {
    UInt count;
    UInt required_size;
    VirtFwCfgFile* entry;

    if (!name || !selector) {
        return -1;
    }

    if (virt_fwcfg_dma(FW_CFG_FILE_DIR, FW_CFG_DMA_CTL_READ, virt_fwcfg_directory, sizeof(virt_fwcfg_directory)) != 0) {
        return -1;
    }

    count = virt_bswap32(*(UInt*)virt_fwcfg_directory);
    required_size = 4U + (count * (UInt)sizeof(VirtFwCfgFile));
    if (required_size > sizeof(virt_fwcfg_directory)) {
        return -1;
    }

    entry = (VirtFwCfgFile*)(virt_fwcfg_directory + 4U);
    for (UInt index = 0; index < count; index++) {
        if (__builtin_strcmp(entry[index].name, name) == 0) {
            *selector = virt_bswap16(entry[index].select_be);
            return 0;
        }
    }

    return -1;
}

static unsigned int framebuffer_clip_width(unsigned int x, unsigned int width) {
    if (x >= framebuffer_width_value) {
        return 0;
    }
    if (width > framebuffer_width_value - x) {
        return framebuffer_width_value - x;
    }
    return width;
}

static unsigned int framebuffer_clip_height(unsigned int y, unsigned int height) {
    if (y >= framebuffer_height_value) {
        return 0;
    }
    if (height > framebuffer_height_value - y) {
        return framebuffer_height_value - y;
    }
    return height;
}

int graphics_init(void) {
    char signature[4];
    UInt fwcfg_id = 0;
    unsigned short ramfb_selector = 0;
    VirtRamfbConfig ramfb_config;

    if (!virt_framebuffer_storage) {
        virt_framebuffer_storage = (unsigned int*)kmalloc((int)VIRT_FRAMEBUFFER_SIZE);
        if (!virt_framebuffer_storage) {
            log_warning("virt framebuffer init failed: unable to allocate framebuffer storage");
            return -1;
        }
    }

    virt_fwcfg_read_raw(FW_CFG_SIGNATURE, signature, sizeof(signature));
    if (signature[0] != 'Q' || signature[1] != 'E' || signature[2] != 'M' || signature[3] != 'U') {
        log_warning("virt framebuffer init failed: fw_cfg signature missing");
        return -1;
    }

    virt_fwcfg_read_raw(FW_CFG_ID, &fwcfg_id, sizeof(fwcfg_id));
    if ((fwcfg_id & FW_CFG_VERSION_DMA) == 0U) {
        log_warning("virt framebuffer init failed: fw_cfg DMA unavailable");
        return -1;
    }

    if (virt_fwcfg_find_file(VIRT_RAMFB_FILE, &ramfb_selector) != 0) {
        log_warning("virt framebuffer init failed: etc/ramfb not exposed by QEMU");
        return -1;
    }

    ramfb_config.addr_be = virt_bswap64((ULong)mem_virt_to_phys((VirtAddr)virt_framebuffer_storage));
    ramfb_config.fourcc_be = virt_bswap32(VIRT_DRM_FORMAT_XRGB8888);
    ramfb_config.flags_be = 0U;
    ramfb_config.width_be = virt_bswap32(VIRT_FRAMEBUFFER_WIDTH);
    ramfb_config.height_be = virt_bswap32(VIRT_FRAMEBUFFER_HEIGHT);
    ramfb_config.stride_be = virt_bswap32(VIRT_FRAMEBUFFER_STRIDE);

    if (virt_fwcfg_dma(ramfb_selector, FW_CFG_DMA_CTL_WRITE, &ramfb_config, sizeof(ramfb_config)) != 0) {
        log_warning("virt framebuffer init failed: ramfb configuration write failed");
        return -1;
    }

    framebuffer_width_value = VIRT_FRAMEBUFFER_WIDTH;
    framebuffer_height_value = VIRT_FRAMEBUFFER_HEIGHT;
    framebuffer_pitch_value = VIRT_FRAMEBUFFER_STRIDE;
    framebuffer_is_rgb_value = 1U;
    framebuffer_address_value = (Address)virt_framebuffer_storage;
    framebuffer_ready = 1;
    log_info("virt framebuffer ready: %ux%u pitch=%u addr=0x%lX", framebuffer_width_value, framebuffer_height_value, framebuffer_pitch_value, framebuffer_address_value);
    return 0;
}

int graphics_is_ready(void) {
    return framebuffer_ready;
}

unsigned int graphics_width(void) {
    return framebuffer_width_value;
}

unsigned int graphics_height(void) {
    return framebuffer_height_value;
}

unsigned int graphics_pitch(void) {
    return framebuffer_pitch_value;
}

unsigned int graphics_is_rgb(void) {
    return framebuffer_is_rgb_value;
}

Address graphics_framebuffer(void) {
    return framebuffer_address_value;
}

void graphics_draw_pixel(unsigned int x, unsigned int y, unsigned int color) {
    unsigned int* pixel;

    if (!framebuffer_ready || x >= framebuffer_width_value || y >= framebuffer_height_value) {
        return;
    }

    pixel = (unsigned int*)(framebuffer_address_value + ((Address)y * framebuffer_pitch_value) + ((Address)x * 4U));
    *pixel = color;
}

void graphics_fill_rect(unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned int color) {
    unsigned int clipped_width;
    unsigned int clipped_height;

    if (!framebuffer_ready || width == 0U || height == 0U) {
        return;
    }

    clipped_width = framebuffer_clip_width(x, width);
    clipped_height = framebuffer_clip_height(y, height);
    for (unsigned int row = 0; row < clipped_height; row++) {
        unsigned int* pixel = (unsigned int*)(framebuffer_address_value + ((Address)(y + row) * framebuffer_pitch_value) + ((Address)x * 4U));
        for (unsigned int col = 0; col < clipped_width; col++) {
            pixel[col] = color;
        }
    }
}

void graphics_clear(unsigned int color) {
    graphics_fill_rect(0U, 0U, framebuffer_width_value, framebuffer_height_value, color);
}