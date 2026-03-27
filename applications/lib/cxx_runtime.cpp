#include "app/kernel.h"

extern "C"
{

    void* __dso_handle = 0;

    /**
     * Stub out C++ destructor registration for this freestanding runtime.
     *
     * Args:
     *   Unused runtime-provided destructor parameters.
     *
     * Returns:
     *   Always `0` so startup code can proceed without full `atexit` support.
     */
    int __cxa_atexit(void (*)(void*), void*, void*) {
        return 0;
    }

    /**
     * Start one-time initialization for function-local statics.
     *
     * Args:
     *   guard: Guard word managed by the compiler.
     *
     * Returns:
     *   Non-zero when initialization should proceed, or `0` if it already ran.
     */
    int __cxa_guard_acquire(long long* guard) {
        return !*(reinterpret_cast<char*>(guard));
    }

    /**
     * Mark one-time initialization as complete.
     *
     * Args:
     *   guard: Guard word updated after successful initialization.
     *
     * Returns:
     *   Nothing.
     */
    void __cxa_guard_release(long long* guard) {
        *(reinterpret_cast<char*>(guard)) = 1;
    }

    /**
     * Abort one-time initialization after a failed constructor path.
     *
     * Args:
     *   guard: Guard word associated with the failed initialization.
     *
     * Returns:
     *   Nothing. This implementation leaves the guard reset behavior to the caller.
     */
    void __cxa_guard_abort(long long*) {
    }

    /**
     * Handle illegal pure-virtual dispatches in freestanding C++ code.
     *
     * Args:
     *   None.
     *
     * Returns:
     *   Never returns. The process is terminated after logging the failure.
     */
    void __cxa_pure_virtual(void) {
        // Report the runtime bug before killing the process.
        user_kernel_write("cxx_runtime: pure virtual call\n");

        // Exit through the kernel so the bad task is reclaimed instead of looping unpredictably.
        user_kernel_exit(1);
        while (true) {
        }
    }

} // extern "C"

/**
 * Allocate storage for a single C++ object.
 *
 * Args:
 *   size: Requested allocation size in bytes.
 *
 * Returns:
 *   User-space pointer on success, or `nullptr` when the kernel allocator fails.
 */
void* operator new(unsigned long size) {
    return reinterpret_cast<void*>(call_sys_malloc(size ? size : 1));
}

/**
 * Allocate storage for a C++ array object.
 *
 * Args:
 *   size: Requested allocation size in bytes.
 *
 * Returns:
 *   User-space pointer on success, or `nullptr` when the kernel allocator fails.
 */
void* operator new[](unsigned long size) {
    return reinterpret_cast<void*>(call_sys_malloc(size ? size : 1));
}

/**
 * Release storage allocated by scalar `new`.
 *
 * Args:
 *   ptr: Allocation base returned by `operator new`.
 *
 * Returns:
 *   Nothing.
 */
void operator delete(void* ptr) noexcept {
    user_kernel_free(ptr);
}

/**
 * Release storage allocated by array `new[]`.
 *
 * Args:
 *   ptr: Allocation base returned by `operator new[]`.
 *
 * Returns:
 *   Nothing.
 */
void operator delete[](void* ptr) noexcept {
    user_kernel_free(ptr);
}

/**
 * Sized scalar delete variant emitted by newer toolchains.
 *
 * Args:
 *   ptr: Allocation base returned by `operator new`.
 *   Unused size argument provided by the compiler.
 *
 * Returns:
 *   Nothing.
 */
void operator delete(void* ptr, unsigned long) noexcept {
    user_kernel_free(ptr);
}

/**
 * Sized array delete variant emitted by newer toolchains.
 *
 * Args:
 *   ptr: Allocation base returned by `operator new[]`.
 *   Unused size argument provided by the compiler.
 *
 * Returns:
 *   Nothing.
 */
void operator delete[](void* ptr, unsigned long) noexcept {
    user_kernel_free(ptr);
}