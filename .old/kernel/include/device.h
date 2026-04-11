/**
 * device.h
 *
 * this file defines the interface for device initialization and management
 */
#ifndef _DEVICE_H
#define _DEVICE_H

#include "platform/board.h"
#include "platform/cpu/mmu.h"
#include "ros.h"

void device_init();
void device_init_fs();
void device_reboot();
void device_shutdown();
int device_init_graphics(void);
int device_init_touch(void);
void device_poll_touch(void);

// interface for device drivers
typedef struct device_driver {
    const char* name;
    int (*init)(void);
    void (*shutdown)(void);
} device_driver_t;

void device_register_driver(device_driver_t* driver);
void device_unregister_driver(device_driver_t* driver);

//
// All code above this line is deprecated
//
// This is the new approach to abstract platform-specific device operations behind a uniform interface
// the previous approach relied on registering individual device drivers with init/shutdown callbacks
// for each device, whereas this new interface exposes a uniform set of operations that the kernel
// can call for any platform device without needing to know the specifics of each driver
// this allows the kernel to call init/read/write/irq registration on any device
// in a platform-agnostic way, rather than having to know the details of each
// individual driver implementation
//

// Example: Initialize platform devices
typedef void (*device_init_fn)(void);
// Example: Read/write to a device
typedef void (*device_read_fn)(int device_id, void* buf, int len);
typedef void (*device_write_fn)(int device_id, const void* buf, int len);
// Example: Platform-specific interrupt handler registration
typedef void (*device_register_irq_fn)(int irq, void (*handler)(void));
// Example: Reboot and shutdown the platform
typedef void (*device_reboot_fn)(void);
typedef void (*device_shutdown_fn)(void);
// Example: Touch input initialization and polling
typedef void (*device_poll_touch_fn)(void);
typedef void (*device_init_touch_fn)(void);




// This struct defines the uniform interface that the kernel will use to interact with
// platform-specific devices in a platform-agnostic way
struct device_interface {
    device_init_fn        init;
    device_read_fn        read;
    device_write_fn       write;
    device_register_irq_fn register_irq;
    // Add more as needed
    device_reboot_fn       reboot;
    device_shutdown_fn     shutdown;

    // touch and inputs
    device_init_touch_fn   init_touch;
    device_poll_touch_fn  poll_touch;

    // keyboard input
    device_init_fn        init_keyboard;
    device_poll_touch_fn  poll_keyboard;

    // framebuffer
    device_init_fn        init_framebuffer;

    // uart
    device_init_fn        uart_init;
    device_read_fn  uart_read;
    device_write_fn uart_write;

    // timer
    device_init_fn        timer_init;
    device_register_irq_fn timer_register_irq;


};

// Kernel will call this to get the platform's device interface
const struct device_interface* get_platform_device_interface(void);


#endif
