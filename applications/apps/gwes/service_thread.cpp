#define ROS_WINDOW_SERVER_ONLY 1
#include "gwes_workers.h"

#include "app/app.h"
#include "app/window.h"

#include <string.h>

namespace {

    /*
     * Return the reply kind paired with one synchronous client request.
     *
     * When the window-event queue cannot accept a new request, the service
     * thread must synthesize the failure reply itself so the client does not
     * block forever waiting on a response that will never be produced.
     *
     * @param kind Request packet kind.
     * @return Matching reply kind, or zero when the request has no reply.
     */
    unsigned long gwes_reply_kind_for_request(unsigned long kind) {
        switch (kind) {
        case ROS_WINDOW_SERVER_KIND_REGISTER_CLASS:
            return ROS_WINDOW_SERVER_KIND_REGISTER_CLASS_ACK;
        case ROS_WINDOW_SERVER_KIND_CREATE_WINDOW:
            return ROS_WINDOW_SERVER_KIND_CREATE_WINDOW_REPLY;
        case ROS_WINDOW_SERVER_KIND_SET_WINDOW_CURSOR:
            return ROS_WINDOW_SERVER_KIND_SET_WINDOW_CURSOR_REPLY;
        case ROS_WINDOW_SERVER_KIND_SET_WINDOW_BOUNDS:
            return ROS_WINDOW_SERVER_KIND_SET_WINDOW_BOUNDS_REPLY;
        case ROS_WINDOW_SERVER_KIND_SET_FOREGROUND_WINDOW:
            return ROS_WINDOW_SERVER_KIND_SET_FOREGROUND_WINDOW_REPLY;
        case ROS_WINDOW_SERVER_KIND_SET_TIMER:
            return ROS_WINDOW_SERVER_KIND_SET_TIMER_REPLY;
        case ROS_WINDOW_SERVER_KIND_KILL_TIMER:
            return ROS_WINDOW_SERVER_KIND_KILL_TIMER_REPLY;
        case ROS_WINDOW_SERVER_KIND_TRACK_POPUP_MENU:
            return ROS_WINDOW_SERVER_KIND_TRACK_POPUP_MENU_REPLY;
        default:
            return 0UL;
        }
    }

    /*
     * Send one immediate queue-failure reply for a dropped client request.
     *
     * @param request Original client request.
     * @param status Negative failure status returned to the caller.
     * @return Nothing.
     */
    void gwes_send_queue_failure_reply(const UserIpcMessage* request, long status) {
        UserIpcMessage reply;
        const unsigned long reply_kind = request != NULL ? gwes_reply_kind_for_request(request->kind) : 0UL;

        if ((request == NULL) || (reply_kind == 0UL)) {
            return;
        }

        memset(&reply, 0, sizeof(reply));
        reply.protocol = ROS_WINDOW_SERVER_PROTOCOL;
        reply.kind = reply_kind;
        reply.arg0 = (unsigned long)status;
        reply.arg1 = (unsigned long)status;
        (void)sendUserIpcMessage(request->sender_pid, &reply);
    }

} // namespace

/*
 * Start the dedicated GWES workers that own shared input and window state.
 *
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_workers_start(void) {
    long status;

    status = startUserThread((unsigned long)&gwes_window_thread_entry, 0UL, "gwes.window");
    if (status < 0L) {
        writeLine("gwes.exe: failed to start window thread");
        return status;
    }

    status = startUserThread((unsigned long)&gwes_input_thread_entry, 0UL, "gwes.input");
    if (status < 0L) {
        writeLine("gwes.exe: failed to start input thread");
        return status;
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Run the broker receive loop on the dedicated service path.
 *
 * @return Nothing.
 */
void gwes_service_thread_run(void) {
    for (;;) {
        UserIpcMessage packet;
        long status = receiveUserIpcMessage(&packet, ROS_USER_IPC_RECEIVE_WAIT);

        if (status == ROS_USER_IPC_STATUS_BUSY) {
            continue;
        }
        if (status < 0L) {
            (void)sleepMs(1UL);
            continue;
        }
        if (packet.protocol != ROS_WINDOW_SERVER_PROTOCOL) {
            continue;
        }

        status = gwes_window_queue_push_protocol_packet(&packet);
        if (status < 0L) {
            gwes_send_queue_failure_reply(&packet, status);
        }
    }
}
