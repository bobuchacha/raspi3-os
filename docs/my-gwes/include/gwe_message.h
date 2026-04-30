/*
 * gwe_message.h
 *
 * Public GUI message queue and dispatch API.
 */
#ifndef GWE_MESSAGE_H
#define GWE_MESSAGE_H

#include "gwe_api.h"

#ifdef __cplusplus
extern "C" {
#endif

GWE_RESULT gwe_post_message(PGWE_CONTEXT context,
                            PGWE_WINDOW window,
                            GWE_MESSAGE_TYPE type,
                            uint64_t wparam,
                            int64_t lparam);

GWE_RESULT gwe_post_quit(PGWE_CONTEXT context, uint32_t threadId, int64_t exitCode);

GWE_RESULT gwe_send_message(PGWE_CONTEXT context,
                            uint32_t callerTid,
                            PGWE_WINDOW window,
                            GWE_MESSAGE_TYPE type,
                            uint64_t wparam,
                            int64_t lparam,
                            int64_t *outResult);

GWE_RESULT gwe_get_message(PGWE_CONTEXT context, uint32_t threadId, PGWE_MESSAGE outMessage);
GWE_RESULT gwe_dispatch_message(PGWE_CONTEXT context, PCGWE_MESSAGE message, int64_t *outResult);

#ifdef __cplusplus
}
#endif

#endif /* GWE_MESSAGE_H */
