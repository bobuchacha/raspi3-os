#ifndef ROS_APP_CORE_IPC_H
#define ROS_APP_CORE_IPC_H

#include "app/import.h"

/*
 * Public import surface for the user-mode IPC broker hosted by core.exe.
 *
 * The broker is intentionally conservative:
 * - no shared callback pointers across processes
 * - bounded queue depth per destination process
 * - bounded completion-status backlog per sender process
 * - fixed-size payloads for simple, predictable memory usage
 */

#define ROS_CORE_IPC_PATH "/bin/core.exe"
#define ROS_CORE_IPC_MAX_CONTENT 160U

 /*
  * Result codes returned by broker entry points.
  *
  * These codes describe both delivery attempts and broker-side bookkeeping.
  * Callers are expected to branch on them explicitly instead of assuming that
  * every send or receive request succeeds.
  */
#define ROS_CORE_IPC_STATUS_OK 0
#define ROS_CORE_IPC_STATUS_EMPTY 1
#define ROS_CORE_IPC_STATUS_NO_DESTINATION 2
#define ROS_CORE_IPC_STATUS_QUEUE_FULL 3
#define ROS_CORE_IPC_STATUS_INVALID 4
#define ROS_CORE_IPC_STATUS_BUSY 5
#define ROS_CORE_IPC_STATUS_EXPIRED 6
#define ROS_CORE_IPC_STATUS_NO_SENDER 7

  /*
   * Message record delivered from the broker to a destination process.
   *
   * `message_id` is unique within the receiver's broker slot and is later used
   * by the receiver when acknowledging completion.
   * `created_sequence` and `expires_sequence` are broker sweep counters rather
   * than wall-clock timestamps.
   */
typedef struct IPCMessage {
    ULong message_id;
    Long sender_pid;
    Long receiver_pid;
    ULong created_sequence;
    ULong expires_sequence;
    char content[ROS_CORE_IPC_MAX_CONTENT];
} IPCMessage;

/*
 * Completion record sent back to the original sender.
 *
 * The sender pulls these records explicitly with `coreIpcReceiveStatus`.
 * `result` is owned by the receiving process and can represent application-
 * specific success or failure states.
 */
typedef struct IPCDeliveryStatus {
    ULong message_id;
    Long sender_pid;
    Long receiver_pid;
    Long result;
} IPCDeliveryStatus;

/*
* Import wrappers for the core.exe IPC broker.
*
* Typical usage:
* 1. receiver registers its PID
* 2. sender sends a message
* 3. receiver pulls one message
* 4. receiver completes the message with a result code
* 5. sender pulls completion status
*/
IMPORT_DLL_DECL(Long, coreIpcRegisterProcess, (Long pid), (pid), ROS_CORE_IPC_PATH, "IPCRegisterProcess")
IMPORT_DLL_DECL(Long, coreIpcUnregisterProcess, (Long pid), (pid), ROS_CORE_IPC_PATH, "IPCUnregisterProcess")
IMPORT_DLL_DECL(Long, coreIpcSend, (Long sender_pid, Long receiver_pid, const char* message_content), (sender_pid, receiver_pid, message_content), ROS_CORE_IPC_PATH, "IPCSendMessage")
IMPORT_DLL_DECL(Long, coreIpcSendEx, (Long sender_pid, Long receiver_pid, const char* message_content, ULong* out_message_id), (sender_pid, receiver_pid, message_content, out_message_id), ROS_CORE_IPC_PATH, "IPCSendMessageEx")
IMPORT_DLL_DECL(Long, coreIpcReceive, (Long receiver_pid, IPCMessage* out_message), (receiver_pid, out_message), ROS_CORE_IPC_PATH, "IPCReceiveMessage")
IMPORT_DLL_DECL(Long, coreIpcComplete, (Long receiver_pid, ULong message_id, Long receiver_status), (receiver_pid, message_id, receiver_status), ROS_CORE_IPC_PATH, "IPCCompleteMessage")
IMPORT_DLL_DECL(Long, coreIpcReceiveStatus, (Long sender_pid, IPCDeliveryStatus* out_status), (sender_pid, out_status), ROS_CORE_IPC_PATH, "IPCReceiveStatus")


#endif