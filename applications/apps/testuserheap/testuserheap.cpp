#define ROS_APP_WITH_CRT 1
#include "app/app.h"

#include <stdint.h>

#define TESTUSERHEAP_CHILD_PATH "/bin/testuserheap2.exe"

DECLARE(char*, SayHello, (char* name), FROM, "testuserheap.dll", "SayHello");

int main(void) {
    char name[] = "testuserheap.exe";
    char* message;
    long child_pid;
    long child_exit_code = -1;
    long wait_status;

    if (SayHello == 0) {
        (void)crt_printf("testuserheap.exe: import slot was not resolved\r\n");
        return 1;
    }

    (void)crt_printf(
        "testuserheap.exe: imported SayHello=0x%lx\r\n",
        (unsigned long)(uintptr_t)SayHello
    );

    message = SayHello(name);
    if (message == NULL) {
        (void)crt_printf("testuserheap.exe: SayHello returned null\r\n");
        return 2;
    }

    (void)crt_printf(
        "testuserheap.exe: text=\"%s\" address=0x%lx\r\n",
        message,
        (unsigned long)(uintptr_t)message
    );
    crt_free(message);

    child_pid = spawnTask(TESTUSERHEAP_CHILD_PATH, "testuserheap2", "");
    if (child_pid < 0) {
        (void)crt_printf("testuserheap.exe: spawn failed status=%ld\r\n", child_pid);
        return 3;
    }

    (void)crt_printf("testuserheap.exe: spawned testuserheap2 pid=%ld\r\n", child_pid);
    wait_status = waitPid(child_pid, &child_exit_code);
    if (wait_status < 0) {
        (void)crt_printf("testuserheap.exe: waitPid failed status=%ld\r\n", wait_status);
        return 4;
    }

    (void)crt_printf(
        "testuserheap.exe: child pid=%ld exit_code=%ld\r\n",
        child_pid,
        child_exit_code
    );
    return (int)child_exit_code;
}
