/*
 * gwe_input.h
 *
 * Public input routing API.
 */
#ifndef GWE_INPUT_H
#define GWE_INPUT_H

#include "gwe_api.h"

#ifdef __cplusplus
extern "C" {
#endif

GWE_RESULT gwe_dispatch_pointer(PGWE_CONTEXT context,
                                GWE_POINTER_KIND kind,
                                int32_t x,
                                int32_t y,
                                uint32_t buttons);

GWE_RESULT gwe_dispatch_key(PGWE_CONTEXT context,
                            bool keyDown,
                            uint32_t scanCode,
                            uint32_t keyCode);

#ifdef __cplusplus
}
#endif

#endif /* GWE_INPUT_H */
