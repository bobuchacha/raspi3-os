/*
 * common_debugger.h
 *
 * Purpose:
 *   Header-only CE-style debug facility that can be shared by kernel code and
 *   user applications. The interface is centered around DBGPARAM, DEBUGZONE,
 *   DEBUGMSG, and RETAILMSG, with a configurable sink so the same header can
 *   target a serial console, kernel trace port, stderr, or any custom logger.
 */
#ifndef MY_COMMON_DEBUGGER_INCLUDE_COMMON_DEBUGGER_H
#define MY_COMMON_DEBUGGER_INCLUDE_COMMON_DEBUGGER_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(MCDBG_NO_STDIO)
#include <stdio.h>
#endif

#if defined(MCDBG_USE_WCHAR)
#include <wchar.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MCDBG_USE_WCHAR)
    typedef wchar_t MCDBG_CHAR;
    typedef void (*MCDBG_SINK_FN)(void* context, const wchar_t* text);
#define MCDBG_TEXT(x) L##x
#define MCDBG_VSNPRINTF vswprintf
#else
    typedef char MCDBG_CHAR;
    typedef void (*MCDBG_SINK_FN)(void* context, const char* text);
#define MCDBG_TEXT(x) x
#define MCDBG_VSNPRINTF vsnprintf
#endif

    typedef uint32_t(*MCDBG_ID_FN)(void);
    typedef uint64_t(*MCDBG_TIME_FN)(void);

    typedef enum MCDBG_LEVEL_TAG {
        MCDBG_LEVEL_DEBUG = 0,
        MCDBG_LEVEL_RETAIL = 1,
        MCDBG_LEVEL_ERROR = 2
    } MCDBG_LEVEL;

    enum {
        MCDBG_ZONE_COUNT = 16
    };

    enum {
        MCDBG_FLAG_LEVEL = 0x00000001u,
        MCDBG_FLAG_MODULE = 0x00000002u,
        MCDBG_FLAG_PROCESS = 0x00000004u,
        MCDBG_FLAG_THREAD = 0x00000008u,
        MCDBG_FLAG_TIMESTAMP = 0x00000010u,
        MCDBG_FLAG_AUTO_NEWLINE = 0x00000020u
    };

#ifndef MCDBG_DEFAULT_FLAGS
#define MCDBG_DEFAULT_FLAGS (MCDBG_FLAG_LEVEL | MCDBG_FLAG_MODULE | MCDBG_FLAG_AUTO_NEWLINE)
#endif

#ifndef MCDBG_BUFFER_CHARS
#define MCDBG_BUFFER_CHARS 1024u
#endif

#ifndef MCDBG_BREAK
#define MCDBG_BREAK() ((void)0)
#endif

    typedef struct _DBGPARAM {
        const MCDBG_CHAR* lpszName;
        const MCDBG_CHAR* rglpszZones[MCDBG_ZONE_COUNT];
        uint32_t ulZoneMask;
        uint32_t ulFlags;
        MCDBG_SINK_FN pfnSink;
        void* pvSinkContext;
        MCDBG_ID_FN pfnGetProcessId;
        MCDBG_ID_FN pfnGetThreadId;
        MCDBG_TIME_FN pfnGetTimestamp;
    } DBGPARAM, * LPDBGPARAM;

#define MCDBG_ZONE_MASK(bit) (1u << (unsigned)(bit))
#define MCDBG_ZONE_BIT_ERROR 13u
#define MCDBG_ZONE_BIT_ENTRY 15u

#define MCDBG_EMPTY_ZONES \
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, \
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL

#define MCDBG_ZONE_NAMES_GENERIC \
    MCDBG_TEXT("Init"), MCDBG_TEXT("Info"), MCDBG_TEXT("Warn"), MCDBG_TEXT("IO"), \
    MCDBG_TEXT("Memory"), MCDBG_TEXT("Sync"), MCDBG_TEXT("IRQ"), MCDBG_TEXT("Verbose"), \
    MCDBG_TEXT("Debug"), MCDBG_TEXT("State"), MCDBG_TEXT("Sched"), MCDBG_TEXT("Assert"), \
    MCDBG_TEXT("Open"), MCDBG_TEXT("Error"), MCDBG_TEXT("Paging"), MCDBG_TEXT("Entry")

#define MCDBG_ZONE_NAMES_CE_KERNEL \
    MCDBG_TEXT("Schedule"), MCDBG_TEXT("Memory"), MCDBG_TEXT("ObjDisp"), MCDBG_TEXT("Debugger"), \
    MCDBG_TEXT("Security"), MCDBG_TEXT("Loader"), MCDBG_TEXT("VirtMem"), MCDBG_TEXT("Loader2"), \
    MCDBG_TEXT("ThreadIDs"), MCDBG_TEXT("MapFile"), MCDBG_TEXT("PhysMem"), MCDBG_TEXT("SEH"), \
    MCDBG_TEXT("OpenExe"), MCDBG_TEXT("Error"), MCDBG_TEXT("Paging"), MCDBG_TEXT("APIEntry")

#define MCDBG_PARAMS_INIT(module_name, zone_mask, ...) \
    { \
        module_name, \
        { __VA_ARGS__ }, \
        zone_mask, \
        MCDBG_DEFAULT_FLAGS, \
        NULL, \
        NULL, \
        NULL, \
        NULL, \
        NULL \
    }

#define MCDBG_PARAMS_INIT_EX(module_name, zone_mask, flags, sink, sink_ctx, pid_fn, tid_fn, time_fn, ...) \
    { \
        module_name, \
        { __VA_ARGS__ }, \
        zone_mask, \
        flags, \
        sink, \
        sink_ctx, \
        pid_fn, \
        tid_fn, \
        time_fn \
    }

    static inline size_t mcdbg_strlen(const MCDBG_CHAR* text) {
        size_t length = 0;

        if (!text) {
            return 0;
        }
        while (text[length]) {
            ++length;
        }
        return length;
    }

    static inline void mcdbg_append_char(MCDBG_CHAR* buffer, size_t capacity, size_t* offset, MCDBG_CHAR ch) {
        if (!buffer || !capacity || !offset) {
            return;
        }
        if (*offset + 1u >= capacity) {
            buffer[capacity - 1u] = 0;
            return;
        }

        buffer[*offset] = ch;
        *offset += 1u;
        buffer[*offset] = 0;
    }

    static inline void mcdbg_append_text(MCDBG_CHAR* buffer, size_t capacity, size_t* offset, const MCDBG_CHAR* text) {
        if (!buffer || !capacity || !offset || !text) {
            return;
        }
        while (*text && (*offset + 1u < capacity)) {
            buffer[*offset] = *text;
            ++(*offset);
            ++text;
        }
        buffer[(*offset < capacity) ? *offset : (capacity - 1u)] = 0;
    }

    static inline void mcdbg_append_ascii(MCDBG_CHAR* buffer, size_t capacity, size_t* offset, const char* text) {
        if (!buffer || !capacity || !offset || !text) {
            return;
        }
        while (*text && (*offset + 1u < capacity)) {
            buffer[*offset] = (MCDBG_CHAR)(unsigned char)(*text);
            ++(*offset);
            ++text;
        }
        buffer[(*offset < capacity) ? *offset : (capacity - 1u)] = 0;
    }

    static inline void mcdbg_append_u64(MCDBG_CHAR* buffer, size_t capacity, size_t* offset, uint64_t value) {
        MCDBG_CHAR scratch[32];
        size_t used = 0;

        if (!buffer || !capacity || !offset) {
            return;
        }

        do {
            scratch[used++] = (MCDBG_CHAR)(MCDBG_TEXT('0') + (MCDBG_CHAR)(value % 10u));
            value /= 10u;
        } while (value && (used < (sizeof(scratch) / sizeof(scratch[0]))));

        while (used) {
            mcdbg_append_char(buffer, capacity, offset, scratch[used - 1u]);
            --used;
        }
    }

    static inline int mcdbg_has_trailing_newline(const MCDBG_CHAR* text) {
        size_t length = mcdbg_strlen(text);

        if (!length) {
            return 0;
        }
        return (text[length - 1u] == MCDBG_TEXT('\n')) || (text[length - 1u] == MCDBG_TEXT('\r'));
    }

    static inline const MCDBG_CHAR* mcdbg_level_name(MCDBG_LEVEL level) {
        switch (level) {
        case MCDBG_LEVEL_DEBUG:
            return MCDBG_TEXT("DEBUG");
        case MCDBG_LEVEL_RETAIL:
            return MCDBG_TEXT("RETAIL");
        case MCDBG_LEVEL_ERROR:
        default:
            return MCDBG_TEXT("ERROR");
        }
    }

    static inline void mcdbg_default_sink(void* context, const MCDBG_CHAR* text) {
        (void)context;

        if (!text) {
            return;
        }

#if defined(MCDBG_USE_WCHAR)
#if defined(MCDBG_PLATFORM_WRITEW)
        MCDBG_PLATFORM_WRITEW(text);
#elif !defined(MCDBG_NO_STDIO)
        fputws(text, stderr);
        fflush(stderr);
#else
        (void)text;
#endif
#else
#if defined(MCDBG_PLATFORM_WRITEA)
        MCDBG_PLATFORM_WRITEA(text);
#elif defined(MCDBG_PLATFORM_WRITE)
        MCDBG_PLATFORM_WRITE(text);
#elif !defined(MCDBG_NO_STDIO)
        fputs(text, stderr);
        fflush(stderr);
#else
        (void)text;
#endif
#endif
    }

    static inline void mcdbg_dispatch(const DBGPARAM* params, const MCDBG_CHAR* text) {
        MCDBG_SINK_FN sink = NULL;
        void* context = NULL;

        if (!text) {
            return;
        }

        if (params) {
            sink = params->pfnSink;
            context = params->pvSinkContext;
        }
        if (!sink) {
            sink = mcdbg_default_sink;
        }
        sink(context, text);
    }

    static inline void mcdbg_build_prefix(const DBGPARAM* params, MCDBG_LEVEL level, MCDBG_CHAR* buffer, size_t capacity, size_t* offset) {
        if (!buffer || !capacity || !offset) {
            return;
        }

        buffer[0] = 0;
        *offset = 0;

        if (!params) {
            return;
        }

        if (params->ulFlags & MCDBG_FLAG_LEVEL) {
            mcdbg_append_char(buffer, capacity, offset, MCDBG_TEXT('['));
            mcdbg_append_text(buffer, capacity, offset, mcdbg_level_name(level));
            mcdbg_append_char(buffer, capacity, offset, MCDBG_TEXT(']'));
            mcdbg_append_char(buffer, capacity, offset, MCDBG_TEXT(' '));
        }

        if ((params->ulFlags & MCDBG_FLAG_MODULE) && params->lpszName) {
            mcdbg_append_text(buffer, capacity, offset, params->lpszName);
            mcdbg_append_char(buffer, capacity, offset, MCDBG_TEXT(':'));
            mcdbg_append_char(buffer, capacity, offset, MCDBG_TEXT(' '));
        }

        if ((params->ulFlags & MCDBG_FLAG_TIMESTAMP) && params->pfnGetTimestamp) {
            mcdbg_append_ascii(buffer, capacity, offset, "TS:");
            mcdbg_append_u64(buffer, capacity, offset, params->pfnGetTimestamp());
            mcdbg_append_char(buffer, capacity, offset, MCDBG_TEXT(' '));
        }

        if ((params->ulFlags & MCDBG_FLAG_PROCESS) && params->pfnGetProcessId) {
            mcdbg_append_ascii(buffer, capacity, offset, "PID:");
            mcdbg_append_u64(buffer, capacity, offset, params->pfnGetProcessId());
            mcdbg_append_char(buffer, capacity, offset, MCDBG_TEXT(' '));
        }

        if ((params->ulFlags & MCDBG_FLAG_THREAD) && params->pfnGetThreadId) {
            mcdbg_append_ascii(buffer, capacity, offset, "TID:");
            mcdbg_append_u64(buffer, capacity, offset, params->pfnGetThreadId());
            mcdbg_append_char(buffer, capacity, offset, MCDBG_TEXT(' '));
        }
    }

    static inline void mcdbg_emitvf(const DBGPARAM* params, MCDBG_LEVEL level, const MCDBG_CHAR* format, va_list args) {
        MCDBG_CHAR prefix[256];
        MCDBG_CHAR body[MCDBG_BUFFER_CHARS];
        MCDBG_CHAR message[MCDBG_BUFFER_CHARS + 256];
        size_t prefix_len = 0;
        size_t message_len = 0;

        if (!format) {
            return;
        }

        body[0] = 0;
        (void)MCDBG_VSNPRINTF(body, sizeof(body) / sizeof(body[0]), format, args);
        body[(sizeof(body) / sizeof(body[0])) - 1u] = 0;

        mcdbg_build_prefix(params, level, prefix, sizeof(prefix) / sizeof(prefix[0]), &prefix_len);
        message[0] = 0;
        mcdbg_append_text(message, sizeof(message) / sizeof(message[0]), &message_len, prefix);
        mcdbg_append_text(message, sizeof(message) / sizeof(message[0]), &message_len, body);

        if ((!params || (params->ulFlags & MCDBG_FLAG_AUTO_NEWLINE)) && !mcdbg_has_trailing_newline(message)) {
            mcdbg_append_char(message, sizeof(message) / sizeof(message[0]), &message_len, MCDBG_TEXT('\n'));
        }

        mcdbg_dispatch(params, message);
    }

    static inline void mcdbg_emitf(const DBGPARAM* params, MCDBG_LEVEL level, const MCDBG_CHAR* format, ...) {
        va_list args;

        va_start(args, format);
        mcdbg_emitvf(params, level, format, args);
        va_end(args);
    }

    static inline void mcdbg_emit_check_failure(const DBGPARAM* params, const char* expression, const char* file, int line) {
        MCDBG_CHAR message[MCDBG_BUFFER_CHARS];
        size_t offset = 0;

        message[0] = 0;
        mcdbg_build_prefix(params, MCDBG_LEVEL_ERROR, message, sizeof(message) / sizeof(message[0]), &offset);
        mcdbg_append_ascii(message, sizeof(message) / sizeof(message[0]), &offset, "DEBUGCHK failed: ");
        mcdbg_append_ascii(message, sizeof(message) / sizeof(message[0]), &offset, expression ? expression : "<expr>");
        mcdbg_append_ascii(message, sizeof(message) / sizeof(message[0]), &offset, " @ ");
        mcdbg_append_ascii(message, sizeof(message) / sizeof(message[0]), &offset, file ? file : "<file>");
        mcdbg_append_char(message, sizeof(message) / sizeof(message[0]), &offset, MCDBG_TEXT(':'));
        mcdbg_append_u64(message, sizeof(message) / sizeof(message[0]), &offset, (uint64_t)((line < 0) ? 0 : line));

        if (!mcdbg_has_trailing_newline(message)) {
            mcdbg_append_char(message, sizeof(message) / sizeof(message[0]), &offset, MCDBG_TEXT('\n'));
        }

        mcdbg_dispatch(params, message);
    }

    static inline void mcdbg_set_sink(DBGPARAM* params, MCDBG_SINK_FN sink, void* context) {
        if (!params) {
            return;
        }
        params->pfnSink = sink;
        params->pvSinkContext = context;
    }

    static inline void mcdbg_set_zone_mask(DBGPARAM* params, uint32_t zone_mask) {
        if (!params) {
            return;
        }
        params->ulZoneMask = zone_mask;
    }

    static inline void mcdbg_enable_zone(DBGPARAM* params, unsigned zone) {
        if (!params || (zone >= MCDBG_ZONE_COUNT)) {
            return;
        }
        params->ulZoneMask |= MCDBG_ZONE_MASK(zone);
    }

    static inline void mcdbg_disable_zone(DBGPARAM* params, unsigned zone) {
        if (!params || (zone >= MCDBG_ZONE_COUNT)) {
            return;
        }
        params->ulZoneMask &= ~MCDBG_ZONE_MASK(zone);
    }

    static inline int mcdbg_zone_enabled(const DBGPARAM* params, unsigned zone) {
        if (!params || (zone >= MCDBG_ZONE_COUNT)) {
            return 0;
        }
        return (params->ulZoneMask & MCDBG_ZONE_MASK(zone)) != 0u;
    }

    static inline const MCDBG_CHAR* mcdbg_zone_name(const DBGPARAM* params, unsigned zone) {
        if (!params || (zone >= MCDBG_ZONE_COUNT)) {
            return NULL;
        }
        return params->rglpszZones[zone];
    }

#define DEBUGZONE(bit) (mcdbg_zone_enabled(&dpCurSettings, (unsigned)(bit)))

#ifndef ZONE_ERROR
#define ZONE_ERROR DEBUGZONE(MCDBG_ZONE_BIT_ERROR)
#endif

#ifndef ZONE_ENTRY
#define ZONE_ENTRY DEBUGZONE(MCDBG_ZONE_BIT_ENTRY)
#endif

#define DEBUGREGISTER(module_handle) ((void)(module_handle))
#define REGISTERDBGZONES(module_handle) DEBUGREGISTER(module_handle)

#define MCDBG_DEBUG_DISPATCH(...) mcdbg_emitf(&dpCurSettings, MCDBG_LEVEL_DEBUG, __VA_ARGS__)
#define MCDBG_RETAIL_DISPATCH(...) mcdbg_emitf(&dpCurSettings, MCDBG_LEVEL_RETAIL, __VA_ARGS__)
#define MCDBG_ERROR_DISPATCH(...) mcdbg_emitf(&dpCurSettings, MCDBG_LEVEL_ERROR, __VA_ARGS__)

#if defined(MCDBG_SHIP_BUILD)
#define DEBUGMSG(cond, printf_exp) ((void)(cond), 0)
#define DEBUGCHK(exp) ((void)(exp))
#else
#define DEBUGMSG(cond, printf_exp) ((cond) ? (MCDBG_DEBUG_DISPATCH printf_exp, 1) : 0)
#define DEBUGCHK(exp) do { if (!(exp)) { mcdbg_emit_check_failure(&dpCurSettings, #exp, __FILE__, __LINE__); MCDBG_BREAK(); } } while (0)
#endif

#define RETAILMSG(cond, printf_exp) ((cond) ? (MCDBG_RETAIL_DISPATCH printf_exp, 1) : 0)
#define ERRORMSG(cond, printf_exp) ((cond) ? (MCDBG_ERROR_DISPATCH printf_exp, 1) : 0)

#ifdef __cplusplus
}
#endif

#endif /* MY_COMMON_DEBUGGER_INCLUDE_COMMON_DEBUGGER_H */
