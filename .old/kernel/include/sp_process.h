/*
 * sp_process.h
 *
 * Public process-manager API.
 */
#ifndef SP_PROCESS_H
#define SP_PROCESS_H

#include "sp_api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct SpContextStruct;
struct SpProcessStruct;
struct SpThreadStruct;

typedef struct SpContextStruct SP_CONTEXT;
typedef struct SpProcessStruct SP_PROCESS;
typedef struct SpThreadStruct  SP_THREAD;

typedef SP_CONTEXT *PSP_CONTEXT;
typedef SP_PROCESS *PSP_PROCESS;
typedef SP_THREAD  *PSP_THREAD;

typedef struct SpProcessInfoStruct {
    uint32_t         processId;
    char             name[SP_NAME_MAX];
    SP_PROCESS_STATE state;
    uint32_t         threadCount;
} SP_PROCESSINFO;

/* Create and initialize one scheduler/process-manager context. */
SP_RESULT sp_init(PCSP_KERNELAPI api, PSP_CONTEXT *outContext);

/* Destroy one context and all owned processes and threads. */
SP_RESULT sp_shutdown(PSP_CONTEXT context);

/* Create one process object in STARTING state. */
SP_RESULT sp_create_process(PSP_CONTEXT context, const char *name, PSP_PROCESS *outProcess);

/* Begin process exit and mark remaining live threads as dying. */
SP_RESULT sp_begin_process_exit(PSP_CONTEXT context, PSP_PROCESS process);

/* Destroy one process object after all owned threads are gone. */
SP_RESULT sp_destroy_process(PSP_CONTEXT context, PSP_PROCESS process);

/* Read one process snapshot into caller-owned memory. */
SP_RESULT sp_query_process(PSP_PROCESS process, SP_PROCESSINFO *outInfo);

#ifdef __cplusplus
}
#endif

#endif /* SP_PROCESS_H */
