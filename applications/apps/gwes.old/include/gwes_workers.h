#ifndef ROS_APP_GWES_WORKERS_H
#define ROS_APP_GWES_WORKERS_H

#include "app/kernel_gui.h"
#include "app/user_ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

    /*
     * Start the dedicated GWES input and window workers.
     *
     * The current EL0 runtime still lacks a general event object, so GWES keeps
     * one broker-owned service receive loop on the main thread and starts two
     * long-lived workers for shared-input intake and window/render ownership.
     *
     * @return Zero on success, or a negative status code on failure.
     */
    long gwes_workers_start(void);

    /*
     * Run the GWES service-thread receive and demultiplex loop.
     *
     * This thread is the only consumer of the kernel message broker for the
     * GWES process. It forwards client packets into the explicit window-event
     * queue so arbitrary worker threads cannot steal request or reply traffic.
     *
     * @return Nothing.
     */
    void gwes_service_thread_run(void);

    /*
     * Drain one batch of shared-input records into the window-event queue.
     *
     * @return Count of queued input events.
     */
    unsigned long gwes_consume_shared_input(void);

    /*
     * Drain one batch of shared input through the dedicated input file.
     *
     * @return Count of queued input events.
     */
    unsigned long gwes_input_handle_drain_pending(void);

    /*
     * Queue one client protocol packet for the window/render owner thread.
     *
     * @param packet Caller-owned packet copied into the queue.
     * @return Zero on success, or a negative status code on allocation failure.
     */
    long gwes_window_queue_push_protocol_packet(const UserIpcMessage* packet);

    /*
     * Queue one shared-input event for the window/render owner thread.
     *
     * @param event Caller-owned input event copied into the queue.
     * @return Zero on success, or a negative status code on allocation failure.
     */
    long gwes_window_queue_push_input_event(const RosKernelGuiInputEvent* event);

    /*
     * Enter the long-lived shared-input worker thread.
     *
     * @param argument Unused worker argument.
     * @return Nothing.
     */
    void gwes_input_thread_entry(unsigned long argument);

    /*
     * Enter the long-lived window/render owner thread.
     *
     * @param argument Unused worker argument.
     * @return Nothing.
     */
    void gwes_window_thread_entry(unsigned long argument);

    /*
     * Apply one queued client protocol packet on the window/render owner thread.
     *
     * @param packet Protocol packet previously received by the service loop.
     * @return Nothing.
     */
    void gwes_window_thread_handle_packet(const UserIpcMessage* packet);

    /*
     * Apply one queued shared-input event on the window/render owner thread.
     *
     * @param event Shared-input event copied from the ring buffer.
     * @return Nothing.
     */
    void gwes_window_thread_handle_input_event(const RosKernelGuiInputEvent* event);

    /*
     * Reap dead client objects from the window/render owner thread.
     *
     * @return Nothing.
     */
    void gwes_window_thread_reap_dead_clients(void);

    /*
     * Deliver any due per-window timers owned by the window/render thread.
     *
     * @param now Current uptime in milliseconds.
     * @return Count of timers that produced one `WM_TIMER` delivery.
     */
    unsigned long gwes_window_thread_fire_due_timers(unsigned long now);

    /*
     * Compute the time until the next due per-window timer.
     *
     * @param now Current uptime in milliseconds.
     * @return Milliseconds until the next timer should fire.
     */
    unsigned long gwes_window_thread_timer_wait_budget(unsigned long now);

    /*
     * Submit any pending compositor damage through the dedicated window file.
     *
     * @return Nothing.
     */
    void gwes_window_thread_present_if_needed(void);

#ifdef __cplusplus
}
#endif

#endif
