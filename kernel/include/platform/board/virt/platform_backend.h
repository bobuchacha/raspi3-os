#ifndef KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_PLATFORM_BACKEND_H
#define KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_PLATFORM_BACKEND_H

#include "arch.h"
#include "device.h"
#include "kernel_time.h"
#include "mm.h"
#include "platform_descriptor.h"
#include "platform/board/virt/block_backend.h"
#include "platform/board/virt/framebuffer_backend.h"
#include "platform/board/virt/input_backend.h"
#include "platform/board/virt/serial_backend.h"

namespace board {

    class Platform final {
    public:
        static const PlatformDescriptor& descriptor(void) {
            return Descriptor;
        }

        static Status early_init(void) {
            return StatusOK;
        }

        static Status init_devices(void) {
            Status status = DeviceManager::init();

            if (status != StatusOK) {
                return status;
            }

            status = DeviceManager::register_driver(&SerialDriver);
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            status = DeviceManager::register_device(&SerialDevice);
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            status = DeviceManager::bind_driver(&SerialDevice, &SerialDriver);
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            status = DeviceManager::register_driver(&TimerDriver);
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            status = DeviceManager::register_device(&TimerDevice);
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            status = DeviceManager::bind_driver(&TimerDevice, &TimerDriver);
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            status = DeviceManager::register_driver(BlockStorage::driver());
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            status = DeviceManager::register_device(BlockStorage::device());
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            status = DeviceManager::bind_driver(BlockStorage::device(), BlockStorage::driver());
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            status = DeviceManager::register_driver(Framebuffer::driver());
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            status = DeviceManager::register_device(Framebuffer::device());
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            status = DeviceManager::bind_driver(Framebuffer::device(), Framebuffer::driver());
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            status = Input::init();
            if ((status != StatusOK) && (status != StatusAlreadyExists) && (status != StatusNotFound) && (status != StatusNotSupported)) {
                return status;
            }

            status = init_interrupt_controller();
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            return StatusOK;
        }

        static Status init_timer(void) {
            return TimerDevice.init();
        }

        /**
         * Run timer-polled board backends so shared IRQ code can stay board
         * agnostic while still draining virtio-input queues promptly.
         *
         * @return Nothing.
         */
        static void poll_devices(void) {
            Input::poll();
            // The block backend still exposes synchronous reads, but its
            // completion path is now split from queue submission so this poll
            // hook can drain one used-ring update when no thread currently owns
            // the driver lock.
            BlockStorage::poll();
        }

        /**
         * Acknowledge one pending board IRQ and return its interrupt ID.
         *
         * The active `virt` board runs on QEMU's GICv2 model without security
         * extensions. In that configuration the CPU interface still uses the
         * secure view internally, so Group 1 timer PPIs only become visible
         * through `GICC_IAR` after `GICC_CTLR.AckCtl` is enabled.
         *
         * @param interrupt_id Receives the acknowledged ID.
         * @return `true` when a real IRQ was acknowledged, or `false` for a
         * spurious CPU-interface read.
         */
        static bool begin_irq(U32* interrupt_id, U32* acknowledge_value) {
            U32 acknowledged_id;
            const U32 interrupt_number = GicIarInterruptIdMask;

            if ((interrupt_id == NULL) || (acknowledge_value == NULL)) {
                return false;
            }
            if (!InterruptControllerReady) {
                return false;
            }

            acknowledged_id = *gicc_iar();
            *acknowledge_value = acknowledged_id;
            *interrupt_id = acknowledged_id & interrupt_number;
            return *interrupt_id != GicSpuriousInterruptId;
        }

        /**
         * Signal end-of-interrupt to the GICv2 CPU interface.
         *
         * @param acknowledge_value Raw acknowledge token previously returned by `begin_irq`.
         * @return Nothing.
         */
        static void end_irq(U32 acknowledge_value) {
            if (!InterruptControllerReady || ((acknowledge_value & GicIarInterruptIdMask) == GicSpuriousInterruptId)) {
                return;
            }

            *gicc_eoir() = acknowledge_value;
        }

        /**
         * Report the architected timer interrupt ID used on `virt`.
         *
         * @return GIC interrupt ID for the EL1 physical timer PPI.
         */
        static bool is_timer_irq(U32 interrupt_id) {
            return (interrupt_id == GicTimerVirtualPpi) ||
                (interrupt_id == GicTimerPhysicalPpi);
        }

        [[noreturn]] static void restart(void) {
            for (;;) {
                __asm__ volatile("wfe");
            }
        }

        [[noreturn]] static void power_off(void) {
            for (;;) {
                __asm__ volatile("wfe");
            }
        }

    private:
        enum : U32 {
            TimerIoctlGetFrequency = 1U,
            GicTimerVirtualPpi = 27U,
            GicTimerPhysicalPpi = 30U,
            GicSpuriousInterruptId = 1023U,
            GicIarInterruptIdMask = 0x3FFU,
        };

        enum : U64 {
            VirtGicDistributorPhysBase = 0x08000000ULL,
            VirtGicCpuInterfacePhysBase = 0x08010000ULL,
        };

        enum : U32 {
            GicdCtlr = 0x000U,
            GicdIgroupr0 = 0x080U,
            GicdIsenabler0 = 0x100U,
            GicdIpriorityrBase = 0x400U,
            GiccCtlr = 0x0000U,
            GiccPmr = 0x0004U,
            GiccIar = 0x000CU,
            GiccEoir = 0x0010U,
        };

        inline static bool InterruptControllerReady = false;

        inline static constexpr U64 TimerIntervalMicroseconds = KernelTime::TickMicroseconds;

        /**
         * Convert one board physical MMIO register into the direct-map kernel alias.
         *
         * @param physical_address Physical MMIO address.
         * @return Volatile 32-bit register pointer.
         */
        static volatile U32* mmio32(PhysAddr physical_address) {
            return reinterpret_cast<volatile U32*>(mm::MemoryManager::physical_to_kernel(physical_address));
        }

        /**
         * Return one distributor register pointer.
         *
         * @param offset Byte offset inside the GIC distributor page.
         * @return Volatile 32-bit register pointer.
         */
        static volatile U32* gicd(U32 offset) {
            return mmio32(VirtGicDistributorPhysBase + offset);
        }

        /**
         * Return one CPU-interface register pointer.
         *
         * @param offset Byte offset inside the GIC CPU interface page.
         * @return Volatile 32-bit register pointer.
         */
        static volatile U32* gicc(U32 offset) {
            return mmio32(VirtGicCpuInterfacePhysBase + offset);
        }

        static volatile U32* gicc_iar(void) {
            return gicc(GiccIar);
        }

        static volatile U32* gicc_eoir(void) {
            return gicc(GiccEoir);
        }

        /**
         * Initialize the `virt` GICv2 distributor and CPU interface for the
         * architected timer PPIs used by QEMU `virt`.
         *
         * QEMU's `virt,gic-version=2` machine does not deliver the architected
         * timer IRQ just because `cntp_ctl_el0` is armed. The timer PPIs must be
         * placed in Group 1, assigned priorities, enabled in the distributor, and
         * unmasked in the CPU interface before EL1 will see the IRQ vector fire.
         *
         * The generic timer PPIs are level-sensitive, so this setup intentionally
         * leaves the distributor trigger configuration at its reset value instead
         * of forcing the PPIs to edge-triggered mode.
         *
         * @return `StatusOK` when the controller is ready, or
         * `StatusAlreadyExists` when already initialized.
         */
        static Status init_interrupt_controller(void) {
            U32 register_value;
            const U32 timer_ppi_mask = (1U << GicTimerVirtualPpi) | (1U << GicTimerPhysicalPpi);

            if (InterruptControllerReady) {
                return StatusAlreadyExists;
            }

            *gicd(GicdCtlr) = 0U;

            register_value = *gicd(GicdIgroupr0);
            register_value |= timer_ppi_mask;
            *gicd(GicdIgroupr0) = register_value;

            set_interrupt_priority(GicTimerVirtualPpi, 0x80U);
            set_interrupt_priority(GicTimerPhysicalPpi, 0x80U);

            *gicd(GicdIsenabler0) = timer_ppi_mask;
            // QEMU's GICv2 model returns special ID 1022 when a Group 1 IRQ is
            // pending but the CPU interface is still acknowledging in the
            // Group 0-only mode. Enable AckCtl together with both group-enable
            // bits so the architected timer PPI shows up as its real ID.
            *gicd(GicdCtlr) = 0x3U;

            *gicc(GiccPmr) = 0xFFU;
            *gicc(GiccCtlr) = 0x7U;
            __asm__ volatile("dsb sy\n\tisb" ::: "memory");

            InterruptControllerReady = true;
            return StatusOK;
        }

        /**
         * Program one GICv2 interrupt priority byte in-place.
         *
         * @param interrupt_id Interrupt number whose priority byte will change.
         * @param priority 8-bit GIC priority value.
         * @return Nothing.
         */
        static void set_interrupt_priority(U32 interrupt_id, U32 priority) {
            U32 register_value;
            volatile U32* priority_register;
            const U32 priority_shift = (interrupt_id % 4U) * 8U;

            priority_register = gicd(GicdIpriorityrBase + ((interrupt_id / 4U) * sizeof(U32)));
            register_value = *priority_register;
            register_value &= ~(0xFFU << priority_shift);
            register_value |= ((priority & 0xFFU) << priority_shift);
            *priority_register = register_value;
        }

        class SerialDeviceOps final {
        public:
            static Status init(Device* device) {
                (void)device;
                return Serial::init();
            }

            static Status deinit(Device* device) {
                (void)device;
                return StatusOK;
            }

            static SSize read(Device* device, U64 offset, void* buffer, Size length) {
                U8* bytes = static_cast<U8*>(buffer);

                (void)device;
                (void)offset;
                if ((buffer == NULL) && (length != 0U)) {
                    return StatusInvalidArgument;
                }

                for (Size index = 0; index < length; ++index) {
                    bytes[index] = static_cast<U8>(Serial::getc());
                }

                return static_cast<SSize>(length);
            }

            static SSize write(Device* device, U64 offset, const void* buffer, Size length) {
                const U8* bytes = static_cast<const U8*>(buffer);

                (void)device;
                (void)offset;
                if ((buffer == NULL) && (length != 0U)) {
                    return StatusInvalidArgument;
                }

                for (Size index = 0; index < length; ++index) {
                    Serial::putc(static_cast<char>(bytes[index]));
                }

                return static_cast<SSize>(length);
            }

            static Status ioctl(Device* device, U32 request, void* argument) {
                (void)device;
                (void)request;
                (void)argument;
                return StatusNotSupported;
            }
        };

        class TimerDeviceOps final {
        public:
            static Status init(Device* device) {
                (void)device;

                Status status = arch::Arch::init_periodic_timer(TimerIntervalMicroseconds);
                if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                    return status;
                }

                return status;
            }

            static Status deinit(Device* device) {
                (void)device;

                arch::Arch::disable_periodic_timer();
                return StatusOK;
            }

            static SSize read(Device* device, U64 offset, void* buffer, Size length) {
                U64 counter;

                (void)device;
                (void)offset;
                if ((buffer == NULL) || (length < sizeof(counter))) {
                    return StatusInvalidArgument;
                }

                counter = arch::Arch::counter_value();
                memcopy(buffer, &counter, sizeof(counter));
                return sizeof(counter);
            }

            static SSize write(Device* device, U64 offset, const void* buffer, Size length) {
                (void)device;
                (void)offset;
                (void)buffer;
                (void)length;
                return StatusNotSupported;
            }

            static Status ioctl(Device* device, U32 request, void* argument) {
                U64 frequency;

                (void)device;
                if (request != TimerIoctlGetFrequency) {
                    return StatusNotSupported;
                }
                if (argument == NULL) {
                    return StatusInvalidArgument;
                }

                frequency = arch::Arch::counter_frequency();
                *static_cast<U64*>(argument) = frequency;
                return StatusOK;
            }
        };

        inline static constexpr PlatformDescriptor Descriptor = {
            "virt",
            "aarch64",
            "generic",
            0,
            0,
        };

        inline static constexpr DeviceInterface SerialInterface = {
            &SerialDeviceOps::init,
            &SerialDeviceOps::deinit,
            &SerialDeviceOps::read,
            &SerialDeviceOps::write,
            &SerialDeviceOps::ioctl,
        };

        inline static DeviceDriver SerialDriver = {
            "pl011",
            &SerialInterface,
            NULL,
        };

        inline static Device SerialDevice = {
            "serial0",
            NULL,
            NULL,
            0,
            &SerialDriver,
        };

        inline static constexpr DeviceInterface TimerInterface = {
            &TimerDeviceOps::init,
            &TimerDeviceOps::deinit,
            &TimerDeviceOps::read,
            &TimerDeviceOps::write,
            &TimerDeviceOps::ioctl,
        };

        inline static DeviceDriver TimerDriver = {
            "arm-generic-timer",
            &TimerInterface,
            NULL,
        };

        inline static Device TimerDevice = {
            "timer0",
            NULL,
            NULL,
            0,
            &TimerDriver,
        };
    };

} // namespace board

#endif // KERNEL_INCLUDE_PLATFORM_BOARD_VIRT_PLATFORM_BACKEND_H