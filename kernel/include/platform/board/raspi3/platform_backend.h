#ifndef KERNEL_INCLUDE_PLATFORM_BOARD_RASPI3_PLATFORM_BACKEND_H
#define KERNEL_INCLUDE_PLATFORM_BOARD_RASPI3_PLATFORM_BACKEND_H

#include "arch.h"
#include "device.h"
#include "kernel_time.h"
#include "platform_descriptor.h"
#include "platform/board/raspi3/serial_backend.h"

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

            return DeviceManager::bind_driver(&TimerDevice, &TimerDriver);
        }

        static Status init_timer(void) {
            return TimerDevice.init();
        }

        /**
         * Keep the platform interface symmetric with the virt backend even
         * though raspi3 currently has no timer-polled devices.
         *
         * @return Nothing.
         */
        static void poll_devices(void) {
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
        };

        inline static constexpr U64 TimerIntervalMicroseconds = KernelTime::TickMicroseconds;

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
            "raspi3",
            "aarch64",
            "cortex-a53",
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

#endif // KERNEL_INCLUDE_PLATFORM_BOARD_RASPI3_PLATFORM_BACKEND_H