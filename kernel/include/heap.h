#ifndef KERNEL_INCLUDE_HEAP_H
#define KERNEL_INCLUDE_HEAP_H

#include "types.h"

typedef struct HeapStats {
    Size total_bytes;
    Size used_bytes;
    Size free_bytes;
} HeapStats;

class Heap final {
public:
    /**
     * Initialize the kernel heap from the board-provided bootstrap arena.
     *
     * @return StatusOK when the heap can serve allocations.
     */
    static Status init(void);

    /**
     * Allocate one aligned heap block.
     *
     * @param size Requested payload size in bytes.
     * @param alignment Required payload alignment in bytes.
     * @return Heap pointer on success, or NULL when allocation fails.
     */
    static void* alloc(Size size, Size alignment);

    /**
     * Resize one existing heap allocation.
     *
     * Passing NULL behaves like a fresh allocation and passing `size == 0U`
     * releases the block and returns NULL.
     *
     * @param pointer Existing allocation, or NULL.
     * @param size New payload size in bytes.
     * @return Resized allocation on success, or NULL when growth fails.
     */
    static void* realloc(void* pointer, Size size);

    /**
     * Release one heap allocation previously returned by `alloc` or `realloc`.
     *
     * @param pointer Allocation to release.
     * @return Nothing.
     */
    static void free(void* pointer);

    /**
     * Snapshot the current heap usage counters.
     *
     * @param stats_out Destination statistics structure.
     * @return Nothing.
     */
    static void get_stats(HeapStats* stats_out);

    /**
     * Validate the heap segment chain without mutating allocator state.
     *
     * The current input/GWES bring-up is tripping a latent heap corruption,
     * and the first failing allocation only tells us where the damage was
     * detected. This helper lets selected callers verify the segment walk at
     * stable boundaries so the first bad subsystem can be identified.
     *
     * @param reason Short diagnostic label printed when validation fails.
     * @return True when the segment list remains internally consistent.
     */
    static bool debug_validate(const char* reason);
};

#endif // KERNEL_INCLUDE_HEAP_H
