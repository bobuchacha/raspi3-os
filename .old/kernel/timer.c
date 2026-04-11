/*
 * timer.c
 *
 * Generic timer hook that forwards architecture timer interrupts into the
 * scheduler time-accounting path.
 */
#include "printf.h"
#include "device.h"
#include "task.h"
extern void dump_registers();

/**
 * Top-level timer interrupt hook used by the generic kernel layer.
 * The timer IRQ handler is still architecture-owned, but the scheduler owns the
 * time-slice bookkeeping.
 */
void timer_tick() {
    // Drain virtio input queues from a periodic context so viewer events do not
    // pile up while userspace tasks dominate the CPU.
    device_poll_touch();

    // Keep the generic timer entry tiny and let the scheduler own time-slice bookkeeping.
    schedler_timer_tick();
}