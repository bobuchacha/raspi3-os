#ifndef APPLICATIONS_USER_IPC_H
#define APPLICATIONS_USER_IPC_H

/*
 * Keep the userspace-visible IPC packet layout in the applications include tree
 * as well so headers that are parsed outside a full translation unit still see
 * the same fixed ABI as the kernel broker.
 */
#define ROS_USER_IPC_TEXT_MAX 32UL
#define ROS_USER_IPC_TEXT2_MAX 64UL
#define ROS_USER_IPC_RECEIVE_WAIT 1UL
#define ROS_USER_IPC_RECEIVE_TIMEOUT_SHIFT 8UL
#define ROS_USER_IPC_RECEIVE_TIMEOUT_MASK (~((1UL << ROS_USER_IPC_RECEIVE_TIMEOUT_SHIFT) - 1UL))
#define ROS_USER_IPC_RECEIVE_TIMEOUT_ENCODE(timeout_msec) ((unsigned long)(timeout_msec) << ROS_USER_IPC_RECEIVE_TIMEOUT_SHIFT)
#define ROS_USER_IPC_RECEIVE_TIMEOUT_MSEC(flags) ((unsigned long)(flags) >> ROS_USER_IPC_RECEIVE_TIMEOUT_SHIFT)

typedef struct UserIpcMessage {
    long sender_pid;
    long receiver_pid;
    unsigned long protocol;
    unsigned long kind;
    unsigned long arg0;
    unsigned long arg1;
    unsigned long arg2;
    unsigned long arg3;
    char text[ROS_USER_IPC_TEXT_MAX];
    char text2[ROS_USER_IPC_TEXT2_MAX];
} UserIpcMessage;

#endif