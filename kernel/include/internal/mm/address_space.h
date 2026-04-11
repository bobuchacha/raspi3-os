#ifndef KERNEL_INTERNAL_MM_ADDRESS_SPACE_H
#define KERNEL_INTERNAL_MM_ADDRESS_SPACE_H

#include "address-space.h"

typedef struct KernelAddressSpace {
    PhysAddr root_table;
    VirtAddr kernel_base;
} KernelAddressSpace;

typedef struct MmState {
    AddressSpace bootstrap_address_space;
    AddressSpace current_address_space;
    KernelAddressSpace kernel_address_space;
} MmState;

#endif // KERNEL_INTERNAL_MM_ADDRESS_SPACE_H
