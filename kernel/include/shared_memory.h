#ifndef KERNEL_INCLUDE_SHARED_MEMORY_H
#define KERNEL_INCLUDE_SHARED_MEMORY_H

#include "types.h"

#if !defined(__cplusplus)
#error "shared_memory.h requires C++"
#endif

struct Process;

/*
 * The shared-memory manager owns named EL0 mappings backed by dedicated
 * physical pages so multiple user processes can observe the same writable
 * bytes without depending on kernel heap allocations.
 */
class SharedMemoryManager final {
public:
    /*
     * Initialize the global shared-memory tables.
     *
     * @return StatusOK after the subsystem is ready.
     */
    static Status init(void);

    /*
     * Map one named shared-memory object into a user process.
     *
     * When the object does not exist yet, a non-zero `requested_size` creates
     * it. Later callers may reopen the same name with `requested_size == 0`.
     *
     * @param process Destination process that should receive the mapping.
     * @param name Stable object name used to find or create the region.
     * @param requested_size Creation size in bytes, or zero to open only.
     * @param address_out Receives the user virtual address on success.
     * @return StatusOK on success, or a propagated allocation/mapping error.
     */
    static Status acquire(Process* process, const char* name, Size requested_size, VirtAddr* address_out);

    /*
     * Drop every shared-memory mapping owned by one exiting process.
     *
     * @param process Process whose mappings should be released.
     * @return StatusOK after the scan completes.
     */
    static Status release_process_mappings(Process* process);
};

#endif // KERNEL_INCLUDE_SHARED_MEMORY_H