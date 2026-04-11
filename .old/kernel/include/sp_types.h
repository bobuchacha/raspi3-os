/*
 * sp_types.h
 *
 * Shared base types and enums for the scheduler/process-manager module.
 */
#ifndef SP_TYPES_H
#define SP_TYPES_H

#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#else
#include "ros.h"

/*
 * The freestanding kernel already ships its own core scalar aliases in
 * `ros.h`. Reuse them here so the schedproc module can compile without the
 * hosted C library headers that would otherwise redefine `NULL`, `true`, and
 * `false`.
 */
typedef Bool  bool;
typedef UByte uint8_t;
typedef UInt  uint32_t;
typedef ULong uint64_t;
typedef ULong size_t;

#ifndef UINT64_MAX
#define UINT64_MAX ((uint64_t)~(uint64_t)0)
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SP_NAME_MAX 32u
#define SP_MAX_PRIORITY 31u
#define SP_READY_LEVELS (SP_MAX_PRIORITY + 1u)

typedef enum SpResultEnum {
    SP_OK = 0,
    SP_E_INVALID_ARG,
    SP_E_STATE,
    SP_E_OOM,
    SP_E_EMPTY,
    SP_E_NOT_IMPLEMENTED
} SP_RESULT;

typedef enum SpProcessStateEnum {
    SP_PROCESS_STARTING = 0,
    SP_PROCESS_NORMAL,
    SP_PROCESS_EXITING,
    SP_PROCESS_DEAD
} SP_PROCESS_STATE;

typedef enum SpThreadStateEnum {
    SP_THREAD_CREATED = 0,
    SP_THREAD_SUSPENDED,
    SP_THREAD_RUNNABLE,
    SP_THREAD_RUNNING,
    SP_THREAD_BLOCKED,
    SP_THREAD_SLEEPING,
    SP_THREAD_DYING,
    SP_THREAD_DEAD
} SP_THREAD_STATE;

typedef enum SpWaitKindEnum {
    SP_WAIT_EVENT = 0,
    SP_WAIT_MUTEX,
    SP_WAIT_SEMAPHORE,
    SP_WAIT_CRITICAL_SECTION,
    SP_WAIT_THREAD,
    SP_WAIT_CUSTOM
} SP_WAIT_KIND;

#ifdef __cplusplus
}
#endif

#endif /* SP_TYPES_H */
