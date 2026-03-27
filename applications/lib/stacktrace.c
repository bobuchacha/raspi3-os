#include "app/kernel.h"
#include "stacktrace.h"
#include "stdio.h"

#define APP_STACKTRACE_MAX_FRAMES 16
#define APP_STACKTRACE_PAGE_SIZE  0x1000UL

static inline unsigned long app_stacktrace_read_fp(void) {
    unsigned long frame_pointer;

    __asm__ volatile("mov %0, x29" : "=r"(frame_pointer));
    return frame_pointer;
}

static inline unsigned long app_stacktrace_read_sp(void) {
    unsigned long stack_pointer;

    __asm__ volatile("mov %0, sp" : "=r"(stack_pointer));
    return stack_pointer;
}

static int app_stacktrace_is_aligned(unsigned long value) {
    return (value & (sizeof(unsigned long) - 1UL)) == 0;
}

static int app_stacktrace_is_valid_frame(unsigned long frame_pointer, unsigned long stack_low, unsigned long stack_high) {
    unsigned long frame_end = frame_pointer + (2UL * sizeof(unsigned long));

    if (frame_pointer == 0 || !app_stacktrace_is_aligned(frame_pointer)) {
        return 0;
    }
    if (frame_pointer < stack_low || frame_end > stack_high) {
        return 0;
    }

    return 1;
}

__attribute__((noinline)) void app_dump_stack(void) {
    char line[128];
    unsigned long stack_pointer = app_stacktrace_read_sp();
    unsigned long stack_low = stack_pointer & ~(APP_STACKTRACE_PAGE_SIZE - 1UL);
    unsigned long stack_high = stack_low + APP_STACKTRACE_PAGE_SIZE;
    unsigned long frame_pointer = app_stacktrace_read_fp();

    if (app_stacktrace_is_valid_frame(frame_pointer, stack_low, stack_high)) {
        unsigned long caller_frame = *((unsigned long*)frame_pointer);

        if (caller_frame != 0) {
            frame_pointer = caller_frame;
        }
    }

    user_kernel_write("\n--- User Stack Trace ---\n");

    if (!app_stacktrace_is_valid_frame(frame_pointer, stack_low, stack_high)) {
        snprintf(line, sizeof(line), "  <no valid frame chain> fp=0x%016lx sp=0x%016lx\n", frame_pointer, stack_pointer);
        user_kernel_write(line);
        return;
    }

    for (int depth = 0; depth < APP_STACKTRACE_MAX_FRAMES; depth++) {
        unsigned long* frame = (unsigned long*)frame_pointer;
        unsigned long next_frame = frame[0];
        unsigned long return_address = frame[1];

        snprintf(line, sizeof(line), "  #%-2d fp=0x%016lx  lr=0x%016lx\n", depth, frame_pointer, return_address);
        user_kernel_write(line);

        if (return_address == 0 || next_frame <= frame_pointer) {
            break;
        }
        if (!app_stacktrace_is_valid_frame(next_frame, stack_low, stack_high)) {
            snprintf(line, sizeof(line), "  <end of stack> next_fp=0x%016lx\n", next_frame);
            user_kernel_write(line);
            break;
        }

        frame_pointer = next_frame;
    }
}