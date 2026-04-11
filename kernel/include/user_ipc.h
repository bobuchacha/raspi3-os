#ifndef KERNEL_INCLUDE_USER_IPC_H
#define KERNEL_INCLUDE_USER_IPC_H

#ifdef __cplusplus
extern "C" {
#endif

    /*
     * Fixed-size text fields keep the kernel broker allocation-free and make the
     * user/kernel ABI stable for the early desktop-management experiments.
     */
#define ROS_USER_IPC_TEXT_MAX 32UL
#define ROS_USER_IPC_TEXT2_MAX 64UL

     /* Receive flags used by the userspace message wrappers. */
#define ROS_USER_IPC_RECEIVE_WAIT 1UL
#define ROS_USER_IPC_RECEIVE_TIMEOUT_SHIFT 8UL
#define ROS_USER_IPC_RECEIVE_TIMEOUT_MASK (~((1UL << ROS_USER_IPC_RECEIVE_TIMEOUT_SHIFT) - 1UL))
#define ROS_USER_IPC_RECEIVE_TIMEOUT_ENCODE(timeout_msec) ((unsigned long)(timeout_msec) << ROS_USER_IPC_RECEIVE_TIMEOUT_SHIFT)
#define ROS_USER_IPC_RECEIVE_TIMEOUT_MSEC(flags) ((unsigned long)(flags) >> ROS_USER_IPC_RECEIVE_TIMEOUT_SHIFT)

/*
 * One routed IPC packet.
 *
 * The broker owns delivery only. Higher-level protocols such as GWES window
 * creation interpret `protocol`, `kind`, the scalar arguments, and the two text
 * payloads in their own wrappers.
 */
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

#ifdef __cplusplus
}
#endif

#endif