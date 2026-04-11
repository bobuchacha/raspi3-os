#include <cstdio>

#include "app/cpp_runtime.hpp"
#include "user_runtime.h"

SYS_EXPORT(Init);
SYS_EXPORT(Deinit);
SYS_EXPORT(__SYS_NAME___dispatch);
SYS_EXPORT(DriverLoop);

namespace {
    class __SYS_NAME___state {
    public:
        __SYS_NAME___state() : dispatch_count(0) {
        }

        unsigned long dispatch_count;
    } g_state;
}

static int __SYS_NAME___on_init(void* base) {
    (void)base;
    g_state.dispatch_count = 0;
    std::printf("__SYS_NAME__: C++ driver online\r\n");
    return 0;
}

static int __SYS_NAME___on_deinit(void* base) {
    (void)base;
    std::printf("__SYS_NAME__: C++ driver offline\r\n");
    return 0;
}

extern "C" int Init(void* base) {
    user::runtime::run_init_array();
    return __SYS_NAME___on_init(base);
}

extern "C" int Deinit(void* base) {
    int status = __SYS_NAME___on_deinit(base);
    user::runtime::run_fini_array();
    return status;
}

extern "C" unsigned long __SYS_NAME___dispatch(unsigned long op, unsigned long value) {
    g_state.dispatch_count++;
    return op + value + g_state.dispatch_count;
}

extern "C" int DriverLoop(void* base) {
    (void)base;
    for (;;) {
        sleepMs(1000);
    }
}