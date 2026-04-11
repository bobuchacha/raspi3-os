#include "user_runtime.h"
#include "app/core_ipc.h"

/*
 * ipc_demo.exe
 *
 * Small test program for the core.exe IPC broker.
 *
 * Launch two copies with unique task names:
 * - one receiver: registers itself, waits for one welcome message, then
 *   completes the message and exits
 * - one sender: resolves the receiver by task name, sends a welcome message,
 *   waits for the completion status, and exits
 *
 * Example shell launches:
 *   spawn /bin/ipc_demo.exe ipc-recv recv
 *   spawn /bin/ipc_demo.exe ipc-send send ipc-recv
 */

#define IPC_DEMO_SCAN_MAX 64UL
#define IPC_DEMO_POLL_SLEEP_MSEC 200UL
#define IPC_DEMO_WAIT_ATTEMPTS 25UL

static int ipc_demo_text_equals(const char* left, const char* right) {
    if (!left || !right) {
        return 0;
    }

    while (*left != '\0' && *right != '\0') {
        if (*left != *right) {
            return 0;
        }
        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}

static const char* ipc_demo_skip_spaces(const char* text) {
    while (text && (*text == ' ' || *text == '\t')) {
        text++;
    }

    return text;
}

/*
 * Copy the next whitespace-delimited token into a small stack buffer.
 *
 * The returned pointer always points at the next unread character, which is
 * either the beginning of the next token or the end of the string.
 */
static const char* ipc_demo_read_token(const char* text, char* token, ULong token_size) {
    ULong token_index = 0;

    text = ipc_demo_skip_spaces(text);
    if (!token || token_size == 0) {
        return text;
    }

    while (text && *text != '\0' && *text != ' ' && *text != '\t') {
        if (token_index + 1 < token_size) {
            token[token_index++] = *text;
        }
        text++;
    }
    token[token_index] = '\0';

    return ipc_demo_skip_spaces(text);
}

static void ipc_demo_write_pid_line(const char* label, long pid) {
    char line[192];
    char* cursor = line;

    cursor = appendText(cursor, "ipc_demo.exe: ");
    cursor = appendText(cursor, label);
    cursor = appendUnsignedLong(cursor, (unsigned long)pid);
    *cursor = '\0';
    writeLine(line);
}

static void ipc_demo_write_message(const IPCMessage* message) {
    char line[256];
    char* cursor = line;

    cursor = appendText(cursor, "ipc_demo.exe: received message id=");
    cursor = appendUnsignedLong(cursor, message->message_id);
    cursor = appendText(cursor, " from pid=");
    cursor = appendUnsignedLong(cursor, (unsigned long)message->sender_pid);
    cursor = appendText(cursor, " text=");
    cursor = appendText(cursor, message->content);
    *cursor = '\0';
    writeLine(line);
}

static void ipc_demo_write_status(const IPCDeliveryStatus* status_record) {
    char line[256];
    char* cursor = line;

    cursor = appendText(cursor, "ipc_demo.exe: status message_id=");
    cursor = appendUnsignedLong(cursor, status_record->message_id);
    cursor = appendText(cursor, " sender=");
    cursor = appendUnsignedLong(cursor, (unsigned long)status_record->sender_pid);
    cursor = appendText(cursor, " receiver=");
    cursor = appendUnsignedLong(cursor, (unsigned long)status_record->receiver_pid);
    cursor = appendText(cursor, " result=");
    cursor = appendUnsignedLong(cursor, (unsigned long)status_record->result);
    *cursor = '\0';
    writeLine(line);
}

static int ipc_demo_is_empty_message(const IPCMessage* message) {
    if (!message) {
        return 1;
    }

    return message->message_id == 0 &&
        message->sender_pid == 0 &&
        message->receiver_pid == 0 &&
        message->content[0] == '\0';
}

static long ipc_demo_find_pid_by_name(const char* target_name) {
    UserTaskInfo task_info;

    if (!target_name || target_name[0] == '\0') {
        return -1;
    }

    for (long pid = 0; pid < (long)IPC_DEMO_SCAN_MAX; pid++) {
        if (getTaskInfo(pid, &task_info) <= 0) {
            continue;
        }
        if (task_info.state == 0) {
            continue;
        }
        if (ipc_demo_text_equals(task_info.name, target_name)) {
            return task_info.id;
        }
    }

    return -1;
}

static long ipc_demo_wait_for_pid_by_name(const char* target_name) {
    for (ULong attempt = 0; attempt < IPC_DEMO_WAIT_ATTEMPTS; attempt++) {
        long pid = ipc_demo_find_pid_by_name(target_name);

        if (pid >= 0) {
            return pid;
        }
        sleepMs(IPC_DEMO_POLL_SLEEP_MSEC);
    }

    return -1;
}

/*
 * Resolve the current process PID by scanning for our unique task name.
 *
 * The shell already assigns the task name when it spawns the app, so the demo
 * can discover itself without a dedicated self-PID syscall.
 */
static long ipc_demo_find_self_pid(char* task_name, ULong task_name_size) {
    getTaskName(task_name, task_name_size);
    if (task_name[0] == '\0') {
        return -1;
    }

    return ipc_demo_find_pid_by_name(task_name);
}

static void ipc_demo_wait_for_status(long self_pid) {
    IPCDeliveryStatus status_record;
    long status_code;

    for (;;) {
        status_code = coreIpcReceiveStatus(self_pid, &status_record);
        if (status_code == ROS_CORE_IPC_STATUS_EMPTY) {
            sleepMs(IPC_DEMO_POLL_SLEEP_MSEC);
            continue;
        }
        if (status_code != ROS_CORE_IPC_STATUS_OK) {
            ipc_demo_write_pid_line("failed to read completion status for pid=", self_pid);
            return;
        }

        ipc_demo_write_status(&status_record);
        return;
    }
}

static void ipc_demo_run_receiver(const char* task_args) {
    char task_name[64];
    IPCMessage message;
    long self_pid;
    long register_status;
    long receive_status;
    long complete_status;

    (void)task_args;
    self_pid = ipc_demo_find_self_pid(task_name, sizeof(task_name));
    if (self_pid < 0) {
        writeLine("ipc_demo.exe: unable to resolve receiver pid");
        exitProcess(1);
    }

    register_status = coreIpcRegisterProcess(self_pid);
    if (register_status != ROS_CORE_IPC_STATUS_OK) {
        ipc_demo_write_pid_line("receiver registration failed for pid=", self_pid);
        exitProcess(2);
    }

    ipc_demo_write_pid_line("receiver ready pid=", self_pid);

    for (;;) {
        receive_status = coreIpcReceive(self_pid, &message);
        if (receive_status == ROS_CORE_IPC_STATUS_EMPTY) {
            sleepMs(IPC_DEMO_POLL_SLEEP_MSEC);
            continue;
        }
        if (receive_status != ROS_CORE_IPC_STATUS_OK) {
            ipc_demo_write_pid_line("receiver failed for pid=", self_pid);
            exitProcess(3);
        }

        if (ipc_demo_is_empty_message(&message)) {
            sleepMs(IPC_DEMO_POLL_SLEEP_MSEC);
            continue;
        }

        ipc_demo_write_message(&message);
        complete_status = coreIpcComplete(self_pid, message.message_id, 0);
        if (complete_status != ROS_CORE_IPC_STATUS_OK) {
            ipc_demo_write_pid_line("receiver completion failed for pid=", self_pid);
            exitProcess(4);
        }

        writeLine("ipc_demo.exe: receiver completed welcome message");
        break;
    }

    (void)coreIpcUnregisterProcess(self_pid);
    exitProcess(0);
}

static void ipc_demo_run_sender(const char* task_args) {
    char task_name[64];
    char peer_name[64];
    char welcome_text[ROS_CORE_IPC_MAX_CONTENT];
    char* cursor;
    long self_pid;
    long receiver_pid;
    long register_status;
    long send_status;
    ULong message_id;

    self_pid = ipc_demo_find_self_pid(task_name, sizeof(task_name));
    if (self_pid < 0) {
        writeLine("ipc_demo.exe: unable to resolve sender pid");
        exitProcess(10);
    }

    register_status = coreIpcRegisterProcess(self_pid);
    if (register_status != ROS_CORE_IPC_STATUS_OK) {
        ipc_demo_write_pid_line("sender registration failed for pid=", self_pid);
        exitProcess(11);
    }

    ipc_demo_read_token(task_args, peer_name, sizeof(peer_name));
    if (peer_name[0] == '\0') {
        writeLine("ipc_demo.exe: sender mode expects the receiver task name");
        exitProcess(12);
    }

    receiver_pid = ipc_demo_wait_for_pid_by_name(peer_name);
    if (receiver_pid < 0) {
        writeLine("ipc_demo.exe: receiver task name not found");
        exitProcess(13);
    }

    cursor = welcome_text;
    cursor = appendText(cursor, "welcome from ");
    cursor = appendText(cursor, task_name);
    cursor = appendText(cursor, " pid=");
    cursor = appendUnsignedLong(cursor, (unsigned long)self_pid);
    *cursor = '\0';

    send_status = coreIpcSendEx(self_pid, receiver_pid, welcome_text, (ULong*)&message_id);
    if (send_status != ROS_CORE_IPC_STATUS_OK) {
        ipc_demo_write_pid_line("send failed for pid=", self_pid);
        exitProcess(14);
    }

    ipc_demo_write_pid_line("sent welcome to pid=", receiver_pid);
    ipc_demo_wait_for_status(self_pid);

    (void)coreIpcUnregisterProcess(self_pid);
    (void)message_id;
    exitProcess(0);
}

int main(void) {
    char task_args[128];
    const char* args_cursor;
    char mode[16];

    getTaskArgs(task_args, sizeof(task_args));
    args_cursor = ipc_demo_read_token(task_args, mode, sizeof(mode));

    if (mode[0] == '\0' || ipc_demo_text_equals(mode, "recv") || ipc_demo_text_equals(mode, "receiver")) {
        ipc_demo_run_receiver(args_cursor);
    }

    if (ipc_demo_text_equals(mode, "send") || ipc_demo_text_equals(mode, "sender")) {
        ipc_demo_run_sender(args_cursor);
    }

    writeLine("ipc_demo.exe: expected mode 'recv' or 'send'");
    exitProcess(20);
}