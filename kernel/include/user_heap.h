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
     * Resolve one recoverable heap page fault by mapping the missing page.
     *
     * The process heap is reserved as a sparse EL0 window. When userspace first
     * touches one heap page, the lower-EL fault path calls this helper to
     * validate the address against the process reservation and populate one
     * physical page on demand.
     *
     * @param process Faulting process.
     * @param address Fault address captured in FAR_EL1.
     * @param esr Raw ESR_EL1 value for diagnostics and future policy.
     * @return StatusOK when the page was mapped or already present.
     */
    static Status handle_page_fault(Process* process, VirtAddr address, U64 esr);

    /**
     * Ensure one user buffer range is backed before an EL1 copy touches it.
     *
     * Lower-EL fault recovery only helps when EL0 itself dereferences the heap.
     * Kernel services that copy directly into a user heap buffer must therefore
     * pre-populate any overlapping heap pages before issuing the copy.
     *
     * @param process Owning process.
     * @param address User virtual start address.
     * @param size Byte count that EL1 is about to touch.
     * @return StatusOK when every overlapping heap page is present.
     */
    static Status ensure_range_mapped(Process* process, VirtAddr address, Size size);

    /**
     * Report the current committed user-heap footprint for one process.
     *
     * @param process Owning process.
     * @return Bytes of heap-backed pages currently mapped for that process.
     */
    static Size mapped_heap_bytes(Process* process);

    /**
     * Release every raw heap block and mapped arena owned by one process.
     *
     * @param process Process being torn down.
     * @return StatusOK after the process heap is fully released.
     */
    static Status release_process(Process* process);
};

#endif // KERNEL_INCLUDE_USER_HEAP_H