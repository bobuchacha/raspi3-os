#include <cstddef>
#include <new>

#include "user_runtime.h"
#include "stdlib.h"

extern "C" {
    typedef void (*ros_init_fn)(void);

    extern ros_init_fn __init_array_start[] __attribute__((weak));
    extern ros_init_fn __init_array_end[] __attribute__((weak));
    extern ros_init_fn __fini_array_start[] __attribute__((weak));
    extern ros_init_fn __fini_array_end[] __attribute__((weak));
}

namespace user::runtime {
    void run_init_array() {
        if (!__init_array_start || !__init_array_end) {
            return;
        }

        for (ros_init_fn* fn = __init_array_start; fn < __init_array_end; ++fn) {
            if (*fn) {
                (*fn)();
            }
        }
    }

    void run_fini_array() {
        if (!__fini_array_start || !__fini_array_end) {
            return;
        }

        for (ros_init_fn* fn = __fini_array_end; fn > __fini_array_start; ) {
            --fn;
            if (*fn) {
                (*fn)();
            }
        }
    }
}

extern "C" void runtimeRunInitArray(void) {
    user::runtime::run_init_array();
}

extern "C" void runtimeRunFiniArray(void) {
    user::runtime::run_fini_array();
}

void* operator new(std::size_t size) {
    void* memory = malloc(size ? size : 1);

    if (!memory) {
        writeLine("operator new failed");
        exitProcess(253);
    }
    return memory;
}

void* operator new[](std::size_t size) {
    return operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return malloc(size ? size : 1);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return malloc(size ? size : 1);
}

void operator delete(void* ptr) noexcept {
    free(ptr);
}

void operator delete[](void* ptr) noexcept {
    free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    free(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    free(ptr);
}

extern "C" int __cxa_atexit(void (*destructor)(void*), void* arg, void* dso_handle) {
    (void)destructor;
    (void)arg;
    (void)dso_handle;
    return 0;
}

extern "C" int __cxa_guard_acquire(unsigned long long* guard) {
    return (guard && *guard == 0ULL) ? 1 : 0;
}

extern "C" void __cxa_guard_release(unsigned long long* guard) {
    if (guard) {
        *guard = 1ULL;
    }
}

extern "C" void __cxa_guard_abort(unsigned long long* guard) {
    (void)guard;
}

extern "C" void __cxa_pure_virtual(void) {
    writeLine("pure virtual call");
    exitProcess(252);
}

const std::nothrow_t std::nothrow = std::nothrow_t();