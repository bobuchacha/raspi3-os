#pragma once

#include "kernel_event.h"

#if !defined(__cplusplus)
#error "kernel_event_broker.h requires C++"
#endif

struct Process;
struct Thread;

class KernelEventBroker final {
public:
    static Status init(void);
    static Status subscribe(
        U64 owner_process_id,
        const KernelEventSubscriptionRequest* request,
        KernelEventSubscriptionId* out_subscription_id);
    static Status unsubscribe(U64 owner_process_id, KernelEventSubscriptionId subscription_id);
    static Status query(
        U64 owner_process_id,
        KernelEventSubscriptionId subscription_id,
        KernelEventSubscriptionInfo* out_info);
    static Status wait(U64 owner_process_id, KernelEventSubscriptionId subscription_id);
    static Status read(
        U64 owner_process_id,
        KernelEventSubscriptionId subscription_id,
        KernelEventRecord* out_records,
        U32 capacity,
        U32* out_count);
    static void unsubscribe_process(U64 owner_process_id);
    /**
     * Publish one process lifecycle snapshot using scalar fields rather than a
     * live `Process*`.
     *
     * The broker only needs stable event payload values. Passing a snapshot
     * keeps this interface insulated from unrelated `Process` layout churn,
     * which avoids stale-offset corruption in event publishers when process
     * internals grow new fields.
     *
     * @param type Process event type being emitted.
     * @param process_id Target process identifier.
     * @param process_name Stable display name for the process.
     * @param image_path Optional executable path associated with the event.
     * @param parent_process_id Launcher PID recorded for reference.
     * @param exit_code Stored exit status snapshot.
     * @param process_state Process lifecycle state snapshot.
     * @param process_flags Process object flags snapshot.
     * @param status Result code associated with the event emission.
     */
    static void publish_process_event(
        KernelEventType type,
        U64 process_id,
        const char* process_name,
        const char* image_path,
        U64 parent_process_id,
        I64 exit_code,
        U32 process_state,
        U32 process_flags,
        Status status);
    static void publish_thread_event(KernelEventType type, const Thread* thread, U32 previous_state, U32 current_state, Status status);
    static void publish_vfs_event(KernelEventType type, const char* path, U32 node_type, Status status);
    static void publish_registration_event(KernelEventType type, const char* name, Status status);
};