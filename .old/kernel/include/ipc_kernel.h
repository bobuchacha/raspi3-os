/*
 * ipc_kernel.h
 *
 * Kernel-owned lifetime wrapper for the IPC module.
 *
 * The raw IPC module is written as an embeddable library. This header exposes
 * one process-wide kernel instance so service code and smoke tests can share a
 * single context that uses kernel allocators, the scheduler tick source, and
 * the kernel log.
 */
#ifndef IPC_KERNEL_H
#define IPC_KERNEL_H

 /* Keep the kernel-facing header free of stdbool/stddef users so legacy kernel
  * sources can include it without colliding with `ros.h`. The implementation
  * file includes the full IPC headers.
  */
typedef struct IpcContextStruct IPC_CONTEXT;
typedef IPC_CONTEXT* PIPC_CONTEXT;

#ifdef __cplusplus
extern "C" {
#endif

    /* Initialize the singleton IPC context used by the running kernel. */
    int ipc_kernel_init(void);

    /* Return the live kernel-owned IPC context, or NULL if init failed. */
    PIPC_CONTEXT ipc_kernel_context(void);

    /* Tear down the singleton IPC context during a future controlled shutdown. */
    void ipc_kernel_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* IPC_KERNEL_H */