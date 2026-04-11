#ifndef KERNEL_INTERNAL_SCHEDULER_THREAD_H
#define KERNEL_INTERNAL_SCHEDULER_THREAD_H

#include "internal/arch/cpu_state.h"
#include "scheduler.h"

typedef struct ThreadControlBlock {
    Thread public_thread;
    CpuState cpu_state;
    struct ThreadControlBlock* next;
    struct ThreadControlBlock* prev;
} ThreadControlBlock;

#endif // KERNEL_INTERNAL_SCHEDULER_THREAD_H
