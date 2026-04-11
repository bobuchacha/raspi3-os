#ifndef ROS_APP_USER_IPC_H
#define ROS_APP_USER_IPC_H

#include "user_runtime.h"
#include "user_ipc.h"

#define ROS_USER_IPC_STATUS_OK 0L
#define ROS_USER_IPC_STATUS_NOT_FOUND (-2L)
#define ROS_USER_IPC_STATUS_NO_SPACE (-6L)
#define ROS_USER_IPC_STATUS_BUSY (-8L)

/*
 * Send one packet to another user process through the kernel-owned broker.
 *
 * @param receiver_pid Destination process identifier.
 * @param message Caller-owned packet template.
 * @return Zero on success, or a negative kernel status code.
 */
static inline long sendUserIpcMessage(long receiver_pid, const UserIpcMessage* message) {
    return (long)invokeSyscall2(USER_SYS_IPC_SEND, (unsigned long)receiver_pid, (unsigned long)message);
}

/*
 * Receive one packet for the current process.
 *
 * @param message Receives the next queued packet.
 * @param flags Zero for polling or `ROS_USER_IPC_RECEIVE_WAIT` for blocking.
 * @return Zero on success, `ROS_USER_IPC_STATUS_BUSY` when no packet is ready,
 * or another negative kernel status code.
 */
static inline long receiveUserIpcMessage(UserIpcMessage* message, unsigned long flags) {
    return (long)invokeSyscall2(USER_SYS_IPC_RECV, (unsigned long)message, flags);
}

/*
 * Compare two null-terminated ASCII strings for equality.
 *
 * @param left First string.
 * @param right Second string.
 * @return Non-zero when both strings match exactly.
 */
static inline int userIpcTextEquals(const char* left, const char* right) {
    unsigned long index = 0UL;

    if (left == right) {
        return 1;
    }
    if (!left || !right) {
        return 0;
    }

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }
        ++index;
    }

    return left[index] == right[index];
}

/*
 * Compare two task names while tolerating an optional `.exe` suffix.
 *
 * Different launch paths in this tree historically used either the visible
 * short name (`gwes`) or the basename with extension (`gwes.exe`). GUI helpers
 * should not stall on that presentation detail when locating the window
 * server.
 *
 * @param candidate Live task name reported by the kernel.
 * @param expected Requested logical task name.
 * @return Non-zero when the names match exactly or differ only by `.exe`.
 */
static inline int userIpcTaskNameMatches(const char* candidate, const char* expected) {
    unsigned long index = 0UL;

    if (userIpcTextEquals(candidate, expected)) {
        return 1;
    }
    if (!candidate || !expected) {
        return 0;
    }

    while (candidate[index] != '\0' && expected[index] != '\0') {
        if (candidate[index] != expected[index]) {
            return 0;
        }
        ++index;
    }

    if (expected[index] == '\0'
        && candidate[index] == '.'
        && candidate[index + 1UL] == 'e'
        && candidate[index + 2UL] == 'x'
        && candidate[index + 3UL] == 'e'
        && candidate[index + 4UL] == '\0') {
        return 1;
    }

    if (candidate[index] == '\0'
        && expected[index] == '.'
        && expected[index + 1UL] == 'e'
        && expected[index + 2UL] == 'x'
        && expected[index + 3UL] == 'e'
        && expected[index + 4UL] == '\0') {
        return 1;
    }

    return 0;
}

/*
 * Locate one live task by its process name.
 *
 * @param task_name Exact process name to search for.
 * @return Matching PID on success, or `ROS_USER_IPC_STATUS_NOT_FOUND`.
 */
static inline long findUserTaskPidByName(const char* task_name) {
    UserTaskInfo info;

    if (!task_name || task_name[0] == '\0') {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    for (long pid = 0; pid < 128; ++pid) {
        if (getTaskInfo(pid, &info) < 0) {
            continue;
        }
        if (info.main_thread_state == USER_TASK_STATE_TERMINATED) {
            continue;
        }
        if (userIpcTaskNameMatches(info.name, task_name)) {
            return pid;
        }
    }

    return ROS_USER_IPC_STATUS_NOT_FOUND;
}

/*
 * Resolve the current process id by matching the current task name back into
 * the task table.
 *
 * @return Current PID on success, or `ROS_USER_IPC_STATUS_NOT_FOUND`.
 */
static inline long resolveCurrentTaskPid(void) {
    char task_name[32] = { 0 };

    if (getTaskName(task_name, sizeof(task_name)) < 0) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    return findUserTaskPidByName(task_name);
}

#endif
