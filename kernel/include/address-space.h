#pragma once

#include "types.h"

struct VmRegion;

struct AddressSpace {
    PhysAddr page_table_root;
    VmRegion* region_list;
    U16 asid;
    VirtAddr user_base;
    VirtAddr user_limit;
    // User address spaces now own heap-backed translation tables so teardown
    // can return them without a fixed global slot table.
    VirtAddr translation_table_l0;
    VirtAddr translation_table_l1;
    VirtAddr translation_table_l2;
};

struct VmRegion {
    VirtAddr base;
    Size size;
    U32 flags;
    VmRegion* next;
};
