#define ROS_APP_WITH_CRT 1
#include "app/app.h"

#include <stddef.h>
#include <stdint.h>

DECLARE(void*, malloc, (size_t size), FROM, CRT_CLIENT_MODULE_NAME, "malloc");
DECLARE(int, sprintf, (char* buffer, const char* fmt, ...), FROM, CRT_CLIENT_MODULE_NAME, "sprintf");
DECLARE(int, printf, (const char* fmt, ...), FROM, CRT_CLIENT_MODULE_NAME, "printf");
DECLARE(size_t, strlen, (const char* text), FROM, CRT_CLIENT_MODULE_NAME, "strlen");

DLL_EXPORT(SayHello);

static unsigned long g_testuserheap_sayhello_count;
static void* g_testuserheap_image_base;

extern "C" char* SayHello(char* name);

extern "C" int testuserheap_entry(void* image_base, U32 reason) {
    char task_name[64] = { 0 };

    if (getTaskName(task_name, sizeof(task_name)) < 0) {
        task_name[0] = '\0';
    }

    if (reason == DLL_REASON_PROCESS_ATTACH) {
        g_testuserheap_image_base = image_base;
        g_testuserheap_sayhello_count = 0UL;
        (void)printf(
            "testuserheap.dll: attach task=%s base=0x%lx SayHello=0x%lx\r\n",
            task_name[0] != '\0' ? task_name : "<unknown>",
            (unsigned long)(uintptr_t)g_testuserheap_image_base,
            (unsigned long)(uintptr_t)&SayHello
        );
        return 1;
    }

    if (reason == DLL_REASON_PROCESS_DETACH) {
        (void)printf(
            "testuserheap.dll: detach task=%s count=%lu base=0x%lx SayHello=0x%lx\r\n",
            task_name[0] != '\0' ? task_name : "<unknown>",
            g_testuserheap_sayhello_count,
            (unsigned long)(uintptr_t)g_testuserheap_image_base,
            (unsigned long)(uintptr_t)&SayHello
        );
        return 1;
    }

    return 1;
}

extern "C" char* SayHello(char* name) {
    static const char prefix[] = "Hello from ";
    const char* safe_name = (name != NULL) ? name : "<null>";
    const size_t total_bytes = (sizeof(prefix) - 1U) + strlen(safe_name) + 1U;
    char task_name[64] = { 0 };
    char* message = (char*)malloc(total_bytes);

    if (message == NULL) {
        (void)printf("testuserheap.dll: malloc failed\r\n");
        return NULL;
    }

    if (getTaskName(task_name, sizeof(task_name)) < 0) {
        task_name[0] = '\0';
    }

    g_testuserheap_sayhello_count += 1UL;
    (void)sprintf(message, "Hello from %s", safe_name);
    (void)printf(
        "testuserheap.dll: task=%s count=%lu SayHello=0x%lx text=\"%s\" heap=0x%lx\r\n",
        task_name[0] != '\0' ? task_name : "<unknown>",
        g_testuserheap_sayhello_count,
        (unsigned long)(uintptr_t)&SayHello,
        message,
        (unsigned long)(uintptr_t)message
    );
    return message;
}
