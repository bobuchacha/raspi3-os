#ifndef KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_BLOCK_BACKEND_H
#define KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_BLOCK_BACKEND_H

#include "device.h"
#include "mm.h"

namespace board {

    class BlockStorage final {
    public:
        /**
         * Drain one pending virtio-blk completion without blocking.
         *
         * The current driver still presents a synchronous block-device API, but
         * later async file I/O work needs the completion path to exist as a
         * separate step that a board poll hook or IRQ path can call. This
         * helper only tries the driver lock once so an IRQ or scheduler poll
         * cannot deadlock if it interrupts a thread already preparing or
         * waiting on a request.
         *
         * @return Nothing.
         */
        static void poll(void) {
            VirtioBlockState* state = &State;

            if ((state == NULL) || !state->ready || !state->request_in_flight) {
                return;
            }
            if (!try_lock(&state->lock_word)) {
                return;
            }

            (void)drain_request_completion_locked(state);
            unlock(state);
        }

        static DeviceDriver* driver(void) {
            return &Driver;
        }

        static Device* device(void) {
            return &DeviceInstance;
        }

    private:
        enum : U32 {
            IoctlGetSectorSize = 1U,
            IoctlGetSectorCount = 2U,
            VirtioMmioMagicValue = 0x000U,
            VirtioMmioVersion = 0x004U,
            VirtioMmioDeviceId = 0x008U,
            VirtioMmioVendorId = 0x00CU,
            VirtioMmioDeviceFeatures = 0x010U,
            VirtioMmioDeviceFeaturesSel = 0x014U,
            VirtioMmioDriverFeatures = 0x020U,
            VirtioMmioDriverFeaturesSel = 0x024U,
            VirtioMmioQueueSel = 0x030U,
            VirtioMmioQueueNumMax = 0x034U,
            VirtioMmioQueueNum = 0x038U,
            VirtioMmioQueueReady = 0x044U,
            VirtioMmioQueueNotify = 0x050U,
            VirtioMmioInterruptStatus = 0x060U,
            VirtioMmioInterruptAck = 0x064U,
            VirtioMmioStatus = 0x070U,
            VirtioMmioQueueDescLow = 0x080U,
            VirtioMmioQueueDescHigh = 0x084U,
            VirtioMmioQueueAvailLow = 0x090U,
            VirtioMmioQueueAvailHigh = 0x094U,
            VirtioMmioQueueUsedLow = 0x0A0U,
            VirtioMmioQueueUsedHigh = 0x0A4U,
            VirtioMagic = 0x74726976U,
            VirtioVersionModern = 2U,
            VirtioDeviceIdBlock = 2U,
            VirtioVendorQemu = 0x554D4551U,
            VirtioStatusAcknowledge = 0x01U,
            VirtioStatusDriver = 0x02U,
            VirtioStatusDriverOk = 0x04U,
            VirtioStatusFeaturesOk = 0x08U,
            VirtioStatusFailed = 0x80U,
            VirtioFeatureVersion1 = 32U,
            VirtqDescFlagNext = 1U,
            VirtqDescFlagWrite = 2U,
            VirtioBlkTypeRead = 0U,
            VirtioBlkTypeWrite = 1U,
            VirtioBlkStatusOk = 0U,
            VirtioBlkStatusIoError = 1U,
        };

        static constexpr Uptr MmioBase = 0x0A000000UL;
        static constexpr Uptr MmioStride = 0x200UL;
        static constexpr U32 MmioSlotCount = 32U;
        static constexpr U32 QueueSize = 8U;
        static constexpr U32 PollLimit = 1000000U;
        static constexpr U64 SectorSize = 512ULL;

        typedef struct __attribute__((packed)) VirtqDesc {
            U64 addr;
            U32 len;
            U16 flags;
            U16 next;
        } VirtqDesc;

        typedef struct __attribute__((packed)) VirtqAvail {
            U16 flags;
            U16 idx;
            U16 ring[QueueSize];
            U16 used_event;
        } VirtqAvail;

        typedef struct __attribute__((packed)) VirtqUsedElem {
            U32 id;
            U32 len;
        } VirtqUsedElem;

        typedef struct __attribute__((packed)) VirtqUsed {
            U16 flags;
            U16 idx;
            VirtqUsedElem ring[QueueSize];
            U16 avail_event;
        } VirtqUsed;

        typedef struct __attribute__((packed)) VirtioBlkRequest {
            U32 type;
            U32 reserved;
            U64 sector;
        } VirtioBlkRequest;

        typedef struct VirtioBlockState {
            bool ready;
            bool completion_ready;
            bool request_in_flight;
            U16 last_used_idx;
            U32 slot_index;
            Status completion_status;
            U32 request_type;
            U64 sector_count;
            U64 request_sector;
            volatile U32 lock_word;
        } VirtioBlockState;

        static bool try_lock(volatile U32* lock_word) {
            U32 previous;
            U32 status;

            __asm__ volatile(
                "ldaxr %w0, [%2]\n"
                "cbnz %w0, 1f\n"
                "mov %w0, #1\n"
                "stxr %w1, %w0, [%2]\n"
                "cbnz %w1, 2f\n"
                "mov %w0, wzr\n"
                "b 3f\n"
                "1:\n"
                "mov %w1, wzr\n"
                "2:\n"
                "3:\n"
                : "=&r"(previous), "=&r"(status)
                : "r"(lock_word)
                : "memory");

            return (previous == 0U) && (status == 0U);
        }

        static void lock(VirtioBlockState* state) {
            while (!try_lock(&state->lock_word)) {
                __asm__ volatile("yield\n" ::: "memory");
            }
        }

        static void unlock(VirtioBlockState* state) {
            __asm__ volatile(
                "stlr %w1, [%0]\n"
                :
            : "r"(&state->lock_word), "r"(0U)
                : "memory");
        }

        static volatile U32* reg(U32 slot_index, Uptr offset) {
            return reinterpret_cast<volatile U32*>(MmioBase + (static_cast<Uptr>(slot_index) * MmioStride) + offset);
        }

        static U32 read_reg(U32 slot_index, Uptr offset) {
            return *reg(slot_index, offset);
        }

        static void write_reg(U32 slot_index, Uptr offset, U32 value) {
            *reg(slot_index, offset) = value;
        }

        static void write_addr(U32 slot_index, U32 low_offset, U32 high_offset, PhysAddr address) {
            write_reg(slot_index, low_offset, static_cast<U32>(address & 0xFFFFFFFFULL));
            write_reg(slot_index, high_offset, static_cast<U32>(address >> 32));
        }

        static void barrier(void) {
            __asm__ volatile("dmb ish" ::: "memory");
        }

        static PhysAddr phys_addr(const void* address) {
            return mm::MemoryManager::kernel_to_physical(reinterpret_cast<VirtAddr>(address));
        }

        static Status find_device_slot(VirtioBlockState* state) {
            for (U32 slot_index = 0; slot_index < MmioSlotCount; ++slot_index) {
                if ((read_reg(slot_index, VirtioMmioMagicValue) == VirtioMagic) &&
                    (read_reg(slot_index, VirtioMmioVersion) == VirtioVersionModern) &&
                    (read_reg(slot_index, VirtioMmioDeviceId) == VirtioDeviceIdBlock)) {
                    state->slot_index = slot_index;
                    return StatusOK;
                }
            }

            return StatusNotFound;
        }

        static Status negotiate_features(VirtioBlockState* state) {
            U32 status = 0U;
            U32 features_hi;

            if (read_reg(state->slot_index, VirtioMmioVendorId) != VirtioVendorQemu) {
                return StatusNotSupported;
            }

            write_reg(state->slot_index, VirtioMmioStatus, 0U);
            status |= VirtioStatusAcknowledge;
            write_reg(state->slot_index, VirtioMmioStatus, status);
            status |= VirtioStatusDriver;
            write_reg(state->slot_index, VirtioMmioStatus, status);

            write_reg(state->slot_index, VirtioMmioDeviceFeaturesSel, 1U);
            features_hi = read_reg(state->slot_index, VirtioMmioDeviceFeatures);
            if ((features_hi & (1U << (VirtioFeatureVersion1 - 32U))) == 0U) {
                write_reg(state->slot_index, VirtioMmioStatus, status | VirtioStatusFailed);
                return StatusNotSupported;
            }

            write_reg(state->slot_index, VirtioMmioDriverFeaturesSel, 0U);
            write_reg(state->slot_index, VirtioMmioDriverFeatures, 0U);
            write_reg(state->slot_index, VirtioMmioDriverFeaturesSel, 1U);
            write_reg(state->slot_index, VirtioMmioDriverFeatures, (1U << (VirtioFeatureVersion1 - 32U)));

            status |= VirtioStatusFeaturesOk;
            write_reg(state->slot_index, VirtioMmioStatus, status);
            if ((read_reg(state->slot_index, VirtioMmioStatus) & VirtioStatusFeaturesOk) == 0U) {
                write_reg(state->slot_index, VirtioMmioStatus, status | VirtioStatusFailed);
                return StatusNotSupported;
            }

            return StatusOK;
        }

        static Status setup_queue(VirtioBlockState* state) {
            write_reg(state->slot_index, VirtioMmioQueueSel, 0U);
            if (read_reg(state->slot_index, VirtioMmioQueueNumMax) < QueueSize) {
                write_reg(state->slot_index, VirtioMmioStatus, read_reg(state->slot_index, VirtioMmioStatus) | VirtioStatusFailed);
                return StatusNoSpace;
            }

            write_reg(state->slot_index, VirtioMmioQueueNum, QueueSize);
            write_addr(state->slot_index, VirtioMmioQueueDescLow, VirtioMmioQueueDescHigh, phys_addr(&DescTable));
            write_addr(state->slot_index, VirtioMmioQueueAvailLow, VirtioMmioQueueAvailHigh, phys_addr(&AvailRing));
            write_addr(state->slot_index, VirtioMmioQueueUsedLow, VirtioMmioQueueUsedHigh, phys_addr(&UsedRing));
            write_reg(state->slot_index, VirtioMmioQueueReady, 1U);
            barrier();
            return StatusOK;
        }

        static Status read_capacity(VirtioBlockState* state) {
            alignas(8) U8 config_bytes[8];

            for (Size index = 0; index < sizeof(config_bytes); ++index) {
                config_bytes[index] = *reinterpret_cast<volatile U8*>(MmioBase + (static_cast<Uptr>(state->slot_index) * MmioStride) + 0x100UL + index);
            }

            state->sector_count =
                static_cast<U64>(config_bytes[0]) |
                (static_cast<U64>(config_bytes[1]) << 8) |
                (static_cast<U64>(config_bytes[2]) << 16) |
                (static_cast<U64>(config_bytes[3]) << 24) |
                (static_cast<U64>(config_bytes[4]) << 32) |
                (static_cast<U64>(config_bytes[5]) << 40) |
                (static_cast<U64>(config_bytes[6]) << 48) |
                (static_cast<U64>(config_bytes[7]) << 56);
            return StatusOK;
        }

        /**
         * Populate the fixed single-request descriptor chain.
         *
         * The active `virt` backend still owns one shared request header,
         * status byte, and descriptor ring. Keeping the descriptor fill logic
         * in one helper makes the future async service reuse the exact same MMIO
         * layout while separating submission from completion draining.
         *
         * @param request_type Virtio block request opcode.
         * @param sector Device sector number.
         * @param buffer Sector-sized transfer buffer.
         * @return Nothing.
         */
        static void populate_request_descriptors(U32 request_type, U64 sector, void* buffer) {
            RequestHeader.type = request_type;
            RequestHeader.reserved = 0U;
            RequestHeader.sector = sector;
            StatusByte = 0xFFU;

            DescTable.entries[0].addr = phys_addr(&RequestHeader);
            DescTable.entries[0].len = sizeof(RequestHeader);
            DescTable.entries[0].flags = VirtqDescFlagNext;
            DescTable.entries[0].next = 1U;

            DescTable.entries[1].addr = phys_addr(buffer);
            DescTable.entries[1].len = static_cast<U32>(SectorSize);
            DescTable.entries[1].flags = VirtqDescFlagNext | ((request_type == VirtioBlkTypeRead) ? VirtqDescFlagWrite : 0U);
            DescTable.entries[1].next = 2U;

            DescTable.entries[2].addr = phys_addr(&StatusByte);
            DescTable.entries[2].len = sizeof(StatusByte);
            DescTable.entries[2].flags = VirtqDescFlagWrite;
            DescTable.entries[2].next = 0U;
        }

        /**
         * Submit one request to the shared virtio-blk queue.
         *
         * This helper deliberately stops after queue notification. Completion is
         * now drained by a separate helper so future async callers can submit a
         * request, release the lock, and observe the used ring later from a poll
         * hook or a dedicated wait path.
         *
         * @param state Active virtio-blk device state.
         * @param request_type Virtio block request opcode.
         * @param sector Device sector number.
         * @param buffer Sector-sized transfer buffer.
         * @return StatusOK when the queue now owns one in-flight request.
         */
        static Status begin_request_locked(VirtioBlockState* state, U32 request_type, U64 sector, void* buffer) {
            if ((state == NULL) || (buffer == NULL)) {
                return StatusInvalidArgument;
            }
            if (state->request_in_flight || state->completion_ready) {
                return StatusBusy;
            }

            populate_request_descriptors(request_type, sector, buffer);

            AvailRing.ring[AvailRing.idx % QueueSize] = 0U;
            barrier();
            ++AvailRing.idx;
            barrier();
            write_reg(state->slot_index, VirtioMmioQueueNotify, 0U);

            state->completion_status = StatusBusy;
            state->request_in_flight = true;
            state->request_type = request_type;
            state->request_sector = sector;
            return StatusOK;
        }

        /**
         * Consume one completed request from the shared used ring.
         *
         * Returning `StatusBusy` means the request is still in flight rather
         * than that the device failed. That distinction lets the synchronous
         * wrapper keep polling without collapsing the future async seam back
         * into the submission helper.
         *
         * @param state Active virtio-blk device state.
         * @return StatusOK or StatusIoError on completion, or StatusBusy while
         * the request is still pending.
         */
        static Status drain_request_completion_locked(VirtioBlockState* state) {
            if (state == NULL) {
                return StatusInvalidArgument;
            }
            if (state->completion_ready) {
                return state->completion_status;
            }
            if (!state->request_in_flight) {
                return StatusBusy;
            }

            barrier();
            if (UsedRing.idx == state->last_used_idx) {
                return StatusBusy;
            }

            state->last_used_idx = UsedRing.idx;
            write_reg(state->slot_index, VirtioMmioInterruptAck, read_reg(state->slot_index, VirtioMmioInterruptStatus));
            state->request_in_flight = false;
            state->completion_ready = true;
            state->completion_status = (StatusByte == VirtioBlkStatusOk) ? StatusOK : StatusIoError;
            return state->completion_status;
        }

        /**
         * Consume one latched completion result.
         *
         * The board poll hook can observe a used-ring update before the
         * synchronous caller wakes up. Latching the result keeps that split
         * correct, and this helper lets the waiter acknowledge the completion
         * once it has seen the final status.
         *
         * @param state Active virtio-blk device state.
         * @return Nothing.
         */
        static void consume_request_completion_locked(VirtioBlockState* state) {
            if (state == NULL) {
                return;
            }

            state->completion_ready = false;
            state->completion_status = StatusBusy;
        }

        /**
         * Preserve the current synchronous read/write behavior on top of the
         * split submit-plus-drain helpers.
         *
         * The driver API is still synchronous today, so callers keep blocking
         * until completion. The important change is that the wait loop no longer
         * owns the MMIO submission logic, which leaves a clean insertion point
         * for a worker thread or IRQ-driven completion path later.
         *
         * @param state Active virtio-blk device state.
         * @param request_type Virtio block request opcode.
         * @param sector Device sector number.
         * @param buffer Sector-sized transfer buffer.
         * @return StatusOK on completion, or an error when the request fails or
         * the completion never arrives.
         */
        static Status submit_request_sync(VirtioBlockState* state, U32 request_type, U64 sector, void* buffer) {
            U32 polls = PollLimit;
            Status status;

            lock(state);
            status = begin_request_locked(state, request_type, sector, buffer);
            unlock(state);
            if (status != StatusOK) {
                return status;
            }

            while (polls-- > 0U) {
                lock(state);
                status = drain_request_completion_locked(state);
                if (status != StatusBusy) {
                    consume_request_completion_locked(state);
                }
                unlock(state);
                if (status != StatusBusy) {
                    return status;
                }
            }

            lock(state);
            state->ready = false;
            state->completion_ready = false;
            state->completion_status = StatusBusy;
            state->request_in_flight = false;
            unlock(state);
            return StatusBusy;
        }

        static Status init_impl(VirtioBlockState* state) {
            U32 status_register;
            Status status;

            if (state == NULL) {
                return StatusInvalidArgument;
            }
            if (state->ready) {
                return StatusAlreadyExists;
            }

            memzero(&DescTable, sizeof(DescTable));
            memzero(&AvailRing, sizeof(AvailRing));
            memzero(&UsedRing, sizeof(UsedRing));
            state->completion_ready = false;
            state->request_in_flight = false;
            state->last_used_idx = 0U;
            state->completion_status = StatusBusy;
            state->request_type = 0U;
            state->sector_count = 0U;
            state->request_sector = 0U;
            state->lock_word = 0U;

            status = find_device_slot(state);
            if (status != StatusOK) {
                return status;
            }
            status = negotiate_features(state);
            if (status != StatusOK) {
                return status;
            }
            status = setup_queue(state);
            if (status != StatusOK) {
                return status;
            }
            status = read_capacity(state);
            if (status != StatusOK) {
                return status;
            }

            status_register = read_reg(state->slot_index, VirtioMmioStatus) | VirtioStatusDriverOk;
            write_reg(state->slot_index, VirtioMmioStatus, status_register);
            state->ready = true;
            return StatusOK;
        }

        static Status init_device(Device* device) {
            return init_impl(static_cast<VirtioBlockState*>(device->state));
        }

        static Status deinit_device(Device* device) {
            VirtioBlockState* state = static_cast<VirtioBlockState*>(device->state);

            if (state == NULL) {
                return StatusInvalidArgument;
            }
            if (!state->ready) {
                return StatusOK;
            }

            write_reg(state->slot_index, VirtioMmioStatus, 0U);
            state->completion_ready = false;
            state->completion_status = StatusBusy;
            state->ready = false;
            state->request_in_flight = false;
            return StatusOK;
        }

        static SSize transfer(Device* device, U32 request_type, U64 offset, void* buffer, Size length) {
            VirtioBlockState* state = static_cast<VirtioBlockState*>(device->state);
            U8* bytes = static_cast<U8*>(buffer);
            Size sector_count;

            if ((state == NULL) || ((buffer == NULL) && (length != 0U))) {
                return StatusInvalidArgument;
            }
            if (length == 0U) {
                return 0;
            }
            if (((offset % SectorSize) != 0ULL) || ((length % SectorSize) != 0ULL)) {
                return StatusInvalidArgument;
            }

            if (!state->ready) {
                Status status = init_impl(state);

                if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                    return status;
                }
            }

            sector_count = length / SectorSize;
            for (Size index = 0; index < sector_count; ++index) {
                Status status = submit_request_sync(state, request_type, (offset / SectorSize) + index, bytes + (index * SectorSize));

                if (status != StatusOK) {
                    return status;
                }
            }

            return static_cast<SSize>(length);
        }

        static SSize read_device(Device* device, U64 offset, void* buffer, Size length) {
            return transfer(device, VirtioBlkTypeRead, offset, buffer, length);
        }

        static SSize write_device(Device* device, U64 offset, const void* buffer, Size length) {
            return transfer(device, VirtioBlkTypeWrite, offset, const_cast<void*>(buffer), length);
        }

        static Status ioctl_device(Device* device, U32 request, void* argument) {
            VirtioBlockState* state = static_cast<VirtioBlockState*>(device->state);

            if ((state == NULL) || (argument == NULL)) {
                return StatusInvalidArgument;
            }

            switch (request) {
            case IoctlGetSectorSize:
                *static_cast<U64*>(argument) = SectorSize;
                return StatusOK;
            case IoctlGetSectorCount:
                if (!state->ready) {
                    Status status = init_impl(state);

                    if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                        return status;
                    }
                }
                *static_cast<U64*>(argument) = state->sector_count;
                return StatusOK;
            default:
                return StatusNotSupported;
            }
        }

        inline static constexpr DeviceInterface Interface = {
            &init_device,
            &deinit_device,
            &read_device,
            &write_device,
            &ioctl_device,
        };

        inline static DeviceDriver Driver = {
            "virtio-blk",
            &Interface,
            NULL,
        };

        alignas(4096) inline static struct {
            VirtqDesc entries[QueueSize];
        } DescTable = {};

        alignas(4096) inline static VirtqAvail AvailRing = {};
        alignas(4096) inline static VirtqUsed UsedRing = {};
        inline static VirtioBlkRequest RequestHeader = {};
        inline static U8 StatusByte = 0U;
        inline static VirtioBlockState State = {};
        inline static Device DeviceInstance = {
            "disk0",
            NULL,
            &State,
            0U,
            &Driver,
        };
    };

} // namespace board

#endif // KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_BLOCK_BACKEND_H