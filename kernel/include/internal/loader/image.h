#ifndef KERNEL_INTERNAL_LOADER_IMAGE_H
#define KERNEL_INTERNAL_LOADER_IMAGE_H

#include "address-space.h"
#include "loader.h"

typedef struct LoadedImage {
    Image descriptor;
    AddressSpace* process_address_space;
} LoadedImage;

#endif // KERNEL_INTERNAL_LOADER_IMAGE_H
