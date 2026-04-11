#ifndef KERNEL_INCLUDE_PLATFORM_H
#define KERNEL_INCLUDE_PLATFORM_H

#include "platform_descriptor.h"

#if !defined(__cplusplus)
#error "platform.h requires C++"
#endif

#if defined(BOARD_RASPI3)
#include "platform/board/raspi3/platform_backend.h"
#elif defined(BOARD_VIRT)
#include "platform/board/virt/platform_backend.h"
#else
#include "platform/board/virt/platform_backend.h"
#endif

#endif // KERNEL_INCLUDE_PLATFORM_H
