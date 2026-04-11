#ifndef ROS_APP_KERNEL_EVENT_H
#define ROS_APP_KERNEL_EVENT_H

#include <kernel_event.h>
#include "user_runtime.h"

/* Register one kernel-event subscription and receive a broker-owned identifier. */
static inline long subscribeKernelEvents(const KernelEventSubscriptionRequest* request, KernelEventSubscriptionId* subscriptionId) {
    return (long)invokeSyscall2(USER_SYS_EVENT_SUBSCRIBE, (unsigned long)request, (unsigned long)subscriptionId);
}

/* Drain one queued batch of records from the subscription into a caller-provided array. */
static inline long readKernelEvents(KernelEventSubscriptionId subscriptionId, KernelEventRecord* records, U32 capacity, U32* recordCount) {
    return (long)invokeSyscall4(USER_SYS_EVENT_READ, (unsigned long)subscriptionId, (unsigned long)records, (unsigned long)capacity, (unsigned long)recordCount);
}

/* Query one subscription state snapshot without consuming queued records. */
static inline long queryKernelEvents(KernelEventSubscriptionId subscriptionId, KernelEventSubscriptionInfo* info) {
    return (long)invokeSyscall2(USER_SYS_EVENT_QUERY, (unsigned long)subscriptionId, (unsigned long)info);
}

/* Block until the subscription has at least one queued event ready to drain. */
static inline long waitKernelEvents(KernelEventSubscriptionId subscriptionId) {
    return (long)invokeSyscall1(USER_SYS_EVENT_WAIT, (unsigned long)subscriptionId);
}

/* Release one subscription so the broker slot can be recycled immediately. */
static inline long unsubscribeKernelEvents(KernelEventSubscriptionId subscriptionId) {
    return (long)invokeSyscall1(USER_SYS_EVENT_UNSUBSCRIBE, (unsigned long)subscriptionId);
}

#endif