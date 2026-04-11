/*
 * sp_wait.h
 *
 * Public wait/synchronization placeholders for the scaffolded module.
 */
#ifndef SP_WAIT_H
#define SP_WAIT_H

#include "sp_scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SpWaitRequestStruct {
    SP_WAIT_KIND kind;
    void        *object;
    uint64_t     timeoutTicks;
} SP_WAITREQUEST;

/* Placeholder for future wait subsystem initialization. */
SP_RESULT sp_wait_feature_status(void);

#ifdef __cplusplus
}
#endif

#endif /* SP_WAIT_H */
