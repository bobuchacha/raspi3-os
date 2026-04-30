#include "kernel_event_broker.h"

#include "arch.h"
#include "heap.h"
#include "kernel_time.h"
#include "process.h"
#include "scheduler.h"
#include "thread.h"

namespace {

    typedef struct EventSubscriber {
        KernelEventSubscriptionId subscription_id;
        U64 owner_process_id;
        KernelEventSubscriptionRequest request;
        KernelEventRecord* queue;
        U32 queue_capacity;
        U32 read_index;
        U32 write_index;
        U32 queued_count;
        U32 reserved0;
        U64 dropped_count;
        U64 newest_sequence;
        Thread* waiting_thread;
        EventSubscriber* next;
        EventSubscriber* prev;
    } EventSubscriber;

    EventSubscriber* g_subscriber_head;
    EventSubscriber* g_subscriber_tail;
    KernelEventSubscriptionId g_next_subscription_id;
    U64 g_next_sequence;

    void wake_waiter(EventSubscriber* subscriber);

    void copy_text(char* destination, Size capacity, const char* source) {
        Size index = 0U;

        if ((destination == NULL) || (capacity == 0U)) {
            return;
        }

        while ((source != NULL) && (source[index] != '\0') && ((index + 1U) < capacity)) {
            destination[index] = source[index];
            ++index;
        }

        destination[index] = '\0';
    }

    bool text_has_prefix(const char* text, const char* prefix) {
        if ((prefix == NULL) || (prefix[0] == '\0')) {
            return true;
        }
        if (text == NULL) {
            return false;
        }

        while (*prefix != '\0') {
            if (*text++ != *prefix++) {
                return false;
            }
        }

        return true;
    }

    void reset_subscriber(EventSubscriber* subscriber) {
        if (subscriber == NULL) {
            return;
        }

        if (subscriber->queue != NULL) {
            Heap::free(subscriber->queue);
        }

        memzero(subscriber, sizeof(*subscriber));
    }

    void link_subscriber(EventSubscriber* subscriber) {
        if (subscriber == NULL) {
            return;
        }

        subscriber->prev = g_subscriber_tail;
        subscriber->next = NULL;
        if (g_subscriber_tail != NULL) {
            g_subscriber_tail->next = subscriber;
        }
        else {
            g_subscriber_head = subscriber;
        }

        g_subscriber_tail = subscriber;
    }

    void unlink_subscriber(EventSubscriber* subscriber) {
        if (subscriber == NULL) {
            return;
        }

        if (subscriber->prev != NULL) {
            subscriber->prev->next = subscriber->next;
        }
        else {
            g_subscriber_head = subscriber->next;
        }

        if (subscriber->next != NULL) {
            subscriber->next->prev = subscriber->prev;
        }
        else {
            g_subscriber_tail = subscriber->prev;
        }

        subscriber->next = NULL;
        subscriber->prev = NULL;
    }

    Status reserve_queue_capacity(EventSubscriber* subscriber, U32 required_capacity) {
        KernelEventRecord* queue;
        U32 grown_capacity;
        U32 target_capacity;

        if (subscriber == NULL) {
            return StatusInvalidArgument;
        }
        if (required_capacity <= subscriber->queue_capacity) {
            return StatusOK;
        }

        target_capacity = required_capacity;
        if (target_capacity > static_cast<U32>(KERNEL_EVENT_QUEUE_MAX_CAPACITY)) {
            target_capacity = static_cast<U32>(KERNEL_EVENT_QUEUE_MAX_CAPACITY);
        }
        if (target_capacity <= subscriber->queue_capacity) {
            return StatusNoSpace;
        }

        grown_capacity = (subscriber->queue_capacity == 0U)
            ? static_cast<U32>(KERNEL_EVENT_QUEUE_INITIAL_CAPACITY)
            : subscriber->queue_capacity;
        while (grown_capacity < target_capacity) {
            if (grown_capacity > (0xFFFFFFFFU / 2U)) {
                grown_capacity = target_capacity;
                break;
            }

            grown_capacity *= 2U;
            if (grown_capacity > static_cast<U32>(KERNEL_EVENT_QUEUE_MAX_CAPACITY)) {
                grown_capacity = static_cast<U32>(KERNEL_EVENT_QUEUE_MAX_CAPACITY);
            }
        }

        queue = static_cast<KernelEventRecord*>(Heap::alloc(sizeof(KernelEventRecord) * grown_capacity, alignof(KernelEventRecord)));
        if (queue == NULL) {
            return StatusNoMemory;
        }

        for (U32 index = 0U; index < subscriber->queued_count; ++index) {
            queue[index] = subscriber->queue[(subscriber->read_index + index) % subscriber->queue_capacity];
        }

        if (subscriber->queue != NULL) {
            Heap::free(subscriber->queue);
        }

        subscriber->queue = queue;
        subscriber->queue_capacity = grown_capacity;
        subscriber->read_index = 0U;
        subscriber->write_index = subscriber->queued_count;
        return StatusOK;
    }

    void destroy_subscriber(EventSubscriber* subscriber) {
        if (subscriber == NULL) {
            return;
        }

        wake_waiter(subscriber);
        unlink_subscriber(subscriber);
        reset_subscriber(subscriber);
        Heap::free(subscriber);
    }

    void wake_waiter(EventSubscriber* subscriber) {
        Thread* waiter;

        if (subscriber == NULL) {
            return;
        }

        waiter = subscriber->waiting_thread;
        if (waiter == NULL) {
            return;
        }

        subscriber->waiting_thread = NULL;
        waiter->wait_object = NULL;
        waiter->wait_status = 0U;
        (void)Scheduler::enqueue(waiter);
    }

    U64 current_process_id(void) {
        Process* process = Scheduler::current_process();
        return (process != NULL) ? process->id : 0ULL;
    }

    U64 current_thread_id(void) {
        Thread* thread = Scheduler::current();
        return (thread != NULL) ? thread->id : 0ULL;
    }

    void initialize_record(KernelEventRecord* record) {
        if (record == NULL) {
            return;
        }

        memzero(record, sizeof(*record));
        record->version = KERNEL_EVENT_ABI_VERSION;
        record->size = sizeof(*record);
    }

    EventSubscriber* find_subscription(U64 owner_process_id, KernelEventSubscriptionId subscription_id) {
        for (EventSubscriber* subscriber = g_subscriber_head; subscriber != NULL; subscriber = subscriber->next) {
            if ((subscriber->subscription_id == subscription_id) && (subscriber->owner_process_id == owner_process_id)) {
                return subscriber;
            }
        }

        return NULL;
    }

    bool subscription_matches(const EventSubscriber* subscriber, const KernelEventRecord* record) {
        if ((subscriber == NULL) || (record == NULL)) {
            return false;
        }
        if ((record->family & subscriber->request.family_mask) == 0ULL) {
            return false;
        }
        if ((subscriber->request.process_id != 0ULL) && (record->source_process_id != subscriber->request.process_id)) {
            return false;
        }
        if ((subscriber->request.thread_id != 0ULL) && (record->source_thread_id != subscriber->request.thread_id)) {
            return false;
        }
        if (!text_has_prefix(record->name, subscriber->request.name_prefix)) {
            return false;
        }
        if (!text_has_prefix(record->path, subscriber->request.path_prefix)) {
            return false;
        }

        return true;
    }

    void enqueue_record(EventSubscriber* subscriber, const KernelEventRecord* record) {
        Status status;

        if ((subscriber == NULL) || (record == NULL)) {
            return;
        }

        status = reserve_queue_capacity(subscriber, subscriber->queued_count + 1U);
        if ((status != StatusOK) && (subscriber->queue_capacity == 0U)) {
            subscriber->dropped_count++;
            return;
        }
        if ((status == StatusNoSpace) && (subscriber->queue_capacity != 0U)) {
            subscriber->dropped_count++;
        }
        if (subscriber->queued_count == subscriber->queue_capacity) {
            subscriber->read_index = (subscriber->read_index + 1U) % subscriber->queue_capacity;
            subscriber->queued_count--;
            if (status != StatusNoSpace) {
                subscriber->dropped_count++;
            }
        }

        subscriber->queue[subscriber->write_index] = *record;
        subscriber->write_index = (subscriber->write_index + 1U) % subscriber->queue_capacity;
        subscriber->queued_count++;
        subscriber->newest_sequence = record->sequence;
        wake_waiter(subscriber);
    }

    void publish_record(KernelEventRecord* record) {
        if (record == NULL) {
            return;
        }

        record->version = KERNEL_EVENT_ABI_VERSION;
        record->size = sizeof(*record);
        record->sequence = ++g_next_sequence;
        record->uptime_msec = KernelTime::ticks_to_milliseconds(Scheduler::tick_count());

        for (EventSubscriber* subscriber = g_subscriber_head; subscriber != NULL; subscriber = subscriber->next) {
            if (subscription_matches(subscriber, record)) {
                enqueue_record(subscriber, record);
            }
        }
    }

    U32 registration_kind_for_type(KernelEventType type) {
        switch (type) {
        case KernelEventTypeDeviceRegistered:
            return KernelEventRegistrationKindDevice;
        case KernelEventTypeDriverRegistered:
            return KernelEventRegistrationKindDriver;
        case KernelEventTypeFilesystemDriverRegistered:
            return KernelEventRegistrationKindFilesystemDriver;
        default:
            return KernelEventRegistrationKindNone;
        }
    }

} // namespace

Status KernelEventBroker::init(void) {
    while (g_subscriber_head != NULL) {
        destroy_subscriber(g_subscriber_head);
    }

    g_next_subscription_id = 1ULL;
    g_next_sequence = 0ULL;
    return StatusOK;
}

Status KernelEventBroker::subscribe(
    U64 owner_process_id,
    const KernelEventSubscriptionRequest* request,
    KernelEventSubscriptionId* out_subscription_id) {
    EventSubscriber* subscriber;

    if ((request == NULL) || (out_subscription_id == NULL)) {
        return StatusInvalidArgument;
    }
    if (request->version != KERNEL_EVENT_ABI_VERSION) {
        return StatusInvalidArgument;
    }
    if (request->flags != 0U) {
        return StatusNotSupported;
    }
    if ((request->family_mask == KERNEL_EVENT_FAMILY_NONE) || ((request->family_mask & ~KERNEL_EVENT_FAMILY_ALL) != 0ULL)) {
        return StatusInvalidArgument;
    }

    subscriber = static_cast<EventSubscriber*>(Heap::alloc(sizeof(EventSubscriber), alignof(EventSubscriber)));
    if (subscriber == NULL) {
        return StatusNoMemory;
    }

    memzero(subscriber, sizeof(*subscriber));
    subscriber->subscription_id = g_next_subscription_id++;
    subscriber->owner_process_id = owner_process_id;
    subscriber->request = *request;
    subscriber->request.name_prefix[KERNEL_EVENT_NAME_CAPACITY - 1U] = '\0';
    subscriber->request.path_prefix[KERNEL_EVENT_PATH_CAPACITY - 1U] = '\0';
    link_subscriber(subscriber);
    *out_subscription_id = subscriber->subscription_id;
    return StatusOK;
}

Status KernelEventBroker::unsubscribe(U64 owner_process_id, KernelEventSubscriptionId subscription_id) {
    EventSubscriber* subscriber;

    if (subscription_id == 0ULL) {
        return StatusInvalidArgument;
    }

    subscriber = find_subscription(owner_process_id, subscription_id);
    if (subscriber == NULL) {
        return StatusNotFound;
    }

    destroy_subscriber(subscriber);
    return StatusOK;
}

Status KernelEventBroker::query(
    U64 owner_process_id,
    KernelEventSubscriptionId subscription_id,
    KernelEventSubscriptionInfo* out_info) {
    EventSubscriber* subscriber;

    if (out_info == NULL) {
        return StatusInvalidArgument;
    }

    subscriber = find_subscription(owner_process_id, subscription_id);
    if (subscriber == NULL) {
        return StatusNotFound;
    }

    memzero(out_info, sizeof(*out_info));
    out_info->version = KERNEL_EVENT_ABI_VERSION;
    out_info->subscription_id = subscriber->subscription_id;
    out_info->owner_process_id = subscriber->owner_process_id;
    out_info->family_mask = subscriber->request.family_mask;
    out_info->process_id = subscriber->request.process_id;
    out_info->thread_id = subscriber->request.thread_id;
    out_info->queued_count = subscriber->queued_count;
    out_info->capacity = subscriber->queue_capacity;
    out_info->next_sequence = (subscriber->queued_count != 0U)
        ? subscriber->queue[subscriber->read_index].sequence
        : (subscriber->newest_sequence + 1ULL);
    out_info->newest_sequence = subscriber->newest_sequence;
    out_info->dropped_count = subscriber->dropped_count;
    copy_text(out_info->name_prefix, sizeof(out_info->name_prefix), subscriber->request.name_prefix);
    copy_text(out_info->path_prefix, sizeof(out_info->path_prefix), subscriber->request.path_prefix);
    return StatusOK;
}

Status KernelEventBroker::wait(U64 owner_process_id, KernelEventSubscriptionId subscription_id) {
    bool interrupts_enabled;
    EventSubscriber* subscriber;
    Thread* current_thread;
    Status status;

    interrupts_enabled = arch::Arch::save_and_disable_interrupts();

    subscriber = find_subscription(owner_process_id, subscription_id);
    if (subscriber == NULL) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusNotFound;
    }
    if (subscriber->queued_count != 0U) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusOK;
    }

    current_thread = Scheduler::current();
    if ((current_thread == NULL) || (current_thread->current_state != ThreadState::Running)) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusBusy;
    }
    if ((subscriber->waiting_thread != NULL) && (subscriber->waiting_thread != current_thread)) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusBusy;
    }

    subscriber->waiting_thread = current_thread;
    status = Scheduler::block_current(WaitReason::Event);
    if (subscriber->waiting_thread == current_thread) {
        subscriber->waiting_thread = NULL;
    }

    arch::Arch::restore_interrupts(interrupts_enabled);
    return status;
}

Status KernelEventBroker::read(
    U64 owner_process_id,
    KernelEventSubscriptionId subscription_id,
    KernelEventRecord* out_records,
    U32 capacity,
    U32* out_count) {
    EventSubscriber* subscriber;
    U32 count;

    if (out_count == NULL) {
        return StatusInvalidArgument;
    }
    if ((capacity != 0U) && (out_records == NULL)) {
        return StatusInvalidArgument;
    }

    subscriber = find_subscription(owner_process_id, subscription_id);
    if (subscriber == NULL) {
        return StatusNotFound;
    }

    count = (capacity < subscriber->queued_count) ? capacity : subscriber->queued_count;
    for (U32 index = 0U; index < count; ++index) {
        out_records[index] = subscriber->queue[subscriber->read_index];
        subscriber->read_index = (subscriber->read_index + 1U) % subscriber->queue_capacity;
        subscriber->queued_count--;
    }

    *out_count = count;
    return StatusOK;
}

void KernelEventBroker::unsubscribe_process(U64 owner_process_id) {
    for (EventSubscriber* subscriber = g_subscriber_head, *next_subscriber = NULL;
        subscriber != NULL;
        subscriber = next_subscriber) {
        next_subscriber = subscriber->next;
        if (subscriber->owner_process_id == owner_process_id) {
            destroy_subscriber(subscriber);
        }
    }
}

void KernelEventBroker::publish_process_event(
    KernelEventType type,
    U64 process_id,
    const char* process_name,
    const char* image_path,
    U64 parent_process_id,
    I64 exit_code,
    U32 process_state,
    U32 process_flags,
    Status status) {
    KernelEventRecord record;

    initialize_record(&record);
    record.family = KERNEL_EVENT_FAMILY_PROCESS;
    record.type = static_cast<U32>(type);
    record.status = static_cast<I32>(status);
    record.source_process_id = process_id;
    record.source_thread_id = 0ULL;
    record.source_object_id = process_id;
    copy_text(record.name, sizeof(record.name), process_name);
    copy_text(record.path, sizeof(record.path), image_path);
    record.payload.process.parent_process_id = parent_process_id;
    record.payload.process.exit_code = exit_code;
    record.payload.process.process_state = process_state;
    record.payload.process.process_flags = process_flags;
    publish_record(&record);
}

void KernelEventBroker::publish_thread_event(KernelEventType type, const Thread* thread, U32 previous_state, U32 current_state, Status status) {
    KernelEventRecord record;

    if (thread == NULL) {
        return;
    }

    initialize_record(&record);
    record.family = KERNEL_EVENT_FAMILY_THREAD;
    record.type = static_cast<U32>(type);
    record.status = static_cast<I32>(status);
    record.source_process_id = (thread->parent != NULL) ? thread->parent->id : 0ULL;
    record.source_thread_id = thread->id;
    record.source_object_id = thread->id;
    copy_text(record.name, sizeof(record.name), thread->name);
    record.payload.thread.parent_process_id = (thread->parent != NULL) ? thread->parent->id : 0ULL;
    record.payload.thread.previous_state = previous_state;
    record.payload.thread.current_state = current_state;
    record.payload.thread.priority = thread->current_priority;
    publish_record(&record);
}

void KernelEventBroker::publish_vfs_event(KernelEventType type, const char* path, U32 node_type, Status status) {
    KernelEventRecord record;

    initialize_record(&record);
    record.family = KERNEL_EVENT_FAMILY_FILESYSTEM;
    record.type = static_cast<U32>(type);
    record.status = static_cast<I32>(status);
    record.source_process_id = current_process_id();
    record.source_thread_id = current_thread_id();
    record.source_object_id = 0ULL;
    copy_text(record.path, sizeof(record.path), path);
    record.payload.filesystem.node_type = node_type;
    record.payload.filesystem.size_bytes = 0ULL;
    publish_record(&record);
}

void KernelEventBroker::publish_registration_event(KernelEventType type, const char* name, Status status) {
    KernelEventRecord record;

    initialize_record(&record);
    record.family = KERNEL_EVENT_FAMILY_REGISTRATION;
    record.type = static_cast<U32>(type);
    record.status = static_cast<I32>(status);
    record.source_process_id = current_process_id();
    record.source_thread_id = current_thread_id();
    record.source_object_id = 0ULL;
    copy_text(record.name, sizeof(record.name), name);
    record.payload.registration.registration_kind = registration_kind_for_type(type);
    publish_record(&record);
}