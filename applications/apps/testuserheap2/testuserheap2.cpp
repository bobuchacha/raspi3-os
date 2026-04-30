#define ROS_APP_WITH_CRT 1
#include "app/app.h"

#include <stdint.h>

DECLARE(char*, SayHello, (char* name), FROM, "testuserheap.dll", "SayHello");
DECLARE(const char*, SaySomething, (void), FROM, "testuserheap2.dll", "SaySomething");

int main(void) {
    void* scratch;
    char* message;
    const char* something;
    char name[] = "testuserheap2.exe";

    if (SayHello == 0 || SaySomething == 0) {
        (void)crt_printf("testuserheap2.exe: import slot was not resolved\r\n");
        return 1;
    }

    (void)crt_printf(
        "testuserheap2.exe: imported SayHello=0x%lx SaySomething=0x%lx\r\n",
        (unsigned long)(uintptr_t)SayHello,
        (unsigned long)(uintptr_t)SaySomething
    );

    something = SaySomething();
    (void)crt_printf(
        "testuserheap2.exe: SaySomething returned \"%s\" at 0x%lx\r\n",
        something != NULL ? something : "<null>",
        (unsigned long)(uintptr_t)something
    );

    scratch = crt_malloc(512U);
    if (scratch == NULL) {
        (void)crt_printf("testuserheap2.exe: 512-byte allocation failed\r\n");
        return 2;
    }

    crt_memset(scratch, 0xA5, 512U);
    (void)crt_printf(
        "testuserheap2.exe: scratch512=0x%lx\r\n",
        (unsigned long)(uintptr_t)scratch
    );

    message = SayHello(name);
    if (message == NULL) {
        crt_free(scratch);
        (void)crt_printf("testuserheap2.exe: SayHello returned null\r\n");
        return 3;
    }

    (void)crt_printf(
        "testuserheap2.exe: text=\"%s\" address=0x%lx\r\n",
        message,
        (unsigned long)(uintptr_t)message
    );

    crt_free(message);
    crt_free(scratch);
    return 0;
}
