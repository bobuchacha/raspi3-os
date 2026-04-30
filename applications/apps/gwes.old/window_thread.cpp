#include "gwes_workers.h"
#include "render.h"

#include "app/app.h"

#include <stdlib.h>
#include <string.h>

namespace {

    typedef enum GwesWindowQueueKind {
        GWES_WINDOW_QUEUE_KIND_PROTOCOL_PACKET = 1UL,
        GWES_WINDOW_QUEUE_KIND_INPUT_EVENT = 2UL
    } GwesWindowQueueKind;

    typedef struct GwesWindowQueueEntry {
        unsigned long kind;
        UserIpcMessage packet;
        RosKernelGuiInputEvent input_event;
        struct GwesWindowQueueEntry* next;
    } GwesWindowQueueEntry;

    static GwesWindowQueueEntry* g_gwes_window_queue_head = NULL;
    static GwesWindowQueueEntry* g_gwes_window_queue_tail = NULL;
    static volatile unsigned long g_gwes_window_queue_lock_word = 0UL;
    static volatile unsigned long g_gwes_window_queue_wake_sequence = 0UL;

    /*
     * Attempt one acquire of the shared window-event queue lock.
     *
     * @return Non-zero when the lock was acquired.
     */
    int gwes_window_queue_try_lock_once(void) {
        unsigned long observed = 0UL;
        unsigned int store_failed = 0U;
        const unsigned long locked = 1UL;

        asm volatile(
            "ldaxr %0, [%2]\n"
            "cbnz %0, 1f\n"
            "stxr %w1, %3, [%2]\n"
            "b 2f\n"
            "1:\n"
            "mov %w1, #1\n"
            "2:\n"
            : "=&r"(observed), "=&r"(store_failed)
            : "r"(&g_gwes_window_queue_lock_word), "r"(locked)
            : "memory");

        return (observed == 0UL) && (store_failed == 0U);
    }

    /*
     * Acquire the shared window-event queue lock.
     *
     * @return Nothing.
     */
    void gwes_window_queue_lock(void) {
        while (!gwes_window_queue_try_lock_once()) {
            asm volatile("yield\n" ::: "memory");
        }
    }

    /*
     * Release the shared window-event queue lock.
     *
     * @return Nothing.
     */
    void gwes_window_queue_unlock(void) {
        const unsigned long unlocked = 0UL;

        asm volatile("stlr %1, [%0]" : : "r"(&g_gwes_window_queue_lock_word), "r"(unlocked) : "memory");
    }

    /*
     * Append one fully initialized entry to the shared window-event queue.
     *
     * @param entry Heap-backed queue node to append.
     * @return Zero on success, or a negative status code on failure.
     */
    long gwes_window_queue_push_entry(GwesWindowQueueEntry* entry) {
        if (entry == NULL) {
            return ROS_USER_IPC_STATUS_NOT_FOUND;
        }

        gwes_window_queue_lock();
        entry->next = NULL;
        if (g_gwes_window_queue_tail != NULL) {
            g_gwes_window_queue_tail->next = entry;
        }
        else {
            g_gwes_window_queue_head = entry;
        }
        g_gwes_window_queue_tail = entry;
        ++g_gwes_window_queue_wake_sequence;
        gwes_window_queue_unlock();
        return ROS_USER_IPC_STATUS_OK;
    }

    /*
     * Remove the oldest pending window-event queue node.
     *
     * @param entry Receives one popped queue payload.
     * @return Non-zero when an entry was removed.
     */
    int gwes_window_queue_pop_entry(GwesWindowQueueEntry* entry) {
        GwesWindowQueueEntry* queued;

        if (entry == NULL) {
            return 0;
        }

        gwes_window_queue_lock();
        queued = g_gwes_window_queue_head;
        if (queued == NULL) {
            gwes_window_queue_unlock();
            return 0;
        }

        *entry = *queued;
        g_gwes_window_queue_head = queued->next;
        if (g_gwes_window_queue_head == NULL) {
            g_gwes_window_queue_tail = NULL;
        }
        gwes_window_queue_unlock();
        free(queued);
        return 1;
    }

} // namespace

/*
 * Queue one client protocol packet for the window/render owner thread.
 *
 * @param packet Caller-owned packet copied into the queue.
 * @return Zero on success, or a negative status code on allocation failure.
 */
long gwes_window_queue_push_protocol_packet(const UserIpcMessage* packet) {
    GwesWindowQueueEntry* entry;

    if (packet == NULL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    entry = (GwesWindowQueueEntry*)malloc(sizeof(*entry));
    if (entry == NULL) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    memset(entry, 0, sizeof(*entry));
    entry->kind = GWES_WINDOW_QUEUE_KIND_PROTOCOL_PACKET;
    entry->packet = *packet;
    return gwes_window_queue_push_entry(entry);
}

/*
 * Queue one shared-input event for the window/render owner thread.
 *
 * @param event Caller-owned input event copied into the queue.
 * @return Zero on success, or a negative status code on allocation failure.
 */
long gwes_window_queue_push_input_event(const RosKernelGuiInputEvent* event) {
    GwesWindowQueueEntry* entry;

    if (event == NULL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    entry = (GwesWindowQueueEntry*)malloc(sizeof(*entry));
    if (entry == NULL) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    memset(entry, 0, sizeof(*entry));
    entry->kind = GWES_WINDOW_QUEUE_KIND_INPUT_EVENT;
    entry->input_event = *event;
    return gwes_window_queue_push_entry(entry);
}

/*
 * Submit any pending compositor damage through the dedicated window file.
 *
 * @return Nothing.
 */
void gwes_window_thread_present_if_needed(void) {
    gwes_render_present_if_needed();
}

/*
 * Run the dedicated window/render owner loop.
 *
 * All retained window state, client protocol handling, input dispatch, timer
 * delivery, and present submission are serialized here so GWES no longer mixes
 * those responsibilities into the broker receive loop.
 *
 * @param argument Unused worker-thread argument.
 * @return Nothing.
 */
extern "C" void gwes_window_thread_entry(unsigned long argument) {
    unsigned long last_reap_msec = getUptimeMs();
    unsigned long observed_wake_sequence = 0UL;

    (void)argument;

    for (;;) {
        GwesWindowQueueEntry entry;
        unsigned long now;
        int did_work = 0;

        while (gwes_window_queue_pop_entry(&entry)) {
            did_work = 1;
            if (entry.kind == GWES_WINDOW_QUEUE_KIND_PROTOCOL_PACKET) {
                gwes_window_thread_handle_packet(&entry.packet);
            }
            else if (entry.kind == GWES_WINDOW_QUEUE_KIND_INPUT_EVENT) {
                gwes_window_thread_handle_input_event(&entry.input_event);
            }
        }

        now = getUptimeMs();
        if (gwes_window_thread_fire_due_timers(now) != 0UL) {
            did_work = 1;
        }
        if ((now - last_reap_msec) >= 200UL) {
            gwes_window_thread_reap_dead_clients();
            last_reap_msec = now;
        }

        gwes_window_thread_present_if_needed();

        observed_wake_sequence = g_gwes_window_queue_wake_sequence;
        if (did_work) {
            continue;
        }

        now = getUptimeMs();
        if (gwes_window_thread_timer_wait_budget(now) == 0UL) {
            continue;
        }
        if (observed_wake_sequence == g_gwes_window_queue_wake_sequence) {
            (void)sleepMs(1UL);
        }
    }
}
