/*
 * gwe_module.h
 *
 * Module lifetime and desktop-wide query helpers.
 */
#ifndef GWE_MODULE_H
#define GWE_MODULE_H

#include "gwe_api.h"

#ifdef __cplusplus
extern "C" {
#endif

GWE_RESULT gwe_init(PCGWE_KERNELAPI kernelApi, const GWE_INITINFO *initInfo, PGWE_CONTEXT *outContext);
GWE_RESULT gwe_shutdown(PGWE_CONTEXT context);

GWE_RESULT gwe_register_thread(PGWE_CONTEXT context, uint32_t threadId);
GWE_RESULT gwe_unregister_thread(PGWE_CONTEXT context, uint32_t threadId);
GWE_RESULT gwe_query_thread_queue(PGWE_CONTEXT context, uint32_t threadId, GWE_THREADQUEUEINFO *outInfo);

GWE_RESULT gwe_query_desktop(PGWE_CONTEXT context, GWE_DESKTOPINFO *outInfo);

#ifdef __cplusplus
}
#endif

#endif /* GWE_MODULE_H */
