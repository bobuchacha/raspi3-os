#ifndef KERNEL_INCLUDE_USER_HEAP_H
#define KERNEL_INCLUDE_USER_HEAP_H

#include "types.h"

#if !defined(__cplusplus)
#error "user_heap.h requires C++"
#endif

struct Process;

/**
 * Kernel-owned per-process heap allocator for raw EL0-visible memory blocks.
 *
 * User-mode `malloc` layers its own size headers on top of this raw allocator.
 * The kernel keeps metadata out of the mapped user pages so heap bookkeeping
 * cannot be corrupted by normal user writes.
 */
class UserHeap final {
public:
    /**
     * Allocate one raw writable user block inside the target process.
     *
     * @param process Owning process whose address space receives the block.
     * @param size Requested raw byte count.
     * @param address_out Receives the user virtual address on success.
     * @return StatusOK on success, or an allocation/mapping failure.
     */
    static Status alloc_raw(Process* process, Size size, VirtAddr* address_out);

    /**
     * Free one raw user block previously returned by `alloc_raw`.
     *
     * @param process Owning process.
     * @param address User virtual address originally returned by `alloc_raw`.
     * @return StatusOK on success, or StatusNotFound when no live block matches.
     */
    static Status free_raw(Process* process, VirtAddr address);

    /**
     * Release every raw heap block and mapped arena owned by one process.
     *
     * @param process Process being torn down.
     * @return StatusOK after the process heap is fully released.
     */
    static Status release_process(Process* process);
};

#endif // KERNEL_INCLUDE_USER_HEAP_H