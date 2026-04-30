#include <cstddef>
#include <new>

#include "user_runtime.h"

extern "C" {
    typedef void (*ros_init_fn)(void);

    extern ros_init_fn __init_array_start[] __attribute__((weak));
    extern ros_init_fn __init_array_end[] __attribute__((weak));
    extern ros_init_fn __fini_array_start[] __attribute__((weak));
    extern ros_init_fn __fini_array_end[] __attribute__((weak));
}

namespace {

    /*
     * Allocate one C-style heap block from the shared userspace heap.
     *
     * Providing the plain C allocation entry points inside the freestanding C++
     * runtime keeps mixed C/C++ modules from leaking unresolved `malloc` and
     * `free` symbols into the loader metadata.
     *
     * @param size Requested allocation size in bytes.
     * @return Allocation pointer, or null on failure.
     */
    extern "C" __attribute__((weak)) void* malloc(std::size_t size) {
        return user_shared_heap_malloc(size ? size : 1U);
    }

    /*
     * Release one C-style heap block previously returned by malloc.
     *
     * @param ptr Allocation to release.
     * @return Nothing.
     */
    extern "C" __attribute__((weak)) void free(void* ptr) {
        user_shared_heap_free(ptr);
    }

    /*
     * Resize one C-style heap block in place when possible.
     *
     * @param ptr Existing allocation, or null.
     * @param size New requested size.
     * @return Resized allocation, or null on failure.
     */
    extern "C" __attribute__((weak)) void* realloc(void* ptr, std::size_t size) {
        return user_shared_heap_realloc(ptr, size);
    }

    /*
     * Fill one caller-owned byte range with a repeated byte value.
     *
     * The compiler may lower aggregate initialization to `memset`, so freestanding
     * DLLs need a local definition instead of inheriting a synthetic import.
     *
     * @param destination Buffer to fill.
     * @param value Byte value written to every position.
     * @param size Number of bytes to write.
     * @return Original destination pointer.
     */
    extern "C" __attribute__((weak)) void* memset(void* destination, int value, std::size_t size) {
        unsigned char* bytes = static_cast<unsigned char*>(destination);
        std::size_t index;

        if (bytes == nullptr) {
            return destination;
        }

        for (index = 0U; index < size; ++index) {
            bytes[index] = static_cast<unsigned char>(value);
        }

        return destination;
    }

    /*
     * Run the constructor array emitted for one DLL image.
     *
     * The loader enters each module through ros_support, so the runtime must
     * explicitly walk the ELF init array to preserve C++ static initialization
     * without relying on a hosted CRT.
     *
     * @return Nothing.
     */
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

    /*
     * Run the destructor array for one DLL image in reverse order.
     *
     * Matching the reverse walk used by hosted runtimes keeps module teardown
     * predictable when one global object's destructor depends on another.
     *
     * @return Nothing.
     */
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

} // namespace

/*
 * Invoke every registered C++ constructor for the current module.
 *
 * ros_support calls this weak hook during DLL attach so each module can bring
 * up static storage before user code executes.
 *
 * @return Nothing.
 */
extern "C" void runtimeRunInitArray(void) {
    run_init_array();
}

/*
 * Invoke every registered C++ destructor for the current module.
 *
 * ros_support calls this weak hook during DLL detach so module-local static
 * state can tear down in the same order expected by C++ code.
 *
 * @return Nothing.
 */
extern "C" void runtimeRunFiniArray(void) {
    run_fini_array();
}

/*
 * Allocate storage for one C++ object.
 *
 * Routing operator new through the existing C allocator keeps allocation policy
 * consistent across C and C++ code inside one userspace process.
 *
 * @param size Requested allocation size in bytes.
 * @return Non-null allocation on success. The process exits on failure.
 */
void* operator new(std::size_t size) {
    void* memory = malloc(size ? size : 1U);

    if (!memory) {
        writeLine("userspace: operator new failed");
        exitProcess(253);
    }

    return memory;
}

/*
 * Allocate storage for one C++ array.
 *
 * Arrays use the same underlying allocator so all C++ allocations share the
 * same ownership rules and debugging behavior.
 *
 * @param size Requested allocation size in bytes.
 * @return Non-null allocation on success. The process exits on failure.
 */
void* operator new[](std::size_t size) {
    return operator new(size);
}

/*
 * Allocate storage without terminating on failure.
 *
 * Some compiler-generated code expects the nothrow overload even when this
 * runtime is otherwise configured with exceptions disabled.
 *
 * @param size Requested allocation size in bytes.
 * @param tag Standard nothrow tag.
 * @return Allocation pointer or null on failure.
 */
void* operator new(std::size_t size, const std::nothrow_t& tag) noexcept {
    (void)tag;
    return malloc(size ? size : 1U);
}

/*
 * Allocate array storage without terminating on failure.
 *
 * Keeping the array nothrow overload present avoids unresolved operator
 * references from compiler-generated array construction helpers.
 *
 * @param size Requested allocation size in bytes.
 * @param tag Standard nothrow tag.
 * @return Allocation pointer or null on failure.
 */
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
    (void)tag;
    return malloc(size ? size : 1U);
}

/*
 * Release one C++ allocation.
 *
 * Deallocation uses the same C runtime backend as operator new so objects can
 * cross simple C/C++ ownership boundaries inside one module safely.
 *
 * @param ptr Allocation returned by operator new.
 * @return Nothing.
 */
void operator delete(void* ptr) noexcept {
    free(ptr);
}

/*
 * Release one C++ array allocation.
 *
 * @param ptr Allocation returned by operator new[].
 * @return Nothing.
 */
void operator delete[](void* ptr) noexcept {
    free(ptr);
}

/*
 * Release one sized C++ allocation.
 *
 * The size parameter is ignored because the shared C allocator already tracks
 * block sizes internally.
 *
 * @param ptr Allocation returned by operator new.
 * @param size Compiler-provided allocation size.
 * @return Nothing.
 */
void operator delete(void* ptr, std::size_t size) noexcept {
    (void)size;
    free(ptr);
}

/*
 * Release one sized C++ array allocation.
 *
 * @param ptr Allocation returned by operator new[].
 * @param size Compiler-provided allocation size.
 * @return Nothing.
 */
void operator delete[](void* ptr, std::size_t size) noexcept {
    (void)size;
    free(ptr);
}

/*
 * Release one nothrow C++ allocation.
 *
 * @param ptr Allocation returned by nothrow operator new.
 * @param tag Standard nothrow tag.
 * @return Nothing.
 */
void operator delete(void* ptr, const std::nothrow_t& tag) noexcept {
    (void)tag;
    free(ptr);
}

/*
 * Release one nothrow C++ array allocation.
 *
 * @param ptr Allocation returned by nothrow operator new[].
 * @param tag Standard nothrow tag.
 * @return Nothing.
 */
void operator delete[](void* ptr, const std::nothrow_t& tag) noexcept {
    (void)tag;
    free(ptr);
}

/*
 * Accept one destructor registration request.
 *
 * The current userspace loader tears modules down wholesale, so individual DSO
 * destructor registration is not persisted yet. Returning success keeps the
 * compiler runtime satisfied until full per-object teardown is needed.
 *
 * @param destructor Destructor callback.
 * @param arg Destructor argument.
 * @param dso_handle Owning module handle.
 * @return Zero to report success.
 */
extern "C" int __cxa_atexit(void (*destructor)(void*), void* arg, void* dso_handle) {
    (void)destructor;
    (void)arg;
    (void)dso_handle;
    return 0;
}

/*
 * Test whether one local static initialization guard is unset.
 *
 * The compiler emits guard checks for function-local statics even with thread
 * safe statics disabled, so this runtime still needs the simple ABI hooks.
 *
 * @param guard Guard word emitted by the compiler.
 * @return One when initialization should proceed, otherwise zero.
 */
extern "C" int __cxa_guard_acquire(unsigned long long* guard) {
    return (guard && *guard == 0ULL) ? 1 : 0;
}

/*
 * Mark one local static initialization guard as complete.
 *
 * @param guard Guard word emitted by the compiler.
 * @return Nothing.
 */
extern "C" void __cxa_guard_release(unsigned long long* guard) {
    if (guard) {
        *guard = 1ULL;
    }
}

/*
 * Abort one in-progress local static initialization.
 *
 * This freestanding runtime does not track partial initialization state beyond
 * leaving the guard unset.
 *
 * @param guard Guard word emitted by the compiler.
 * @return Nothing.
 */
extern "C" void __cxa_guard_abort(unsigned long long* guard) {
    (void)guard;
}

/*
 * Terminate the process after a pure virtual dispatch.
 *
 * Reaching this hook means a C++ object model invariant was violated, so the
 * safest behavior in this environment is an immediate controlled exit.
 *
 * @return Nothing. The process does not continue.
 */
extern "C" void __cxa_pure_virtual(void) {
    writeLine("userspace: pure virtual call");
    exitProcess(252);
}

const std::nothrow_t std::nothrow = std::nothrow_t();
