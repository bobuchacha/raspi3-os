#ifndef KERNEL_INCLUDE_SERVICE_CALL_H
#define KERNEL_INCLUDE_SERVICE_CALL_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct ServiceFrame {
        U64 service_id;
        U64 arguments[6];
        U64 result;
        Status status;
        void* trap_frame;
    } ServiceFrame;

    Status service_dispatch(ServiceFrame* frame);
    Status service_kernel_ipc_notify(
        U64 receiver_pid,
        unsigned long protocol,
        unsigned long kind,
        unsigned long arg0,
        unsigned long arg1,
        unsigned long arg2,
        unsigned long arg3);

#ifdef __cplusplus
}
#endif

#endif // KERNEL_INCLUDE_SERVICE_CALL_H
