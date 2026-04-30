#include "app/app.h"

DLL_EXPORT(SampleMathAdd3);
DLL_EXPORT(SampleMathInvocationCount);
DLL_EXPORT(SampleMathProfile);
DLL_EXPORT(SampleMathSetName);
DLL_EXPORT(SampleMathGetName);

static unsigned long g_samplemath_invocation_count;
static const char g_samplemath_profile[] = "dll-loader-shared-memory-v1";

#define SAMPLEMATH_SHARED_NAME_OBJECT "samplemath.shared.name"
#define SAMPLEMATH_SHARED_NAME_CAPACITY 128U
#define SAMPLEMATH_SHARED_STATE_VERSION 1UL

typedef struct SampleMathSharedState {
    unsigned long version;
    char name[SAMPLEMATH_SHARED_NAME_CAPACITY];
} SampleMathSharedState;

static SampleMathSharedState* g_samplemath_shared_state;
static long samplemath_write_line(const char* text);

/*
 * samplemath_copy_text
 *
 * The shared-name demo needs one tiny freestanding copier so it can update the
 * shared block without depending on a hosted libc implementation.
 *
 * @param destination Destination character buffer.
 * @param capacity Destination buffer capacity.
 * @param source Source string to copy.
 * @return Nothing.
 */
static void samplemath_copy_text(char* destination, unsigned long capacity, const char* source) {
    unsigned long index = 0UL;

    if (destination == 0 || capacity == 0UL) {
        return;
    }

    if (source == 0) {
        destination[0] = '\0';
        return;
    }

    while (source[index] != '\0' && (index + 1UL) < capacity) {
        destination[index] = source[index];
        ++index;
    }

    destination[index] = '\0';
}

/*
 * samplemath_shared_state
 *
 * DLL globals remain process-local because each process receives its own image
 * copy. This helper instead opens one named shared region so `setName` and
 * `getName` can exchange bytes across processes through one common backing.
 *
 * @return Mapped shared-state pointer, or NULL when the mapping failed.
 */
static SampleMathSharedState* samplemath_shared_state(void) {
    void* address = 0;

    if (g_samplemath_shared_state != 0) {
        return g_samplemath_shared_state;
    }

    if (acquireSharedMemoryRegion(SAMPLEMATH_SHARED_NAME_OBJECT, sizeof(SampleMathSharedState), &address) < 0) {
        (void)samplemath_write_line("samplemath.dll: shared memory acquire failed");
        return 0;
    }

    g_samplemath_shared_state = (SampleMathSharedState*)address;
    if (g_samplemath_shared_state->version != SAMPLEMATH_SHARED_STATE_VERSION) {
        g_samplemath_shared_state->version = SAMPLEMATH_SHARED_STATE_VERSION;
        g_samplemath_shared_state->name[0] = '\0';
    }

    return g_samplemath_shared_state;
}

/*
 * samplemath_write_line
 *
 * Keep all DLL status messages flowing through one wrapper so attach/detach
 * hooks stay easy to scan in the serial log.
 *
 * @param text Null-terminated status line.
 * @return Kernel write status.
 */
static long samplemath_write_line(const char* text) {
    return writeLine(text);
}

/*
 * samplemath_write_count
 *
 * The detach path reports the final call count so the smoke test proves the
 * DLL kept process-local state alive across exported function calls.
 *
 * @param label Prefix text written before the numeric count.
 * @param value Unsigned count to append.
 * @return Kernel write status.
 */
static long samplemath_write_count(const char* label, unsigned long value) {
    char line[160];
    char* cursor = line;

    cursor = appendText(cursor, label);
    cursor = appendUnsignedLong(cursor, value);
    *cursor = '\0';
    return samplemath_write_line(line);
}

/*
 * samplemath_entry
 *
 * The loader calls one DLL entrypoint for process attach and detach. This hook
 * resets the module state on attach and reports the final invocation count on
 * detach so the console shows the full DLL lifecycle.
 *
 * @param image_base Base address where the DLL is mapped in the current process.
 * @param reason Loader notification reason.
 * @return Non-zero success code for the caller.
 */
extern "C" int samplemath_entry(void* image_base, U32 reason) {
    (void)image_base;

    if (reason == DLL_REASON_PROCESS_ATTACH) {
        g_samplemath_invocation_count = 0UL;
        g_samplemath_shared_state = 0;
        (void)samplemath_write_line("samplemath.dll: process attach");
        return 1;
    }
    if (reason == DLL_REASON_PROCESS_DETACH) {
        (void)samplemath_write_count("samplemath.dll: process detach count=", g_samplemath_invocation_count);
        return 1;
    }

    return 1;
}

/*
 * samplemath_add3
 *
 * Return a simple sum plus a running invocation count so the consumer can prove
 * the DLL owns mutable state across repeated calls.
 *
 * @param lhs First addend.
 * @param rhs Second addend.
 * @param extra Third addend.
 * @return Sum of all inputs plus the current call count.
 */
extern "C" long SampleMathAdd3(long lhs, long rhs, long extra) {
    g_samplemath_invocation_count += 1ul;
    return lhs + rhs + extra + (long)g_samplemath_invocation_count;
}

/*
 * samplemath_invocation_count
 *
 * Expose the current call counter so the consumer can assert DLL state stayed
 * live after import resolution and exported calls.
 *
 * @return Current call count.
 */
extern "C" unsigned long SampleMathInvocationCount(void) {
    return g_samplemath_invocation_count;
}

/*
 * samplemath_profile
 *
 * Publish one stable string that lets the consumer prove pointer-returning DLL
 * exports work as expected.
 *
 * @return Null-terminated profile string owned by the DLL image.
 */
extern "C" const char* SampleMathProfile(void) {
    return g_samplemath_profile;
}

/*
 * samplemath_set_name
 *
 * Store one caller-provided name inside the DLL-owned shared-memory object so
 * later callers in other processes can observe the same bytes.
 *
 * @param value Null-terminated name to publish.
 * @return Zero on success, or -1 when the shared block was unavailable.
 */
extern "C" long SampleMathSetName(const char* value) {
    SampleMathSharedState* state = samplemath_shared_state();

    if ((state == 0) || (value == 0)) {
        return -1;
    }

    samplemath_copy_text(state->name, SAMPLEMATH_SHARED_NAME_CAPACITY, value);
    return 0;
}

/*
 * samplemath_get_name
 *
 * Return the caller's local mapping of the shared name buffer.
 *
 * The pointer is process-specific, but the underlying bytes are backed by one
 * shared kernel object, so every process sees the same string contents.
 *
 * @return Shared string pointer, or NULL when the shared block was unavailable.
 */
extern "C" const char* SampleMathGetName(void) {
    SampleMathSharedState* state = samplemath_shared_state();

    if (state == 0) {
        return 0;
    }

    return state->name;
}
