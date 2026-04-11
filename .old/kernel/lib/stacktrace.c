#include "memory.h"
#include "printf.h"
#include "stacktrace.h"
#include "task.h"

#define STACKTRACE_MAX_FRAMES 16

static inline unsigned long stacktrace_read_fp(void) {
    unsigned long frame_pointer;

    __asm__ volatile("mov %0, x29" : "=r"(frame_pointer));
    return frame_pointer;
}

static inline unsigned long stacktrace_read_sp(void) {
    unsigned long stack_pointer;

    __asm__ volatile("mov %0, sp" : "=r"(stack_pointer));
    return stack_pointer;
}

static Bool stacktrace_is_aligned(unsigned long value) {
    return (value & (sizeof(unsigned long) - 1UL)) == 0;
}

static Bool stacktrace_is_valid_kernel_frame(unsigned long frame_pointer, unsigned long stack_low, unsigned long stack_high) {
    unsigned long frame_end = frame_pointer + (2UL * sizeof(unsigned long));

    if (frame_pointer == 0 || !stacktrace_is_aligned(frame_pointer)) {
        return false;
    }
    if (!mem_is_kernel_virt_addr((VirtAddr)frame_pointer)) {
        return false;
    }
    if (stack_low != 0 && frame_pointer < stack_low) {
        return false;
    }
    if (stack_high != 0 && frame_end > stack_high) {
        return false;
    }

    return true;
}

void dump_stack_from_frame(unsigned long frame_pointer, unsigned long stack_pointer) {
    unsigned long stack_low = 0;
    unsigned long stack_high = 0;

    if (current_task && current_task->kernel_stack_page != 0) {
        stack_low = current_task->kernel_stack_page + VA_START;
        stack_high = stack_low + THREAD_SIZE;
    }
    else if (stack_pointer != 0 && mem_is_kernel_virt_addr((VirtAddr)stack_pointer)) {
        stack_low = stack_pointer & ~(THREAD_SIZE - 1UL);
        stack_high = stack_low + THREAD_SIZE;
    }

    kprint("\n\x1b[2;37m-------------------------------------------------------------------------------\x1b[0m\n");
    kprint("\x1b[1;36mKernel Stack Trace\x1b[0m\n");
    kprint("\x1b[2;37m-------------------------------------------------------------------------------\x1b[0m\n");

    if (!stacktrace_is_valid_kernel_frame(frame_pointer, stack_low, stack_high)) {
        kprint("  <no valid frame chain> fp=\x1b[36m0x%016lX\x1b[0m sp=\x1b[36m0x%016lX\x1b[0m\n", frame_pointer, stack_pointer);
        return;
    }

    for (int depth = 0; depth < STACKTRACE_MAX_FRAMES; depth++) {
        unsigned long* frame = (unsigned long*)frame_pointer;
        unsigned long next_frame = frame[0];
        unsigned long return_address = frame[1];

        kprint("  #%-2d fp=\x1b[36m0x%016lX\x1b[0m  lr=\x1b[36m0x%016lX\x1b[0m\n", depth, frame_pointer, return_address);

        if (return_address == 0 || next_frame <= frame_pointer) {
            break;
        }
        if (!stacktrace_is_valid_kernel_frame(next_frame, stack_low, stack_high)) {
            kprint("  <end of stack> next_fp=\x1b[36m0x%016lX\x1b[0m\n", next_frame);
            break;
        }

        frame_pointer = next_frame;
    }
}

__attribute__((noinline)) void dump_stack(void) {
    unsigned long frame_pointer = stacktrace_read_fp();
    unsigned long stack_pointer = stacktrace_read_sp();

    if (stacktrace_is_valid_kernel_frame(frame_pointer, 0, 0)) {
        unsigned long caller_frame = *((unsigned long*)frame_pointer);

        if (caller_frame != 0) {
            frame_pointer = caller_frame;
        }
    }

    dump_stack_from_frame(frame_pointer, stack_pointer);
}