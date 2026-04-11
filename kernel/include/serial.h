#ifndef KERNEL_INCLUDE_SERIAL_H
#define KERNEL_INCLUDE_SERIAL_H

#include "types.h"

#if !defined(__cplusplus)
#error "serial.h requires C++"
#endif

#if defined(BOARD_RASPI3)
#include "platform/board/raspi3/serial_backend.h"
#elif defined(BOARD_VIRT)
#include "platform/board/virt/serial_backend.h"
#else
#include "platform/board/virt/serial_backend.h"
#endif

#endif