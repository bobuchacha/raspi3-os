#ifndef KERNEL_INCLUDE_DEBUG_MESSAGE_H
#define KERNEL_INCLUDE_DEBUG_MESSAGE_H

#include <stdarg.h>
#include <stddef.h>

#include "types.h"

/*
 * Keep the shared CE-style header as the canonical macro surface, but disable
 * its automatic prefix builder so kernel callers can supply file, line, and
 * ANSI colors consistently from one wrapper.
 */
#define MCDBG_NO_STDIO 1

#ifdef __cplusplus
extern "C" int vsnprintf(char* buffer, size_t size, const char* fmt, va_list args);
extern "C" int snprintf(char* buffer, size_t size, const char* fmt, ...);
#else
int vsnprintf(char* buffer, size_t size, const char* fmt, va_list args);
int snprintf(char* buffer, size_t size, const char* fmt, ...);
#endif

#define MCDBG_DEFAULT_FLAGS 0u
#include "../../applications/include/debug-message.h"

#ifdef __cplusplus
extern "C" {
#endif

    extern DBGPARAM dpCurSettings;

    /*
     * Return the basename portion of one source file path.
     *
     * @param path Full source path supplied by `__FILE__`.
     * @return Pointer to the last path component inside `path`.
     */
    const char* kernel_debug_short_file(const char* path);

    /*
     * Initialize the kernel debug sink and the initial zone mask.
     *
     * @return Nothing.
     */
    void kernel_debug_initialize(void);

    /*
     * Replace the active kernel debug zone mask at runtime.
     *
     * @param zone_mask New 16-bit zone mask.
     * @return Nothing.
     */
    void kernel_debug_set_zone_mask(U32 zone_mask);

    /*
     * Read the active kernel debug zone mask.
     *
     * @return Current zone mask.
     */
    U32 kernel_debug_zone_mask(void);

    /*
     * Enable one debug zone bit inside the active kernel debug mask.
     *
     * @param zone_bit Zero-based zone bit index in the range [0, 15].
     * @return Nothing.
     */
    void kernel_debug_enable_zone(U32 zone_bit);

    /*
     * Disable one debug zone bit inside the active kernel debug mask.
     *
     * @param zone_bit Zero-based zone bit index in the range [0, 15].
     * @return Nothing.
     */
    void kernel_debug_disable_zone(U32 zone_bit);

    /*
     * Report whether one debug zone bit is currently enabled.
     *
     * @param zone_bit Zero-based zone bit index in the range [0, 15].
     * @return `true` when the requested zone is enabled, otherwise `false`.
     */
    bool kernel_debug_is_zone_enabled(U32 zone_bit);

    /*
     * Remove every source-file allow or deny rule.
     *
     * The boot path uses this to start from a deterministic logging baseline
     * before it applies any board- or scenario-specific file filters.
     *
     * @return Nothing.
     */
    void kernel_debug_clear_file_filters(void);

    /*
     * Add one source file to the explicit allow list.
     *
     * When at least one allow-list entry exists, `KDEBUG` emits only from the
     * listed files until callers clear the list again.
     *
     * @param file_name Basename or full source path to allow.
     * @return `StatusOK` when stored, `StatusAlreadyExists` when already
     * present, or a capacity/validation error.
     */
    Status kernel_debug_add_enabled_file(const char* file_name);

    /*
     * Add one source file to the explicit deny list.
     *
     * Deny-list entries always suppress `KDEBUG` output for the matching file,
     * even when the same zone remains enabled globally.
     *
     * @param file_name Basename or full source path to suppress.
     * @return `StatusOK` when stored, `StatusAlreadyExists` when already
     * present, or a capacity/validation error.
     */
    Status kernel_debug_add_disabled_file(const char* file_name);

    /*
     * Report whether one `KDEBUG` call should emit right now.
     *
     * This combines the active zone mask with the optional source-file allow
     * and deny lists so callers can enable a zone broadly but narrow the live
     * output to a few hot files during investigation.
     *
     * @param zone_mask CE-style debug zone mask supplied by `KDEBUG`.
     * @param path Full source path supplied by `__FILE__`.
     * @return `true` when the message should be emitted.
     */
    bool kernel_debug_should_emit(U32 zone_mask, const char* path);

#ifdef __cplusplus
}
#endif

#ifndef KERNEL_DEBUG_ENABLE
#define KERNEL_DEBUG_ENABLE 1
#endif

#ifndef KERNEL_DEBUG_ZONE_MASK
// #define KERNEL_DEBUG_ZONE_MASK 0xFFFFu
#endif

#define KDBG_COLOR_RESET "\x1b[0m"
#define KDBG_COLOR_DEBUG "\x1b[36m"
#define KDBG_COLOR_RETAIL "\x1b[32m"
#define KDBG_COLOR_ERROR "\x1b[31;1m"

#define KZONE_BIT_SCHEDULE 0u
#define KZONE_BIT_MEMORY 1u
#define KZONE_BIT_OBJECT 2u
#define KZONE_BIT_DEBUGGER 3u
#define KZONE_BIT_SECURITY 4u
#define KZONE_BIT_LOADER 5u
#define KZONE_BIT_VMM 6u
#define KZONE_BIT_LOADER2 7u
#define KZONE_BIT_THREAD 8u
#define KZONE_BIT_MAPFILE 9u
#define KZONE_BIT_PHYSMEM 10u
#define KZONE_BIT_SEH 11u
#define KZONE_BIT_OPENEXE 12u
#define KZONE_BIT_ERROR 13u
#define KZONE_BIT_PAGING 14u
#define KZONE_BIT_ENTRY 15u

#define KZONE_SCHEDULE DEBUGZONE(KZONE_BIT_SCHEDULE)
#define KZONE_MEMORY DEBUGZONE(KZONE_BIT_MEMORY)
#define KZONE_OBJECT DEBUGZONE(KZONE_BIT_OBJECT)
#define KZONE_DEBUGGER DEBUGZONE(KZONE_BIT_DEBUGGER)
#define KZONE_SECURITY DEBUGZONE(KZONE_BIT_SECURITY)
#define KZONE_LOADER DEBUGZONE(KZONE_BIT_LOADER)
#define KZONE_VMM DEBUGZONE(KZONE_BIT_VMM)
#define KZONE_LOADER2 DEBUGZONE(KZONE_BIT_LOADER2)
#define KZONE_THREAD DEBUGZONE(KZONE_BIT_THREAD)
#define KZONE_MAPFILE DEBUGZONE(KZONE_BIT_MAPFILE)
#define KZONE_PHYSMEM DEBUGZONE(KZONE_BIT_PHYSMEM)
#define KZONE_SEH DEBUGZONE(KZONE_BIT_SEH)
#define KZONE_OPENEXE DEBUGZONE(KZONE_BIT_OPENEXE)
#define KZONE_ERROR DEBUGZONE(KZONE_BIT_ERROR)
#define KZONE_PAGING DEBUGZONE(KZONE_BIT_PAGING)
#define KZONE_ENTRY DEBUGZONE(KZONE_BIT_ENTRY)

#if KERNEL_DEBUG_ENABLE
#define KDEBUG(zone, fmt, ...) \
    do { \
        if (kernel_debug_should_emit((U32)(zone), __FILE__)) { \
            DEBUGMSG((zone), (KDBG_COLOR_DEBUG "[DBG] %s:%d: " fmt KDBG_COLOR_RESET, kernel_debug_short_file(__FILE__), __LINE__, ##__VA_ARGS__)); \
        } \
    } while (0)

#else
#define KDEBUG(zone, fmt, ...) ((void)(zone))
#endif

#define KRETAIL(fmt, ...) \
    RETAILMSG(1, (KDBG_COLOR_RETAIL "[LOG] %s:%d: " fmt KDBG_COLOR_RESET, kernel_debug_short_file(__FILE__), __LINE__, ##__VA_ARGS__))

#define KERROR(fmt, ...) \
    ERRORMSG(1, (KDBG_COLOR_ERROR "[ERR] %s:%d: " fmt KDBG_COLOR_RESET, kernel_debug_short_file(__FILE__), __LINE__, ##__VA_ARGS__))

#endif