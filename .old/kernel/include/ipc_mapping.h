/*
 * ipc_mapping.h
 *
 * File purpose:
 *   Public API for named shared-memory mappings.
 *
 * Design notes:
 *   The scaffold uses heap-backed storage to model the future kernel mapping
 *   object. The API shape is intentionally close to CreateFileMapping /
 *   MapViewOfFile style semantics so later VM-backed integration can preserve
 *   the caller-facing contract.
 */
#ifndef IPC_MAPPING_H
#define IPC_MAPPING_H

#include "ipc_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create a named shared-memory object with a fixed byte size. */
IPC_RESULT ipc_create_mapping(PIPC_CONTEXT context, const char *name, size_t size, PIPC_MAPPING *outMapping);
/* Open an already-existing named mapping and acquire another reference. */
IPC_RESULT ipc_open_mapping(PIPC_CONTEXT context, const char *name, PIPC_MAPPING *outMapping);
/* Release one mapping reference and destroy storage when the count reaches zero. */
IPC_RESULT ipc_close_mapping(PIPC_CONTEXT context, PIPC_MAPPING mapping);
/* Return the base address and size of the mapping's shared storage. */
IPC_RESULT ipc_map_view(PIPC_MAPPING mapping, void **outView, size_t *outSize);
/* Release a previously returned view reference. */
IPC_RESULT ipc_unmap_view(PIPC_MAPPING mapping, void *view);
/* Copy caller data into the mapping and advance its write sequence. */
IPC_RESULT ipc_write_mapping(PIPC_MAPPING mapping, size_t offset, const void *buffer, size_t length);
/* Copy bytes out of the mapping into a caller-owned buffer. */
IPC_RESULT ipc_read_mapping(PIPC_MAPPING mapping, size_t offset, void *buffer, size_t length);
/* Report the current mapping state for diagnostics or tests. */
IPC_RESULT ipc_query_mapping(PIPC_MAPPING mapping, IPC_MAPPINGINFO *outInfo);

#ifdef __cplusplus
}
#endif

#endif /* IPC_MAPPING_H */
