#define ROS_APP_WITH_CRT 1
#include "app/app.h"

#include <stdint.h>

DECLARE(int, printf, (const char* fmt, ...), FROM, CRT_CLIENT_MODULE_NAME, "printf");

DLL_EXPORT(SaySomething);

static const char g_testuserheap2_message[] = "Hello from testuserheap2.dll";
static void* g_testuserheap2_image_base;

extern "C" const char* SaySomething(void);

extern "C" int testuserheap2_entry(void* image_base, U32 reason) {
    char task_name[64] = { 0 };

    if (getTaskName(task_name, sizeof(task_name)) < 0) {
        task_name[0] = '\0';
    }

    if (reason == DLL_REASON_PROCESS_ATTACH) {
        g_testuserheap2_image_base = image_base;
        (void)printf(
            "testuserheap2.dll: attach task=%s base=0x%lx SaySomething=0x%lx\r\n",
            task_name[0] != '\0' ? task_name : "<unknown>",
            (unsigned long)(uintptr_t)g_testuserheap2_image_base,
            (unsigned long)(uintptr_t)&SaySomething
        );
        return 1;
    }

    if (reason == DLL_REASON_PROCESS_DETACH) {
        (void)printf(
            "testuserheap2.dll: detach task=%s base=0x%lx SaySomething=0x%lx\r\n",
            task_name[0] != '\0' ? task_name : "<unknown>",
            (unsigned long)(uintptr_t)g_testuserheap2_image_base,
            (unsigned long)(uintptr_t)&SaySomething
        );
        return 1;
    }

    return 1;
}

extern "C" const char* SaySomething(void) {
    char task_name[64] = { 0 };

    if (getTaskName(task_name, sizeof(task_name)) < 0) {
        task_name[0] = '\0';
    }

    (void)printf(
        "testuserheap2.dll: task=%s SaySomething=0x%lx text=\"%s\"\r\n",
        task_name[0] != '\0' ? task_name : "<unknown>",
        (unsigned long)(uintptr_t)&SaySomething,
        g_testuserheap2_message
    );
    return g_testuserheap2_message;
}
