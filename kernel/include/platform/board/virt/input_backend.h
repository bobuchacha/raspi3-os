#ifndef KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_INPUT_BACKEND_H
#define KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_INPUT_BACKEND_H

#include "device.h"
#include "framebuffer.h"
#include "gui_service.h"
#include "mm.h"
#include "platform/board/virt/serial_backend.h"

namespace board {

    class Input final {
    public:
        /**
         * Probe the virtio-input MMIO slots and prepare event queues for any
         * keyboard or pointer devices QEMU exposed on the `virt` machine.
         *
         * The probe intentionally runs during normal board bring-up so the
         * first real input event does not need to negotiate features from the
         * timer IRQ path.
         *
         * @return `StatusOK` when at least one input device is ready,
         * `StatusAlreadyExists` when the backend was already initialized,
         * or a non-fatal discovery status such as `StatusNotFound` when QEMU
         * did not expose any compatible input device.
         */
        static Status init(void) {
            U32 slot_index;

            if (ProbeComplete) {
                return AnyDeviceReady ? StatusAlreadyExists : StatusNotFound;
            }

            ProbeComplete = true;
            AnyDeviceReady = false;
            memzero(Devices, sizeof(Devices));
            DeviceCount = 0U;

            for (slot_index = 0U; (slot_index < MmioSlotCount) && (DeviceCount < COUNT_OF(Devices)); ++slot_index) {
                DeviceState* device = &Devices[DeviceCount];
                Status status;

                memzero(device, sizeof(*device));
                device->slot_index = slot_index;
                status = probe_device(device);
                if (status == StatusOK) {
                    AnyDeviceReady = true;
                    ++DeviceCount;
                }
            }

            return AnyDeviceReady ? StatusOK : StatusNotFound;
        }

        /**
         * Drain all ready virtio-input queues and publish normalized keyboard
         * and pointer events into the kernel GUI shared-input ring.
         *
         * The timer IRQ calls this once per scheduler tick. The method uses a
         * tiny lock so secondary CPUs do not process the same used ring in
         * parallel on SMP boots.
         *
         * @return Nothing.
         */
        static void poll(void) {
            U32 index;

            if (!ProbeComplete) {
                (void)init();
            }
            if (!AnyDeviceReady) {
                return;
            }
            if (!try_lock(&PollLockWord)) {
                return;
            }

            for (index = 0U; index < DeviceCount; ++index) {
                poll_device(&Devices[index]);
            }

            unlock(&PollLockWord);
        }

    private:
        enum : U32 {
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
            VirtioMmioConfig = 0x100U,
            VirtioMagic = 0x74726976U,
            VirtioVersionModern = 2U,
            VirtioDeviceIdInput = 18U,
            VirtioVendorQemu = 0x554D4551U,
            VirtioStatusAcknowledge = 0x01U,
            VirtioStatusDriver = 0x02U,
            VirtioStatusDriverOk = 0x04U,
            VirtioStatusFeaturesOk = 0x08U,
            VirtioStatusFailed = 0x80U,
            VirtioFeatureVersion1 = 32U,
            VirtqDescFlagWrite = 2U,
            VirtioInputCfgIdName = 0x01U,
            VirtioInputCfgEvBits = 0x11U,
            VirtioInputCfgAbsInfo = 0x12U,
            VirtioInputEventQueueIndex = 0U,
            VirtioInputStatusQueueIndex = 1U,
            VirtioInputEventTypeSync = 0x00U,
            VirtioInputEventTypeKey = 0x01U,
            VirtioInputEventTypeRel = 0x02U,
            VirtioInputEventTypeAbs = 0x03U,
            VirtioInputSynReport = 0U,
            VirtioInputRelX = 0U,
            VirtioInputRelY = 1U,
            VirtioInputAbsX = 0U,
            VirtioInputAbsY = 1U,
            VirtioInputButtonLeft = 272U,
            VirtioInputButtonTouch = 330U,
            VirtioInputKeyEsc = 1U,
            VirtioInputKey1 = 2U,
            VirtioInputKey2 = 3U,
            VirtioInputKey3 = 4U,
            VirtioInputKey4 = 5U,
            VirtioInputKey5 = 6U,
            VirtioInputKey6 = 7U,
            VirtioInputKey7 = 8U,
            VirtioInputKey8 = 9U,
            VirtioInputKey9 = 10U,
            VirtioInputKey0 = 11U,
            VirtioInputKeyMinus = 12U,
            VirtioInputKeyEqual = 13U,
            VirtioInputKeyBackspace = 14U,
            VirtioInputKeyTab = 15U,
            VirtioInputKeyQ = 16U,
            VirtioInputKeyW = 17U,
            VirtioInputKeyE = 18U,
            VirtioInputKeyR = 19U,
            VirtioInputKeyT = 20U,
            VirtioInputKeyY = 21U,
            VirtioInputKeyU = 22U,
            VirtioInputKeyI = 23U,
            VirtioInputKeyO = 24U,
            VirtioInputKeyP = 25U,
            VirtioInputKeyLeftBrace = 26U,
            VirtioInputKeyRightBrace = 27U,
            VirtioInputKeyEnter = 28U,
            VirtioInputKeyA = 30U,
            VirtioInputKeyS = 31U,
            VirtioInputKeyD = 32U,
            VirtioInputKeyF = 33U,
            VirtioInputKeyG = 34U,
            VirtioInputKeyH = 35U,
            VirtioInputKeyJ = 36U,
            VirtioInputKeyK = 37U,
            VirtioInputKeyL = 38U,
            VirtioInputKeySemicolon = 39U,
            VirtioInputKeyApostrophe = 40U,
            VirtioInputKeyGrave = 41U,
            VirtioInputKeyLeftShift = 42U,
            VirtioInputKeyBackslash = 43U,
            VirtioInputKeyZ = 44U,
            VirtioInputKeyX = 45U,
            VirtioInputKeyC = 46U,
            VirtioInputKeyV = 47U,
            VirtioInputKeyB = 48U,
            VirtioInputKeyN = 49U,
            VirtioInputKeyM = 50U,
            VirtioInputKeyComma = 51U,
            VirtioInputKeyDot = 52U,
            VirtioInputKeySlash = 53U,
            VirtioInputKeyRightShift = 54U,
            VirtioInputKeySpace = 57U,
            VirtioInputKeyKp0 = 82U,
            VirtioInputKeyKp1 = 79U,
            VirtioInputKeyKp2 = 80U,
            VirtioInputKeyKp3 = 81U,
            VirtioInputKeyKp4 = 75U,
            VirtioInputKeyKp5 = 76U,
            VirtioInputKeyKp6 = 77U,
            VirtioInputKeyKp7 = 71U,
            VirtioInputKeyKp8 = 72U,
            VirtioInputKeyKp9 = 73U,
            VirtioInputKeyKpDot = 83U,
            VirtioInputKeyKpEnter = 96U,
        };

        static constexpr Uptr MmioBase = 0x0A000000UL;
        static constexpr Uptr MmioStride = 0x200UL;
        static constexpr U32 MmioSlotCount = 32U;
        static constexpr U32 QueueSize = 16U;
        static constexpr U32 MaxDevices = 4U;
        static constexpr U32 DefaultWidth = 1024U;
        static constexpr U32 DefaultHeight = 768U;

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

        typedef struct __attribute__((packed)) VirtioInputEvent {
            U16 type;
            U16 code;
            U32 value;
        } VirtioInputEvent;

        typedef struct __attribute__((packed)) VirtioInputAbsInfo {
            I32 min;
            I32 max;
            I32 fuzz;
            I32 flat;
            I32 res;
        } VirtioInputAbsInfo;

        typedef struct __attribute__((packed)) VirtioInputConfig {
            U8 select;
            U8 subsel;
            U8 size;
            U8 reserved[5];
            union {
                char string[128];
                U8 bitmap[128];
                VirtioInputAbsInfo abs;
            } u;
        } VirtioInputConfig;

        typedef struct InputQueue {
            alignas(4096) VirtqDesc desc[QueueSize];
            alignas(4096) VirtqAvail avail;
            alignas(4096) VirtqUsed used;
            alignas(16) VirtioInputEvent events[QueueSize];
            U16 last_used_idx;
        } InputQueue;

        typedef struct DeviceState {
            bool ready;
            bool keyboard;
            bool pointer;
            bool absolute_pointer;
            bool relative_pointer;
            bool shift_down;
            bool button_down;
            bool last_button_down;
            bool have_abs_x;
            bool have_abs_y;
            bool pointer_initialized;
            U32 slot_index;
            I32 abs_min_x;
            I32 abs_max_x;
            I32 abs_min_y;
            I32 abs_max_y;
            I32 raw_pointer_x;
            I32 raw_pointer_y;
            I32 pending_rel_x;
            I32 pending_rel_y;
            U32 pointer_x;
            U32 pointer_y;
            U32 traced_event_count;
            U32 traced_pointer_commit_count;
            U32 traced_poll_count;
            char name[33];
            InputQueue event_queue;
            InputQueue status_queue;
        } DeviceState;

        /**
         * Emit one short virtio-input trace line to the early serial console.
         *
         * The input path currently depends on low-level MMIO discovery during
         * boot, so terse serial diagnostics are the fastest way to confirm
         * whether QEMU exposed keyboard/tablet devices and whether the driver
         * accepted them.
         *
         * @param text Null-terminated message body.
         * @return Nothing.
         */
        static void trace_line(const char* text) {
            Serial::puts_raw("[virtio-input] ");
            Serial::puts_raw(text);
            Serial::putc_raw('\n');
        }

        /**
         * Emit one unsigned decimal value to the early serial console.
         *
         * @param value Value to print.
         * @return Nothing.
         */
        static void trace_u32(U32 value) {
            char digits[11];
            U32 count = 0U;

            do {
                digits[count++] = static_cast<char>('0' + (value % 10U));
                value /= 10U;
            } while (value != 0U);

            while (count != 0U) {
                Serial::putc_raw(digits[--count]);
            }
        }

        /**
         * Emit one device-ready summary so boot logs capture the discovered
         * input topology without overwhelming the serial console.
         *
         * @param device Ready device state.
         * @return Nothing.
         */
        static void trace_ready_device(const DeviceState* device) {
            if (device == NULL) {
                return;
            }

            Serial::puts_raw("[virtio-input] ready slot=");
            trace_u32(device->slot_index);
            Serial::puts_raw(" kind=");
            if (device->keyboard) {
                Serial::puts_raw("keyboard");
                if (device->pointer) {
                    Serial::puts_raw("/");
                }
            }
            if (device->pointer) {
                Serial::puts_raw(device->absolute_pointer ? "tablet" : "mouse");
            }
            if (device->name[0] != '\0') {
                Serial::puts_raw(" name=");
                Serial::puts_raw(device->name);
            }
            Serial::putc_raw('\n');
        }

        /**
         * Emit one concise input-event trace for the first few packets a
         * device produces after boot.
         *
         * This keeps the serial log usable while still proving whether the
         * driver is draining the virt queue and seeing real keyboard/tablet
         * traffic after enumeration succeeds.
         *
         * @param device Device that produced the event.
         * @param event Input event payload copied from the virt queue.
         * @return Nothing.
         */
        static void trace_input_event(DeviceState* device, const VirtioInputEvent* event) {
            if ((device == NULL) || (event == NULL) || (device->traced_event_count >= 24U)) {
                return;
            }

            ++device->traced_event_count;
            Serial::puts_raw("[virtio-input] event slot=");
            trace_u32(device->slot_index);
            Serial::puts_raw(" type=");
            trace_u32(event->type);
            Serial::puts_raw(" code=");
            trace_u32(event->code);
            Serial::puts_raw(" value=");
            trace_u32(event->value);
            Serial::putc_raw('\n');
        }

        /**
         * Emit one concise pointer-commit trace for the first few desktop
         * pointer state updates that leave the kernel.
         *
         * @param device Pointer-producing device.
         * @param x Committed desktop X coordinate.
         * @param y Committed desktop Y coordinate.
         * @param buttons Pointer button mask.
         * @return Nothing.
         */
        static void trace_pointer_commit(DeviceState* device, U32 x, U32 y, U32 buttons) {
            if ((device == NULL) || (device->traced_pointer_commit_count >= 16U)) {
                return;
            }

            ++device->traced_pointer_commit_count;
            Serial::puts_raw("[virtio-input] commit slot=");
            trace_u32(device->slot_index);
            Serial::puts_raw(" x=");
            trace_u32(x);
            Serial::puts_raw(" y=");
            trace_u32(y);
            Serial::puts_raw(" buttons=");
            trace_u32(buttons);
            Serial::putc_raw('\n');
        }

        /**
         * Emit one low-volume queue-poll trace so runtime logs show whether the
         * driver is actively draining descriptors or spinning on an idle queue.
         *
         * @param device Device being polled.
         * @param phase Short poll phase label such as `used`, `irq`, or `idle`.
         * @param interrupt_status Raw virtio interrupt status value.
         * @param used_index Current used-ring producer index.
         * @return Nothing.
         */
        static void trace_poll_state(DeviceState* device, const char* phase, U32 interrupt_status, U16 used_index) {
            if ((device == NULL) || (phase == NULL)) {
                return;
            }

            Serial::puts_raw("[virtio-input] poll slot=");
            trace_u32(device->slot_index);
            Serial::puts_raw(" phase=");
            Serial::puts_raw(phase);
            Serial::puts_raw(" irq=");
            trace_u32(interrupt_status);
            Serial::puts_raw(" used=");
            trace_u32(static_cast<U32>(used_index));
            Serial::puts_raw(" last=");
            trace_u32(static_cast<U32>(device->event_queue.last_used_idx));
            Serial::putc_raw('\n');
        }

        /**
         * Move one virtio-input device into the minimal acknowledged-driver
         * state before capability queries touch its configuration space.
         *
         * The older working virt backend performed this handshake before
         * reading event-bit and abs-info pages. Matching that ordering keeps
         * the modern backend consistent with devices that only expose valid
         * config data after the driver announces itself.
         *
         * @param device Device state being prepared.
         * @return `StatusOK` when the status register was primed.
         */
        static Status begin_device_setup(DeviceState* device) {
            if (device == NULL) {
                return StatusInvalidArgument;
            }

            write_reg(device->slot_index, VirtioMmioStatus, 0U);
            write_reg(device->slot_index, VirtioMmioStatus, VirtioStatusAcknowledge | VirtioStatusDriver);
            return StatusOK;
        }

        /**
         * Translate one kernel virtual address into the physical address that
         * virtio MMIO queue registers expect.
         *
         * @param address Kernel virtual address.
         * @return Physical address visible to the device.
         */
        static PhysAddr phys_addr(const void* address) {
            return mm::MemoryManager::kernel_to_physical(reinterpret_cast<VirtAddr>(address));
        }

        /**
         * Insert one device-memory barrier so queue writes become visible to
         * the virtio device before the notify doorbell is raised.
         *
         * @return Nothing.
         */
        static void barrier(void) {
            __asm__ volatile("dmb ish" ::: "memory");
        }

        /**
         * Acquire the polling lock without blocking.
         *
         * @param lock_word Address of the shared lock word.
         * @return `true` when the caller owns the lock, or `false` when some
         * other CPU is already polling the queues.
         */
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

        /**
         * Release the polling lock after one IRQ-driven queue drain pass.
         *
         * @param lock_word Address of the shared lock word.
         * @return Nothing.
         */
        static void unlock(volatile U32* lock_word) {
            __asm__ volatile(
                "stlr %w1, [%0]\n"
                :
            : "r"(lock_word), "r"(0U)
                : "memory");
        }

        /**
         * Compute one typed MMIO register address for a virtio-input slot.
         *
         * @param slot_index MMIO slot index.
         * @param offset Register offset within the slot.
         * @return Pointer to the register.
         */
        static volatile U32* reg(U32 slot_index, Uptr offset) {
            return reinterpret_cast<volatile U32*>(MmioBase + (static_cast<Uptr>(slot_index) * MmioStride) + offset);
        }

        /**
         * Read one 32-bit MMIO register from the selected slot.
         *
         * @param slot_index MMIO slot index.
         * @param offset Register offset within the slot.
         * @return Raw register value.
         */
        static U32 read_reg(U32 slot_index, Uptr offset) {
            return *reg(slot_index, offset);
        }

        /**
         * Write one 32-bit MMIO register in the selected slot.
         *
         * @param slot_index MMIO slot index.
         * @param offset Register offset within the slot.
         * @param value Value to write.
         * @return Nothing.
         */
        static void write_reg(U32 slot_index, Uptr offset, U32 value) {
            *reg(slot_index, offset) = value;
        }

        /**
         * Program one split 64-bit virtio queue address register pair.
         *
         * @param slot_index MMIO slot index.
         * @param low_offset Low 32-bit register offset.
         * @param high_offset High 32-bit register offset.
         * @param address Physical queue address.
         * @return Nothing.
         */
        static void write_addr(U32 slot_index, U32 low_offset, U32 high_offset, PhysAddr address) {
            write_reg(slot_index, low_offset, static_cast<U32>(address & 0xFFFFFFFFULL));
            write_reg(slot_index, high_offset, static_cast<U32>(address >> 32));
        }

        /**
         * Address the device configuration structure that exposes capability
         * bitmaps and absolute-axis ranges.
         *
         * @param slot_index MMIO slot index.
         * @return Pointer to the volatile configuration window.
         */
        static volatile VirtioInputConfig* config_reg(U32 slot_index) {
            return reinterpret_cast<volatile VirtioInputConfig*>(MmioBase + (static_cast<Uptr>(slot_index) * MmioStride) + VirtioMmioConfig);
        }

        /**
         * Select one virtio-input configuration page.
         *
         * @param slot_index MMIO slot index.
         * @param select Top-level config selector.
         * @param subsel Sub-selector used by event-bit and abs-info queries.
         * @return Nothing.
         */
        static void config_select(U32 slot_index, U8 select, U8 subsel) {
            volatile VirtioInputConfig* config = config_reg(slot_index);

            config->select = select;
            config->subsel = subsel;
            barrier();
        }

        /**
         * Read one capability bitmap from the selected device.
         *
         * @param slot_index MMIO slot index.
         * @param select Bitmap selector.
         * @param subsel Bitmap sub-selector.
         * @param out_bitmap Destination bitmap storage.
         * @return Number of valid bytes QEMU reported.
         */
        static U8 query_bitmap(U32 slot_index, U8 select, U8 subsel, U8* out_bitmap) {
            volatile VirtioInputConfig* config = config_reg(slot_index);
            U8 size;

            config_select(slot_index, select, subsel);
            size = config->size;
            if (out_bitmap != NULL) {
                U32 index;

                memzero(out_bitmap, 128U);
                for (index = 0U; (index < size) && (index < 128U); ++index) {
                    out_bitmap[index] = config->u.bitmap[index];
                }
            }

            return size;
        }

        /**
         * Read one absolute-axis range descriptor.
         *
         * @param slot_index MMIO slot index.
         * @param axis Axis identifier such as `ABS_X` or `ABS_Y`.
         * @param out_abs Destination descriptor.
         * @return `true` when the device exposed valid absolute-axis data.
         */
        static bool query_absinfo(U32 slot_index, U8 axis, VirtioInputAbsInfo* out_abs) {
            volatile VirtioInputConfig* config = config_reg(slot_index);

            if (out_abs == NULL) {
                return false;
            }

            config_select(slot_index, VirtioInputCfgAbsInfo, axis);
            if (config->size < sizeof(VirtioInputAbsInfo)) {
                memzero(out_abs, sizeof(*out_abs));
                return false;
            }

            out_abs->min = config->u.abs.min;
            out_abs->max = config->u.abs.max;
            out_abs->fuzz = config->u.abs.fuzz;
            out_abs->flat = config->u.abs.flat;
            out_abs->res = config->u.abs.res;
            return true;
        }

        /**
         * Read the human-readable QEMU device name for diagnostics and future
         * debugging support.
         *
         * @param slot_index MMIO slot index.
         * @param out_name Destination string buffer.
         * @param capacity Output buffer size in bytes.
         * @return Nothing.
         */
        static void query_name(U32 slot_index, char* out_name, U32 capacity) {
            volatile VirtioInputConfig* config = config_reg(slot_index);
            U8 size;
            U32 index;

            if ((out_name == NULL) || (capacity == 0U)) {
                return;
            }

            config_select(slot_index, VirtioInputCfgIdName, 0U);
            size = config->size;
            if (size >= capacity) {
                size = static_cast<U8>(capacity - 1U);
            }

            for (index = 0U; index < size; ++index) {
                out_name[index] = config->u.string[index];
            }
            out_name[size] = '\0';
        }

        /**
         * Test whether one bit is present in a variable-length capability
         * bitmap returned by the device.
         *
         * @param bitmap Device-supplied bitmap bytes.
         * @param size Number of valid bytes in the bitmap.
         * @param bit_index Capability bit index to test.
         * @return `true` when the bit is set.
         */
        static bool bitmap_has(const U8* bitmap, U8 size, U32 bit_index) {
            U32 byte_index = bit_index / 8U;
            U8 bit_mask = static_cast<U8>(1U << (bit_index % 8U));

            return (bitmap != NULL) && (byte_index < size) && ((bitmap[byte_index] & bit_mask) != 0U);
        }

        /**
         * Query framebuffer geometry so absolute tablets and relative mice use
         * the same desktop bounds GWES presents to user mode.
         *
         * @param out_width Destination width.
         * @param out_height Destination height.
         * @return Nothing. The method falls back to the virt RAMFB default when
         * geometry is temporarily unavailable.
         */
        static void query_display_geometry(U32* out_width, U32* out_height) {
            Device* framebuffer_device = DeviceManager::find_device("framebuffer0");
            FramebufferGeometry geometry;
            Status status;

            if (out_width == NULL || out_height == NULL) {
                return;
            }

            *out_width = DefaultWidth;
            *out_height = DefaultHeight;
            if (framebuffer_device == NULL) {
                return;
            }

            memzero(&geometry, sizeof(geometry));
            status = framebuffer_device->ioctl(FramebufferIoctlGetGeometry, &geometry);
            if ((status != StatusOK) || (geometry.width == 0U) || (geometry.height == 0U)) {
                return;
            }

            *out_width = geometry.width;
            *out_height = geometry.height;
        }

        /**
         * Clamp one pointer coordinate into the current desktop extent.
         *
         * @param value Raw coordinate candidate.
         * @param limit Exclusive dimension bound.
         * @return Valid desktop coordinate.
         */
        static U32 clamp_axis(I32 value, U32 limit) {
            if (limit == 0U) {
                return 0U;
            }
            if (value < 0) {
                return 0U;
            }
            if (static_cast<U32>(value) >= limit) {
                return limit - 1U;
            }

            return static_cast<U32>(value);
        }

        /**
         * Convert one absolute tablet coordinate into desktop pixels.
         *
         * @param value Raw device coordinate.
         * @param min_value Device minimum.
         * @param max_value Device maximum.
         * @param limit Exclusive desktop dimension bound.
         * @return Scaled desktop coordinate.
         */
        static U32 scale_axis(I32 value, I32 min_value, I32 max_value, U32 limit) {
            I64 span;
            I64 offset;

            if ((limit <= 1U) || (max_value <= min_value)) {
                return 0U;
            }
            if (value < min_value) {
                value = min_value;
            }
            if (value > max_value) {
                value = max_value;
            }

            span = static_cast<I64>(max_value) - static_cast<I64>(min_value);
            offset = static_cast<I64>(value) - static_cast<I64>(min_value);
            return static_cast<U32>((offset * static_cast<I64>(limit - 1U)) / span);
        }

        /**
         * Translate one Linux input key code into the ASCII/control value the
         * current widget runtime already expects in `WM_KEYDOWN`.
         *
         * @param code Linux input key code.
         * @param shift_down Whether Shift is currently held.
         * @return ASCII or control byte, or `0` when the key has no current
         * mapping in the minimal widget runtime.
         */
        static U32 map_key(U32 code, bool shift_down) {
            switch (code) {
            case VirtioInputKeyQ:
                return shift_down ? 'Q' : 'q';
            case VirtioInputKeyW:
                return shift_down ? 'W' : 'w';
            case VirtioInputKeyE:
                return shift_down ? 'E' : 'e';
            case VirtioInputKeyR:
                return shift_down ? 'R' : 'r';
            case VirtioInputKeyT:
                return shift_down ? 'T' : 't';
            case VirtioInputKeyY:
                return shift_down ? 'Y' : 'y';
            case VirtioInputKeyU:
                return shift_down ? 'U' : 'u';
            case VirtioInputKeyI:
                return shift_down ? 'I' : 'i';
            case VirtioInputKeyO:
                return shift_down ? 'O' : 'o';
            case VirtioInputKeyP:
                return shift_down ? 'P' : 'p';
            case VirtioInputKeyA:
                return shift_down ? 'A' : 'a';
            case VirtioInputKeyS:
                return shift_down ? 'S' : 's';
            case VirtioInputKeyD:
                return shift_down ? 'D' : 'd';
            case VirtioInputKeyF:
                return shift_down ? 'F' : 'f';
            case VirtioInputKeyG:
                return shift_down ? 'G' : 'g';
            case VirtioInputKeyH:
                return shift_down ? 'H' : 'h';
            case VirtioInputKeyJ:
                return shift_down ? 'J' : 'j';
            case VirtioInputKeyK:
                return shift_down ? 'K' : 'k';
            case VirtioInputKeyL:
                return shift_down ? 'L' : 'l';
            case VirtioInputKeyZ:
                return shift_down ? 'Z' : 'z';
            case VirtioInputKeyX:
                return shift_down ? 'X' : 'x';
            case VirtioInputKeyC:
                return shift_down ? 'C' : 'c';
            case VirtioInputKeyV:
                return shift_down ? 'V' : 'v';
            case VirtioInputKeyB:
                return shift_down ? 'B' : 'b';
            case VirtioInputKeyN:
                return shift_down ? 'N' : 'n';
            case VirtioInputKeyM:
                return shift_down ? 'M' : 'm';
            case VirtioInputKeyEsc:
                return 27U;
            case VirtioInputKey1:
                return shift_down ? '!' : '1';
            case VirtioInputKey2:
                return shift_down ? '@' : '2';
            case VirtioInputKey3:
                return shift_down ? '#' : '3';
            case VirtioInputKey4:
                return shift_down ? '$' : '4';
            case VirtioInputKey5:
                return shift_down ? '%' : '5';
            case VirtioInputKey6:
                return shift_down ? '^' : '6';
            case VirtioInputKey7:
                return shift_down ? '&' : '7';
            case VirtioInputKey8:
                return shift_down ? '*' : '8';
            case VirtioInputKey9:
                return shift_down ? '(' : '9';
            case VirtioInputKey0:
                return shift_down ? ')' : '0';
            case VirtioInputKeyMinus:
                return shift_down ? '_' : '-';
            case VirtioInputKeyEqual:
                return shift_down ? '+' : '=';
            case VirtioInputKeyBackspace:
                return 8U;
            case VirtioInputKeyTab:
                return '\t';
            case VirtioInputKeyLeftBrace:
                return shift_down ? '{' : '[';
            case VirtioInputKeyRightBrace:
                return shift_down ? '}' : ']';
            case VirtioInputKeyEnter:
            case VirtioInputKeyKpEnter:
                return '\n';
            case VirtioInputKeySemicolon:
                return shift_down ? ':' : ';';
            case VirtioInputKeyApostrophe:
                return shift_down ? '"' : '\'';
            case VirtioInputKeyGrave:
                return shift_down ? '~' : '`';
            case VirtioInputKeyBackslash:
                return shift_down ? '|' : '\\';
            case VirtioInputKeyComma:
                return shift_down ? '<' : ',';
            case VirtioInputKeyDot:
            case VirtioInputKeyKpDot:
                return shift_down ? '>' : '.';
            case VirtioInputKeySlash:
                return shift_down ? '?' : '/';
            case VirtioInputKeySpace:
                return ' ';
            case VirtioInputKeyKp0:
                return '0';
            case VirtioInputKeyKp1:
                return '1';
            case VirtioInputKeyKp2:
                return '2';
            case VirtioInputKeyKp3:
                return '3';
            case VirtioInputKeyKp4:
                return '4';
            case VirtioInputKeyKp5:
                return '5';
            case VirtioInputKeyKp6:
                return '6';
            case VirtioInputKeyKp7:
                return '7';
            case VirtioInputKeyKp8:
                return '8';
            case VirtioInputKeyKp9:
                return '9';
            default:
                return 0U;
            }
        }

        /**
         * Publish one pointer event into the kernel GUI shared-input ring.
         *
         * @param type Shared-input event type.
         * @param x Desktop X coordinate.
         * @param y Desktop Y coordinate.
         * @param buttons Pointer button mask.
         * @return Nothing.
         */
        static void publish_pointer_event(U32 type, U32 x, U32 y, U32 buttons) {
            GuiService::publish_input_event(type, x, y, 0U, buttons);
        }

        /**
         * Publish one mapped keyboard event into the shared-input ring.
         *
         * @param type Shared-input key event type.
         * @param key ASCII/control key value.
         * @return Nothing.
         */
        static void publish_key_event(U32 type, U32 key) {
            GuiService::publish_input_event(type, 0U, 0U, key, 0U);
        }

        /**
         * Emit the normalized pointer state changes accumulated since the last
         * SYN_REPORT boundary.
         *
         * @param device Device state being committed.
         * @return Nothing.
         */
        static void commit_pointer(DeviceState* device) {
            U32 width;
            U32 height;
            U32 x;
            U32 y;
            bool moved;
            bool button_changed;

            if ((device == NULL) || !device->pointer) {
                return;
            }

            query_display_geometry(&width, &height);
            if ((width == 0U) || (height == 0U)) {
                return;
            }

            if (device->absolute_pointer) {
                x = scale_axis(device->raw_pointer_x, device->abs_min_x, device->abs_max_x, width);
                y = scale_axis(device->raw_pointer_y, device->abs_min_y, device->abs_max_y, height);
            }
            else {
                x = clamp_axis(static_cast<I32>(device->pointer_x) + device->pending_rel_x, width);
                y = clamp_axis(static_cast<I32>(device->pointer_y) + device->pending_rel_y, height);
            }

            device->pending_rel_x = 0;
            device->pending_rel_y = 0;
            moved = !device->pointer_initialized || (x != device->pointer_x) || (y != device->pointer_y);
            button_changed = device->button_down != device->last_button_down;
            device->pointer_x = x;
            device->pointer_y = y;
            device->pointer_initialized = true;
            if (moved || button_changed) {
                trace_pointer_commit(device, x, y, device->button_down ? 1U : 0U);
            }

            if (moved) {
                publish_pointer_event(ROS_KERNEL_GUI_INPUT_EVENT_POINTER_MOVE, x, y, device->button_down ? 1U : 0U);
            }
            if (!device->last_button_down && device->button_down) {
                publish_pointer_event(ROS_KERNEL_GUI_INPUT_EVENT_POINTER_DOWN, x, y, 1U);
            }
            else if (device->last_button_down && !device->button_down) {
                publish_pointer_event(ROS_KERNEL_GUI_INPUT_EVENT_POINTER_UP, x, y, 0U);
            }
            else if (button_changed) {
                publish_pointer_event(device->button_down ? ROS_KERNEL_GUI_INPUT_EVENT_POINTER_DOWN : ROS_KERNEL_GUI_INPUT_EVENT_POINTER_UP,
                    x,
                    y,
                    device->button_down ? 1U : 0U);
            }

            device->last_button_down = device->button_down;
        }

        /**
         * Consume one virtio event and fold it into the current keyboard or
         * pointer state machine for the device.
         *
         * @param device Device state receiving the event.
         * @param event One used-ring event payload.
         * @return Nothing.
         */
        static void process_event(DeviceState* device, const VirtioInputEvent* event) {
            U32 mapped_key;

            if ((device == NULL) || (event == NULL)) {
                return;
            }

            trace_input_event(device, event);

            if (device->keyboard && (event->type == VirtioInputEventTypeKey)) {
                if ((event->code == VirtioInputKeyLeftShift) || (event->code == VirtioInputKeyRightShift)) {
                    device->shift_down = event->value != 0U;
                    return;
                }

                mapped_key = map_key(event->code, device->shift_down);
                if (mapped_key != 0U) {
                    if (event->value == 0U) {
                        publish_key_event(ROS_KERNEL_GUI_INPUT_EVENT_KEY_UP, mapped_key);
                    }
                    else if ((event->value == 1U) || (event->value == 2U)) {
                        (void)Serial::inject_key(static_cast<char>(mapped_key));
                        publish_key_event(ROS_KERNEL_GUI_INPUT_EVENT_KEY_DOWN, mapped_key);
                    }
                }
                return;
            }

            if (!device->pointer) {
                return;
            }

            switch (event->type) {
            case VirtioInputEventTypeKey:
                if ((event->code == VirtioInputButtonLeft) || (event->code == VirtioInputButtonTouch)) {
                    device->button_down = event->value != 0U;
                }
                break;
            case VirtioInputEventTypeRel:
                if (event->code == VirtioInputRelX) {
                    device->pending_rel_x += static_cast<I32>(event->value);
                }
                else if (event->code == VirtioInputRelY) {
                    device->pending_rel_y += static_cast<I32>(event->value);
                }
                break;
            case VirtioInputEventTypeAbs:
                if (event->code == VirtioInputAbsX) {
                    device->raw_pointer_x = static_cast<I32>(event->value);
                    device->have_abs_x = true;
                }
                else if (event->code == VirtioInputAbsY) {
                    device->raw_pointer_y = static_cast<I32>(event->value);
                    device->have_abs_y = true;
                }
                break;
            case VirtioInputEventTypeSync:
                if (event->code == VirtioInputSynReport) {
                    commit_pointer(device);
                }
                break;
            default:
                break;
            }
        }

        /**
         * Requeue one event descriptor so the device can write the next input
         * packet into the same pre-allocated slot.
         *
         * @param device Device state owning the queue.
         * @param descriptor_id Descriptor index to recycle.
         * @return Nothing.
         */
        static void refill_descriptor(DeviceState* device, U32 descriptor_id) {
            InputQueue* queue;

            if ((device == NULL) || (descriptor_id >= QueueSize)) {
                return;
            }

            queue = &device->event_queue;
            queue->avail.ring[queue->avail.idx % QueueSize] = static_cast<U16>(descriptor_id);
            barrier();
            ++queue->avail.idx;
        }

        /**
         * Ring the event queue doorbell after recycled descriptors were added
         * back to the available ring.
         *
         * @param slot_index MMIO slot index.
         * @return Nothing.
         */
        static void notify_queue(U32 slot_index) {
            write_reg(slot_index, VirtioMmioQueueSel, VirtioInputEventQueueIndex);
            barrier();
            write_reg(slot_index, VirtioMmioQueueNotify, VirtioInputEventQueueIndex);
        }

        /**
         * Drain all used descriptors for one device.
         *
         * @param device Device state to poll.
         * @return Nothing.
         */
        static void poll_device(DeviceState* device) {
            InputQueue* queue;
            volatile VirtqUsed* used_ring;
            U32 interrupt_status;
            bool queue_changed = false;

            if ((device == NULL) || !device->ready) {
                return;
            }

            interrupt_status = read_reg(device->slot_index, VirtioMmioInterruptStatus);
            if (interrupt_status != 0U) {
                write_reg(device->slot_index, VirtioMmioInterruptAck, interrupt_status);
            }

            queue = &device->event_queue;
            used_ring = &queue->used;
            ++device->traced_poll_count;
            barrier();
            while (used_ring->idx != queue->last_used_idx) {
                U32 used_slot = queue->last_used_idx % QueueSize;
                U32 descriptor_id = used_ring->ring[used_slot].id;

                trace_poll_state(device, "used", interrupt_status, used_ring->idx);

                if (descriptor_id < QueueSize) {
                    VirtioInputEvent event;
                    volatile VirtioInputEvent* source_event = &queue->events[descriptor_id];

                    barrier();
                    event.type = source_event->type;
                    event.code = source_event->code;
                    event.value = source_event->value;
                    process_event(device, &event);
                    refill_descriptor(device, descriptor_id);
                    queue_changed = true;
                }
                ++queue->last_used_idx;
                barrier();
            }

            if (interrupt_status != 0U) {
                trace_poll_state(device, "irq", interrupt_status, used_ring->idx);
            }
            else if ((device->traced_poll_count <= 16U)
                || (device->traced_poll_count == 1024U)
                || ((device->traced_poll_count % 65536U) == 0U)) {
                trace_poll_state(device, "idle", interrupt_status, used_ring->idx);
            }

            if (queue_changed) {
                notify_queue(device->slot_index);
            }
        }

        /**
         * Classify the probed device as keyboard, relative mouse, or absolute
         * pointer by consulting its event capability bitmaps.
         *
         * @param device Device state to populate.
         * @return Nothing.
         */
        static void classify_device(DeviceState* device) {
            U8 key_bits[128];
            U8 rel_bits[128];
            U8 abs_bits[128];
            U8 key_size;
            U8 rel_size;
            U8 abs_size;
            VirtioInputAbsInfo abs_info;
            U32 width;
            U32 height;

            if (device == NULL) {
                return;
            }

            key_size = query_bitmap(device->slot_index, VirtioInputCfgEvBits, VirtioInputEventTypeKey, key_bits);
            rel_size = query_bitmap(device->slot_index, VirtioInputCfgEvBits, VirtioInputEventTypeRel, rel_bits);
            abs_size = query_bitmap(device->slot_index, VirtioInputCfgEvBits, VirtioInputEventTypeAbs, abs_bits);

            device->keyboard = (key_size != 0U) && bitmap_has(key_bits, key_size, VirtioInputKeyA) && bitmap_has(key_bits, key_size, VirtioInputKeyEnter);
            device->relative_pointer = (rel_size != 0U) && bitmap_has(rel_bits, rel_size, VirtioInputRelX) && bitmap_has(rel_bits, rel_size, VirtioInputRelY);
            device->absolute_pointer = (abs_size != 0U) && bitmap_has(abs_bits, abs_size, VirtioInputAbsX) && bitmap_has(abs_bits, abs_size, VirtioInputAbsY);
            device->pointer = device->relative_pointer || device->absolute_pointer;

            if (device->absolute_pointer) {
                if (query_absinfo(device->slot_index, VirtioInputAbsX, &abs_info)) {
                    device->abs_min_x = abs_info.min;
                    device->abs_max_x = abs_info.max;
                }
                if (query_absinfo(device->slot_index, VirtioInputAbsY, &abs_info)) {
                    device->abs_min_y = abs_info.min;
                    device->abs_max_y = abs_info.max;
                }
            }

            query_display_geometry(&width, &height);
            device->pointer_x = width / 2U;
            device->pointer_y = height / 2U;
            device->raw_pointer_x = device->abs_min_x;
            device->raw_pointer_y = device->abs_min_y;
            query_name(device->slot_index, device->name, sizeof(device->name));
        }

        /**
         * Negotiate the modern virtio feature set required by the active
         * backend and reject devices that do not support `VERSION_1`.
         *
         * @param device Device state being negotiated.
         * @return `StatusOK` on success, or a status describing why the device
         * was rejected.
         */
        static Status negotiate_features(DeviceState* device) {
            U32 status;
            U32 features_hi;

            if (device == NULL) {
                return StatusInvalidArgument;
            }
            if (read_reg(device->slot_index, VirtioMmioVendorId) != VirtioVendorQemu) {
                return StatusNotSupported;
            }

            status = read_reg(device->slot_index, VirtioMmioStatus);
            if ((status & (VirtioStatusAcknowledge | VirtioStatusDriver)) != (VirtioStatusAcknowledge | VirtioStatusDriver)) {
                status = VirtioStatusAcknowledge | VirtioStatusDriver;
                write_reg(device->slot_index, VirtioMmioStatus, status);
            }

            write_reg(device->slot_index, VirtioMmioDeviceFeaturesSel, 1U);
            features_hi = read_reg(device->slot_index, VirtioMmioDeviceFeatures);
            if ((features_hi & (1U << (VirtioFeatureVersion1 - 32U))) == 0U) {
                write_reg(device->slot_index, VirtioMmioStatus, status | VirtioStatusFailed);
                return StatusNotSupported;
            }

            write_reg(device->slot_index, VirtioMmioDriverFeaturesSel, 0U);
            write_reg(device->slot_index, VirtioMmioDriverFeatures, 0U);
            write_reg(device->slot_index, VirtioMmioDriverFeaturesSel, 1U);
            write_reg(device->slot_index, VirtioMmioDriverFeatures, (1U << (VirtioFeatureVersion1 - 32U)));

            status |= VirtioStatusFeaturesOk;
            write_reg(device->slot_index, VirtioMmioStatus, status);
            if ((read_reg(device->slot_index, VirtioMmioStatus) & VirtioStatusFeaturesOk) == 0U) {
                write_reg(device->slot_index, VirtioMmioStatus, status | VirtioStatusFailed);
                return StatusNotSupported;
            }

            return StatusOK;
        }

        /**
         * Program the event queue descriptors and place every descriptor in the
         * available ring so QEMU can start writing input packets immediately.
         *
         * @param device Device state being configured.
         * @return `StatusOK` when the queue is live.
         */
        static Status setup_queue(DeviceState* device, InputQueue* queue, U32 queue_index, bool populate_events) {
            U32 queue_limit;
            U32 descriptor_index;

            if ((device == NULL) || (queue == NULL)) {
                return StatusInvalidArgument;
            }

            memzero(queue, sizeof(*queue));

            write_reg(device->slot_index, VirtioMmioQueueSel, queue_index);
            queue_limit = read_reg(device->slot_index, VirtioMmioQueueNumMax);
            if (queue_limit < QueueSize) {
                write_reg(device->slot_index, VirtioMmioStatus, read_reg(device->slot_index, VirtioMmioStatus) | VirtioStatusFailed);
                return StatusNotSupported;
            }

            write_reg(device->slot_index, VirtioMmioQueueNum, QueueSize);
            write_addr(device->slot_index, VirtioMmioQueueDescLow, VirtioMmioQueueDescHigh, phys_addr(queue->desc));
            write_addr(device->slot_index, VirtioMmioQueueAvailLow, VirtioMmioQueueAvailHigh, phys_addr(&queue->avail));
            write_addr(device->slot_index, VirtioMmioQueueUsedLow, VirtioMmioQueueUsedHigh, phys_addr(&queue->used));
            write_reg(device->slot_index, VirtioMmioQueueReady, 1U);

            if (populate_events) {
                for (descriptor_index = 0U; descriptor_index < QueueSize; ++descriptor_index) {
                    queue->desc[descriptor_index].addr = phys_addr(&queue->events[descriptor_index]);
                    queue->desc[descriptor_index].len = sizeof(VirtioInputEvent);
                    queue->desc[descriptor_index].flags = VirtqDescFlagWrite;
                    queue->desc[descriptor_index].next = 0U;
                    queue->avail.ring[descriptor_index] = static_cast<U16>(descriptor_index);
                }
                barrier();
                queue->avail.idx = QueueSize;
                barrier();
                notify_queue(device->slot_index);
            }

            return StatusOK;
        }

        /**
         * Program the event queue descriptors and place every descriptor in the
         * available ring so QEMU can start writing input packets immediately.
         *
         * @param device Device state being configured.
         * @return `StatusOK` when the queue is live.
         */
        static Status setup_event_queue(DeviceState* device) {
            return setup_queue(device, &device->event_queue, VirtioInputEventQueueIndex, true);
        }

        /**
         * Program the status queue required by the virtio-input transport.
         *
         * The current kernel does not send status updates such as LED changes,
         * but the queue still needs a valid transport setup before the device
         * is marked driver-ready.
         *
         * @param device Device state being configured.
         * @return `StatusOK` when the queue is live.
         */
        static Status setup_status_queue(DeviceState* device) {
            return setup_queue(device, &device->status_queue, VirtioInputStatusQueueIndex, false);
        }

        /**
         * Probe and fully configure one virtio-input MMIO slot.
         *
         * @param device Destination device state.
         * @return `StatusOK` when the slot exposes a supported input device.
         */
        static Status probe_device(DeviceState* device) {
            U32 version;
            Status status;

            if (device == NULL) {
                return StatusInvalidArgument;
            }
            if (read_reg(device->slot_index, VirtioMmioMagicValue) != VirtioMagic) {
                return StatusNotFound;
            }

            version = read_reg(device->slot_index, VirtioMmioVersion);
            if (version != VirtioVersionModern) {
                return StatusNotSupported;
            }
            if (read_reg(device->slot_index, VirtioMmioDeviceId) != VirtioDeviceIdInput) {
                return StatusNotFound;
            }

            status = begin_device_setup(device);
            if (status != StatusOK) {
                return status;
            }

            classify_device(device);
            if (!device->keyboard && !device->pointer) {
                return StatusNotSupported;
            }

            status = negotiate_features(device);
            if (status != StatusOK) {
                return status;
            }

            status = setup_event_queue(device);
            if (status != StatusOK) {
                return status;
            }

            status = setup_status_queue(device);
            if (status != StatusOK) {
                return status;
            }

            write_reg(device->slot_index, VirtioMmioStatus, read_reg(device->slot_index, VirtioMmioStatus) | VirtioStatusDriverOk);
            barrier();
            notify_queue(device->slot_index);
            device->ready = true;
            trace_ready_device(device);
            return StatusOK;
        }

        inline static DeviceState Devices[MaxDevices];
        inline static U32 DeviceCount;
        inline static bool ProbeComplete;
        inline static bool AnyDeviceReady;
        inline static volatile U32 PollLockWord;
    };

} // namespace board

#endif // KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_INPUT_BACKEND_H