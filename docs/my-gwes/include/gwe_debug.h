/*
 * gwe_debug.h
 *
 * Human-readable dump helpers for the GWES scaffold.
 */
#ifndef GWE_DEBUG_H
#define GWE_DEBUG_H

#include <stdio.h>

#include "gwe_api.h"

#ifdef __cplusplus
extern "C" {
#endif

void gwe_dump_state(PCGWE_CONTEXT context, FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* GWE_DEBUG_H */
