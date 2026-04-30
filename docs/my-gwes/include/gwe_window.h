/*
 * gwe_window.h
 *
 * Public window-management API.
 */
#ifndef GWE_WINDOW_H
#define GWE_WINDOW_H

#include "gwe_api.h"

#ifdef __cplusplus
extern "C" {
#endif

GWE_RESULT gwe_create_window(PGWE_CONTEXT context, const GWE_WINDOW_CREATEINFO *createInfo, PGWE_WINDOW *outWindow);
GWE_RESULT gwe_destroy_window(PGWE_CONTEXT context, PGWE_WINDOW window);
GWE_RESULT gwe_query_window(PGWE_WINDOW window, GWE_WINDOWINFO *outInfo);

GWE_RESULT gwe_set_capture(PGWE_CONTEXT context, PGWE_WINDOW window);
GWE_RESULT gwe_release_capture(PGWE_CONTEXT context, PGWE_WINDOW window);

#ifdef __cplusplus
}
#endif

#endif /* GWE_WINDOW_H */
