#ifndef KERNEL_INTERNAL_ARCH_CPU_STATE_H
#define KERNEL_INTERNAL_ARCH_CPU_STATE_H

#include "types.h"

typedef struct CpuState {
    U64 x[31];
    U64 sp_el0;
    U64 sp_el1;
    U64 elr_el1;
    U64 spsr_el1;
} CpuState;

#endif // KERNEL_INTERNAL_ARCH_CPU_STATE_H
