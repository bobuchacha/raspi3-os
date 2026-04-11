#ifndef KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_FRAMEBUFFER_BACKEND_H
#define KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_FRAMEBUFFER_BACKEND_H

#include "framebuffer.h"
#include "device.h"
#include "mm.h"

namespace board {

    class Framebuffer final {
    public:
        static DeviceDriver* driver(void) {
            return &Driver;
        }

        static Device* device(void) {
            return &DeviceInstance;
        }

    private:
        enum : U32 {
            FwCfgSignature = 0x0000U,
            FwCfgId = 0x0001U,
            FwCfgFileDir = 0x0019U,
            FwCfgVersionDma = 0x02U,
            FwCfgDmaCtlError = 0x01U,
            FwCfgDmaCtlRead = 0x02U,
            FwCfgDmaCtlSelect = 0x08U,
            FwCfgDmaCtlWrite = 0x10U,
        };

        static constexpr Uptr FwCfgBase = 0x09020000UL;
        static constexpr Uptr FwCfgDataOffset = 0x00UL;
        static constexpr Uptr FwCfgCtlOffset = 0x08UL;
        static constexpr Uptr FwCfgDmaOffset = 0x10UL;
        static constexpr U32 Width = 1024U;
        static constexpr U32 Height = 768U;
        static constexpr U32 BytesPerPixel = 4U;
        static constexpr U32 Pitch = Width * BytesPerPixel;
        static constexpr Size FramebufferSize = static_cast<Size>(Pitch) * static_cast<Size>(Height);
        static constexpr U32 DrmFormatXrgb8888 = 0x34325258U;

        typedef struct __attribute__((packed)) VirtFwCfgFile {
            U32 size_be;
            U16 select_be;
            U16 reserved;
            char name[56];
        } VirtFwCfgFile;

        typedef struct __attribute__((packed, aligned(16))) VirtFwCfgDmaAccess {
            U32 control_be;
            U32 length_be;
            U64 address_be;
        } VirtFwCfgDmaAccess;

        typedef struct __attribute__((packed)) VirtRamfbConfig {
            U64 addr_be;
            U32 fourcc_be;
            U32 flags_be;
            U32 width_be;
            U32 height_be;
            U32 stride_be;
        } VirtRamfbConfig;

        typedef struct FramebufferState {
            bool ready;
            U32 width;
            U32 height;
            U32 pitch;
            U32 bytes_per_pixel;
            Size size_bytes;
        } FramebufferState;

        static volatile U8* data_reg(void) {
            return reinterpret_cast<volatile U8*>(FwCfgBase + FwCfgDataOffset);
        }

        static volatile U16* control_reg(void) {
            return reinterpret_cast<volatile U16*>(FwCfgBase + FwCfgCtlOffset);
        }

        static volatile U64* dma_reg(void) {
            return reinterpret_cast<volatile U64*>(FwCfgBase + FwCfgDmaOffset);
        }

        static U16 bswap16(U16 value) {
            return static_cast<U16>((value >> 8) | (value << 8));
        }

        static U32 bswap32(U32 value) {
            return ((value & 0x000000FFU) << 24) |
                ((value & 0x0000FF00U) << 8) |
                ((value & 0x00FF0000U) >> 8) |
                ((value & 0xFF000000U) >> 24);
        }

        static U64 bswap64(U64 value) {
            return (static_cast<U64>(bswap32(static_cast<U32>(value & 0xFFFFFFFFULL))) << 32) |
                static_cast<U64>(bswap32(static_cast<U32>(value >> 32)));
        }

        static PhysAddr phys_addr(const void* address) {
            return mm::MemoryManager::kernel_to_physical(reinterpret_cast<VirtAddr>(address));
        }

        static bool same_text(const char* lhs, const char* rhs) {
            if ((lhs == NULL) || (rhs == NULL)) {
                return false;
            }

            while ((*lhs != '\0') && (*rhs != '\0')) {
                if (*lhs != *rhs) {
                    return false;
                }
                ++lhs;
                ++rhs;
            }

            return *lhs == *rhs;
        }

        static void select(U16 selector) {
            *control_reg() = bswap16(selector);
        }

        static void read_raw(U16 selector, void* buffer, U32 length) {
            U8* bytes = static_cast<U8*>(buffer);

            select(selector);
            while (length-- > 0U) {
                *bytes++ = *data_reg();
            }
        }

        static Status dma_transfer(U16 selector, U32 control, void* buffer, U32 length) {
            U32 status;

            DmaAccess.control_be = bswap32((static_cast<U32>(selector) << 16) | FwCfgDmaCtlSelect | control);
            DmaAccess.length_be = bswap32(length);
            DmaAccess.address_be = bswap64(static_cast<U64>(phys_addr(buffer)));

            __asm__ volatile("dsb ishst" ::: "memory");
            *dma_reg() = bswap64(static_cast<U64>(phys_addr(&DmaAccess)));

            do {
                __asm__ volatile("dsb ish" ::: "memory");
                status = bswap32(DmaAccess.control_be);
            } while ((status & ~FwCfgDmaCtlError) != 0U);

            return ((status & FwCfgDmaCtlError) != 0U) ? StatusIoError : StatusOK;
        }

        static Status find_file(const char* name, U16* selector_out) {
            U32 file_count;
            U32 directory_size;
            VirtFwCfgFile* file_entries;

            if ((name == NULL) || (selector_out == NULL)) {
                return StatusInvalidArgument;
            }

            if (dma_transfer(FwCfgFileDir, FwCfgDmaCtlRead, FileDirectory, sizeof(FileDirectory)) != StatusOK) {
                return StatusIoError;
            }

            file_count = bswap32(*reinterpret_cast<U32*>(FileDirectory));
            directory_size = 4U + (file_count * static_cast<U32>(sizeof(VirtFwCfgFile)));
            if (directory_size > sizeof(FileDirectory)) {
                return StatusNoSpace;
            }

            file_entries = reinterpret_cast<VirtFwCfgFile*>(FileDirectory + 4U);
            for (U32 index = 0; index < file_count; ++index) {
                if (same_text(file_entries[index].name, name)) {
                    *selector_out = bswap16(file_entries[index].select_be);
                    return StatusOK;
                }
            }

            return StatusNotFound;
        }

        static Status init_impl(FramebufferState* state) {
            char signature[4];
            U32 fwcfg_id = 0U;
            U16 ramfb_selector;
            VirtRamfbConfig config;
            Status status;

            if (state == NULL) {
                return StatusInvalidArgument;
            }
            if (state->ready) {
                return StatusAlreadyExists;
            }

            read_raw(FwCfgSignature, signature, sizeof(signature));
            if ((signature[0] != 'Q') || (signature[1] != 'E') || (signature[2] != 'M') || (signature[3] != 'U')) {
                return StatusNotFound;
            }

            // QEMU exposes the fw_cfg ID register in the CPU's native byte order on this path, so
            // preserve the raw register layout instead of reinterpreting it as a big-endian field.
            read_raw(FwCfgId, &fwcfg_id, sizeof(fwcfg_id));
            if ((fwcfg_id & FwCfgVersionDma) == 0U) {
                return StatusNotSupported;
            }

            status = find_file("etc/ramfb", &ramfb_selector);
            if (status != StatusOK) {
                return status;
            }

            config.addr_be = bswap64(static_cast<U64>(phys_addr(FramebufferStorage)));
            config.fourcc_be = bswap32(DrmFormatXrgb8888);
            config.flags_be = 0U;
            config.width_be = bswap32(Width);
            config.height_be = bswap32(Height);
            config.stride_be = bswap32(Pitch);

            status = dma_transfer(ramfb_selector, FwCfgDmaCtlWrite, &config, sizeof(config));
            if (status != StatusOK) {
                return status;
            }

            state->ready = true;
            state->width = Width;
            state->height = Height;
            state->pitch = Pitch;
            state->bytes_per_pixel = BytesPerPixel;
            state->size_bytes = FramebufferSize;
            return StatusOK;
        }

        static Status init_device(Device* device) {
            return init_impl(static_cast<FramebufferState*>(device->state));
        }

        static Status deinit_device(Device* device) {
            FramebufferState* state = static_cast<FramebufferState*>(device->state);

            if (state == NULL) {
                return StatusInvalidArgument;
            }

            state->ready = false;
            return StatusOK;
        }

        static SSize read_device(Device* device, U64 offset, void* buffer, Size length) {
            FramebufferState* state = static_cast<FramebufferState*>(device->state);

            if ((state == NULL) || ((buffer == NULL) && (length != 0U))) {
                return StatusInvalidArgument;
            }
            if (!state->ready) {
                return StatusNotFound;
            }
            if (length == 0U) {
                return 0;
            }
            if ((offset > state->size_bytes) || (length > (state->size_bytes - offset))) {
                return StatusInvalidArgument;
            }

            memcopy(buffer, FramebufferStorage + offset, length);
            return static_cast<SSize>(length);
        }

        static SSize write_device(Device* device, U64 offset, const void* buffer, Size length) {
            FramebufferState* state = static_cast<FramebufferState*>(device->state);

            if ((state == NULL) || ((buffer == NULL) && (length != 0U))) {
                return StatusInvalidArgument;
            }
            if (!state->ready) {
                Status status = init_impl(state);

                if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                    return status;
                }
            }
            if (length == 0U) {
                return 0;
            }
            if ((offset > state->size_bytes) || (length > (state->size_bytes - offset))) {
                return StatusInvalidArgument;
            }

            memcopy(FramebufferStorage + offset, buffer, length);
            return static_cast<SSize>(length);
        }

        static Status ioctl_device(Device* device, U32 request, void* argument) {
            FramebufferState* state = static_cast<FramebufferState*>(device->state);
            FramebufferGeometry* geometry = static_cast<FramebufferGeometry*>(argument);

            if (state == NULL) {
                return StatusInvalidArgument;
            }
            if (request != FramebufferIoctlGetGeometry) {
                return StatusNotSupported;
            }
            if (geometry == NULL) {
                return StatusInvalidArgument;
            }
            if (!state->ready) {
                Status status = init_impl(state);

                if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                    return status;
                }
            }

            geometry->width = state->width;
            geometry->height = state->height;
            geometry->pitch = state->pitch;
            geometry->bytes_per_pixel = state->bytes_per_pixel;
            geometry->pixel_format = FramebufferPixelFormatXrgb8888;
            geometry->size_bytes = state->size_bytes;
            return StatusOK;
        }

        inline static constexpr DeviceInterface Interface = {
            &init_device,
            &deinit_device,
            &read_device,
            &write_device,
            &ioctl_device,
        };

        inline static DeviceDriver Driver = {
            "virt-ramfb",
            &Interface,
            NULL,
        };

        alignas(16) inline static VirtFwCfgDmaAccess DmaAccess = {};
        inline static U8 FileDirectory[4096] = {};
        alignas(4096) inline static U8 FramebufferStorage[FramebufferSize] = {};
        inline static FramebufferState State = {};
        inline static Device DeviceInstance = {
            "framebuffer0",
            NULL,
            &State,
            0U,
            &Driver,
        };
    };

} // namespace board

#endif // KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_FRAMEBUFFER_BACKEND_H