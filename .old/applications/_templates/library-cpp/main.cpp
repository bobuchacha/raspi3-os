#include <cstdio>

#include "app/__LIB_NAME__.h"
#include "app/cpp_runtime.hpp"

DLL_EXPORT(Init);
DLL_EXPORT(Deinit);
DLL_EXPORT(__LIB_NAME___sum);

namespace {
    class __LIB_NAME___state {
    public:
        __LIB_NAME___state() : call_count(0) {
        }

        unsigned long call_count;
    } g_state;
}

static int __LIB_NAME___on_init(void* base) {
    (void)base;
    g_state.call_count = 0;
    std::printf("__LIB_NAME__: C++ library ready\r\n");
    return 0;
}

static int __LIB_NAME___on_deinit(void* base) {
    (void)base;
    std::printf("__LIB_NAME__: C++ library stopping\r\n");
    return 0;
}

extern "C" int Init(void* base) {
    user::runtime::run_init_array();
    return __LIB_NAME___on_init(base);
}

extern "C" int Deinit(void* base) {
    int status = __LIB_NAME___on_deinit(base);
    user::runtime::run_fini_array();
    return status;
}

extern "C" long __LIB_NAME___sum(long left, long right) {
    g_state.call_count++;
    return left + right + static_cast<long>(g_state.call_count);
}