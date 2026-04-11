#include "user_runtime.h"
#include "app/core_ipc.h"


/*
 * core/ipc.c
 *
 * This file implements the user-mode IPC broker exported by core.exe.
 * Messages are not delivered through direct cross-process callbacks. Instead,
 * the broker keeps small fixed-size queues per process, lets receivers pull
 * messages explicitly, and routes completion status back to the sender.
 *
 * The design goals are:
 * - predictable memory usage
 * - simple multi-process messaging semantics
 * - automatic disposal of stale queued or in-flight messages
 */

#define IPC_MAX_PROCESSES 32UL
#define IPC_QUEUE_DEPTH 4UL
#define IPC_STATUS_DEPTH 4UL
#define IPC_MESSAGE_TTL_STEPS 32UL



 /* One slot in a receiver inbox queue. */
typedef struct IPCQueueSlot {
    Bool in_use;
    IPCMessage message;
} IPCQueueSlot;

/* One completion-status record waiting for the sender to consume it. */
typedef struct IPCStatusSlot {
    Bool in_use;
    ULong expires_sequence;
    IPCDeliveryStatus status;
} IPCStatusSlot;

/*
 * Per-process broker state.
 *
 * `queue` holds messages waiting for the receiver.
 * `inflight_message` holds the single message currently checked out by that
 * receiver until it acknowledges completion.
 * `statuses` holds completion notifications for the original sender.
 */
typedef struct IPCProcessSlot {
    Bool in_use;
    Long pid;
    ULong next_message_id;
    Bool inflight_in_use;
    IPCMessage inflight_message;
    IPCQueueSlot queue[IPC_QUEUE_DEPTH];
    IPCStatusSlot statuses[IPC_STATUS_DEPTH];
} IPCProcessSlot;

static IPCProcessSlot g_ipc_processes[IPC_MAX_PROCESSES];
static ULong g_ipc_sequence = 1;

DLL_EXPORT(IPCRegisterProcess);
DLL_EXPORT(IPCUnregisterProcess);
DLL_EXPORT(IPCSendMessage);
DLL_EXPORT(IPCSendMessageEx);
DLL_EXPORT(IPCReceiveMessage);
DLL_EXPORT(IPCCompleteMessage);
DLL_EXPORT(IPCReceiveStatus);

/* Clear a broker-owned structure without relying on a libc memset. */
static void ipc_zero_bytes(void* buffer, ULong byte_count) {
    UByte* buffer_bytes = (UByte*)buffer;

    if (!buffer) {
        return;
    }

    for (ULong byte_index = 0; byte_index < byte_count; byte_index++) {
        buffer_bytes[byte_index] = 0;
    }
}

/* Copy caller text into a fixed-size payload buffer and always NUL-terminate it. */
static void ipc_copy_text(char* destination, ULong destination_size, const char* source_text) {
    ULong text_index = 0;

    if (!destination || destination_size == 0) {
        return;
    }
    if (!source_text) {
        destination[0] = '\0';
        return;
    }

    while (text_index + 1 < destination_size && source_text[text_index] != '\0') {
        destination[text_index] = source_text[text_index];
        text_index++;
    }
    destination[text_index] = '\0';
}

/* Check whether the PID currently resolves to a live user task. */
static Bool ipc_process_exists(Long process_id) {
    UserTaskInfo task_info;

    if (process_id < 0) {
        return FALSE;
    }

    return getTaskInfo(process_id, &task_info) > 0 && task_info.state != 0;
}

/* Drop all broker state associated with one process slot. */
static void ipc_clear_process_slot(IPCProcessSlot* process_slot) {
    if (!process_slot) {
        return;
    }

    ipc_zero_bytes(process_slot, sizeof(*process_slot));
}

/* Advance the broker sweep counter used for message expiry. */
static ULong ipc_next_sequence(void) {
    ULong current_sequence = g_ipc_sequence;

    if (g_ipc_sequence == (ULong)-1) {
        g_ipc_sequence = 1;
    }
    else {
        g_ipc_sequence++;
    }

    return current_sequence;
}

/* Find an existing broker slot for a PID. */
static IPCProcessSlot* ipc_find_process_slot(Long process_id) {
    for (ULong process_index = 0; process_index < IPC_MAX_PROCESSES; process_index++) {
        if (g_ipc_processes[process_index].in_use && g_ipc_processes[process_index].pid == process_id) {
            return &g_ipc_processes[process_index];
        }
    }

    return null;
}

/* Return an existing slot or allocate a new empty one for the PID. */
static IPCProcessSlot* ipc_reserve_process_slot(Long process_id) {
    IPCProcessSlot* process_slot = ipc_find_process_slot(process_id);

    if (process_slot) {
        return process_slot;
    }

    for (ULong process_index = 0; process_index < IPC_MAX_PROCESSES; process_index++) {
        if (!g_ipc_processes[process_index].in_use) {
            ipc_zero_bytes(&g_ipc_processes[process_index], sizeof(g_ipc_processes[process_index]));
            g_ipc_processes[process_index].in_use = TRUE;
            g_ipc_processes[process_index].pid = process_id;
            g_ipc_processes[process_index].next_message_id = 1;
            return &g_ipc_processes[process_index];
        }
    }

    return null;
}

/*
 * Remove expired queue entries and compact the remaining inbox messages toward
 * the front so the first valid entry is always at index 0.
 */
static void ipc_compact_queue(IPCProcessSlot* process_slot, ULong current_sequence) {
    ULong next_write_index = 0;

    if (!process_slot) {
        return;
    }

    for (ULong read_index = 0; read_index < IPC_QUEUE_DEPTH; read_index++) {
        IPCQueueSlot queue_entry = process_slot->queue[read_index];

        if (!queue_entry.in_use) {
            continue;
        }
        if (queue_entry.message.expires_sequence <= current_sequence) {
            continue;
        }
        if (next_write_index != read_index) {
            process_slot->queue[next_write_index] = queue_entry;
        }
        next_write_index++;
    }

    while (next_write_index < IPC_QUEUE_DEPTH) {
        ipc_zero_bytes(&process_slot->queue[next_write_index], sizeof(process_slot->queue[next_write_index]));
        next_write_index++;
    }
}

/* Compact the sender's completion-status queue and discard expired records. */
static void ipc_compact_statuses(IPCProcessSlot* process_slot, ULong current_sequence) {
    ULong next_write_index = 0;

    if (!process_slot) {
        return;
    }

    for (ULong read_index = 0; read_index < IPC_STATUS_DEPTH; read_index++) {
        IPCStatusSlot status_entry = process_slot->statuses[read_index];

        if (!status_entry.in_use) {
            continue;
        }
        if (status_entry.expires_sequence <= current_sequence) {
            continue;
        }
        if (next_write_index != read_index) {
            process_slot->statuses[next_write_index] = status_entry;
        }
        next_write_index++;
    }

    while (next_write_index < IPC_STATUS_DEPTH) {
        ipc_zero_bytes(&process_slot->statuses[next_write_index], sizeof(process_slot->statuses[next_write_index]));
        next_write_index++;
    }
}

/*
 * Queue one completion record for the sender. If the sender status queue is
 * full, the oldest status is dropped so the latest completion survives.
 */
static void ipc_push_status_event(Long sender_pid, ULong message_id, Long receiver_pid, Long result_code, ULong current_sequence) {
    IPCProcessSlot* sender_process_slot;
    ULong status_write_index;

    if (!ipc_process_exists(sender_pid)) {
        return;
    }

    sender_process_slot = ipc_reserve_process_slot(sender_pid);
    if (!sender_process_slot) {
        return;
    }

    ipc_compact_statuses(sender_process_slot, current_sequence);

    status_write_index = IPC_STATUS_DEPTH;
    for (ULong status_index = 0; status_index < IPC_STATUS_DEPTH; status_index++) {
        if (!sender_process_slot->statuses[status_index].in_use) {
            status_write_index = status_index;
            break;
        }
    }

    if (status_write_index == IPC_STATUS_DEPTH) {
        for (ULong status_index = 1; status_index < IPC_STATUS_DEPTH; status_index++) {
            sender_process_slot->statuses[status_index - 1] = sender_process_slot->statuses[status_index];
        }
        status_write_index = IPC_STATUS_DEPTH - 1;
    }

    ipc_zero_bytes(&sender_process_slot->statuses[status_write_index], sizeof(sender_process_slot->statuses[status_write_index]));
    sender_process_slot->statuses[status_write_index].in_use = TRUE;
    sender_process_slot->statuses[status_write_index].expires_sequence = current_sequence + IPC_MESSAGE_TTL_STEPS;
    sender_process_slot->statuses[status_write_index].status.message_id = message_id;
    sender_process_slot->statuses[status_write_index].status.sender_pid = sender_pid;
    sender_process_slot->statuses[status_write_index].status.receiver_pid = receiver_pid;
    sender_process_slot->statuses[status_write_index].status.result = result_code;
}

/*
 * Sweep broker state before public operations.
 *
 * This single pass advances the logical clock, drops dead-process state,
 * expires stale inbox and status entries, and converts expired in-flight work
 * into a completion status for the original sender when possible.
 */
static void ipc_sweep(void) {
    ULong current_sequence = ipc_next_sequence();

    for (ULong process_index = 0; process_index < IPC_MAX_PROCESSES; process_index++) {
        IPCProcessSlot* process_slot = &g_ipc_processes[process_index];

        if (!process_slot->in_use) {
            continue;
        }
        if (!ipc_process_exists(process_slot->pid)) {
            ipc_clear_process_slot(process_slot);
            continue;
        }

        ipc_compact_queue(process_slot, current_sequence);
        ipc_compact_statuses(process_slot, current_sequence);

        if (process_slot->inflight_in_use && process_slot->inflight_message.expires_sequence <= current_sequence) {
            ipc_push_status_event(process_slot->inflight_message.sender_pid,
                process_slot->inflight_message.message_id,
                process_slot->pid,
                ROS_CORE_IPC_STATUS_EXPIRED,
                current_sequence);
            process_slot->inflight_in_use = FALSE;
            ipc_zero_bytes(&process_slot->inflight_message, sizeof(process_slot->inflight_message));
        }
    }
}

/* Try to append one message to the receiver queue after dropping expired items. */
static Long ipc_enqueue_message(IPCProcessSlot* receiver_process_slot, const IPCMessage* queued_message, ULong current_sequence) {
    if (!receiver_process_slot || !queued_message) {
        return ROS_CORE_IPC_STATUS_INVALID;
    }

    ipc_compact_queue(receiver_process_slot, current_sequence);
    for (ULong queue_index = 0; queue_index < IPC_QUEUE_DEPTH; queue_index++) {
        if (!receiver_process_slot->queue[queue_index].in_use) {
            receiver_process_slot->queue[queue_index].in_use = TRUE;
            receiver_process_slot->queue[queue_index].message = *queued_message;
            return ROS_CORE_IPC_STATUS_OK;
        }
    }

    return ROS_CORE_IPC_STATUS_QUEUE_FULL;
}

/* Register a process with the broker so it can send and receive messages. */
Long IPCRegisterProcess(Long process_id) {
    ipc_sweep();
    if (!ipc_process_exists(process_id)) {
        return ROS_CORE_IPC_STATUS_NO_DESTINATION;
    }
    if (!ipc_reserve_process_slot(process_id)) {
        return ROS_CORE_IPC_STATUS_BUSY;
    }

    return ROS_CORE_IPC_STATUS_OK;
}

/* Remove a process and discard any pending queue or status state for it. */
Long IPCUnregisterProcess(Long process_id) {
    IPCProcessSlot* process_slot;

    ipc_sweep();
    process_slot = ipc_find_process_slot(process_id);
    if (!process_slot) {
        return ROS_CORE_IPC_STATUS_EMPTY;
    }

    ipc_clear_process_slot(process_slot);
    return ROS_CORE_IPC_STATUS_OK;
}

/*
 * Send one payload to another process.
 *
 * Arguments:
 * - sender_pid: process originating the message
 * - receiver_pid: destination process that must already exist
 * - message_content: UTF-8 text copied into the fixed payload buffer
 * - out_message_id: optional pointer that receives the broker-assigned ID
 */
Long IPCSendMessageEx(Long sender_pid, Long receiver_pid, const char* message_content, ULong* out_message_id) {
    IPCProcessSlot* receiver_process_slot;
    IPCMessage outgoing_message;
    ULong current_sequence;
    Long send_result;

    ipc_sweep();
    if (sender_pid < 0 || receiver_pid < 0 || !message_content) {
        return ROS_CORE_IPC_STATUS_INVALID;
    }
    if (!ipc_process_exists(sender_pid)) {
        return ROS_CORE_IPC_STATUS_INVALID;
    }
    if (!ipc_process_exists(receiver_pid)) {
        return ROS_CORE_IPC_STATUS_NO_DESTINATION;
    }
    if (!ipc_reserve_process_slot(sender_pid)) {
        return ROS_CORE_IPC_STATUS_BUSY;
    }

    receiver_process_slot = ipc_reserve_process_slot(receiver_pid);
    if (!receiver_process_slot) {
        return ROS_CORE_IPC_STATUS_BUSY;
    }

    /* Build the broker-owned message copy that will be queued for the receiver. */
    current_sequence = g_ipc_sequence;
    ipc_zero_bytes(&outgoing_message, sizeof(outgoing_message));
    outgoing_message.message_id = receiver_process_slot->next_message_id++;
    outgoing_message.sender_pid = sender_pid;
    outgoing_message.receiver_pid = receiver_pid;
    outgoing_message.created_sequence = current_sequence;
    outgoing_message.expires_sequence = current_sequence + IPC_MESSAGE_TTL_STEPS;
    ipc_copy_text(outgoing_message.content, sizeof(outgoing_message.content), message_content);

    send_result = ipc_enqueue_message(receiver_process_slot, &outgoing_message, current_sequence);
    if (send_result == ROS_CORE_IPC_STATUS_OK && out_message_id) {
        *out_message_id = outgoing_message.message_id;
    }

    return send_result;
}

/* Convenience wrapper when the caller does not need the broker-assigned ID. */
Long IPCSendMessage(Long sender_pid, Long receiver_pid, const char* message_content) {
    return IPCSendMessageEx(sender_pid, receiver_pid, message_content, null);
}

/*
 * Let a receiver process pull the next available message.
 *
 * The broker moves the selected queue entry into the receiver's single
 * in-flight slot. The receiver must later acknowledge it with
 * `IPCCompleteMessage`.
 */
Long IPCReceiveMessage(Long receiver_pid, IPCMessage* out_message) {
    IPCProcessSlot* receiver_process_slot;
    ULong current_sequence;

    ipc_sweep();
    if (receiver_pid < 0 || !out_message) {
        return ROS_CORE_IPC_STATUS_INVALID;
    }
    if (!ipc_process_exists(receiver_pid)) {
        return ROS_CORE_IPC_STATUS_NO_DESTINATION;
    }

    receiver_process_slot = ipc_find_process_slot(receiver_pid);
    if (!receiver_process_slot) {
        return ROS_CORE_IPC_STATUS_EMPTY;
    }

    current_sequence = g_ipc_sequence;
    ipc_compact_queue(receiver_process_slot, current_sequence);
    if (receiver_process_slot->inflight_in_use) {
        return ROS_CORE_IPC_STATUS_BUSY;
    }
    if (!receiver_process_slot->queue[0].in_use) {
        return ROS_CORE_IPC_STATUS_EMPTY;
    }

    /* Promote the head of the queue into the in-flight slot for this receiver. */
    receiver_process_slot->inflight_in_use = TRUE;
    receiver_process_slot->inflight_message = receiver_process_slot->queue[0].message;
    *out_message = receiver_process_slot->inflight_message;

    for (ULong queue_index = 1; queue_index < IPC_QUEUE_DEPTH; queue_index++) {
        receiver_process_slot->queue[queue_index - 1] = receiver_process_slot->queue[queue_index];
    }
    ipc_zero_bytes(&receiver_process_slot->queue[IPC_QUEUE_DEPTH - 1], sizeof(receiver_process_slot->queue[IPC_QUEUE_DEPTH - 1]));

    return ROS_CORE_IPC_STATUS_OK;
}

/*
 * Acknowledge the receiver's current in-flight message and route the result
 * back to the original sender.
 */
Long IPCCompleteMessage(Long receiver_pid, ULong message_id, Long receiver_status) {
    IPCProcessSlot* receiver_process_slot;
    IPCMessage completed_message;

    ipc_sweep();
    if (receiver_pid < 0) {
        return ROS_CORE_IPC_STATUS_INVALID;
    }
    if (!ipc_process_exists(receiver_pid)) {
        return ROS_CORE_IPC_STATUS_NO_DESTINATION;
    }

    receiver_process_slot = ipc_find_process_slot(receiver_pid);
    if (!receiver_process_slot || !receiver_process_slot->inflight_in_use) {
        return ROS_CORE_IPC_STATUS_EMPTY;
    }
    if (receiver_process_slot->inflight_message.message_id != message_id) {
        return ROS_CORE_IPC_STATUS_INVALID;
    }

    completed_message = receiver_process_slot->inflight_message;
    receiver_process_slot->inflight_in_use = FALSE;
    ipc_zero_bytes(&receiver_process_slot->inflight_message, sizeof(receiver_process_slot->inflight_message));

    /* Persist a completion record so the sender can observe the result later. */
    ipc_push_status_event(completed_message.sender_pid,
        completed_message.message_id,
        completed_message.receiver_pid,
        receiver_status,
        g_ipc_sequence);

    if (!ipc_process_exists(completed_message.sender_pid)) {
        return ROS_CORE_IPC_STATUS_NO_SENDER;
    }

    return ROS_CORE_IPC_STATUS_OK;
}

/*
 * Let a sender consume the oldest pending completion status.
 *
 * This is the sender-side mirror of `IPCCompleteMessage` and drains the status
 * queue one entry at a time.
 */
Long IPCReceiveStatus(Long sender_pid, IPCDeliveryStatus* out_status) {
    IPCProcessSlot* sender_process_slot;
    ULong current_sequence;

    ipc_sweep();
    if (sender_pid < 0 || !out_status) {
        return ROS_CORE_IPC_STATUS_INVALID;
    }
    if (!ipc_process_exists(sender_pid)) {
        return ROS_CORE_IPC_STATUS_INVALID;
    }

    sender_process_slot = ipc_find_process_slot(sender_pid);
    if (!sender_process_slot) {
        return ROS_CORE_IPC_STATUS_EMPTY;
    }

    current_sequence = g_ipc_sequence;
    ipc_compact_statuses(sender_process_slot, current_sequence);
    if (!sender_process_slot->statuses[0].in_use) {
        return ROS_CORE_IPC_STATUS_EMPTY;
    }

    /* Return the oldest status and shift the remaining queue forward. */
    *out_status = sender_process_slot->statuses[0].status;
    for (ULong status_index = 1; status_index < IPC_STATUS_DEPTH; status_index++) {
        sender_process_slot->statuses[status_index - 1] = sender_process_slot->statuses[status_index];
    }
    ipc_zero_bytes(&sender_process_slot->statuses[IPC_STATUS_DEPTH - 1], sizeof(sender_process_slot->statuses[IPC_STATUS_DEPTH - 1]));

    return ROS_CORE_IPC_STATUS_OK;
}