#include "ros.h"
#include "memory.h"
#include "device.h"
#include "log.h"
#include "utils.h"

#define HEAP_ALIGNMENT 16UL

static HEAP_SEGMENT* heap_segment_list_head;

/*
 * Return the number of caller-usable bytes held by one heap segment.
 */
static unsigned long mem_heap_segment_bytes(const HEAP_SEGMENT* segment) {
    if (!segment || segment->segment_size <= sizeof(heap_segment_t)) {
        return 0;
    }

    return (unsigned long)(segment->segment_size - sizeof(heap_segment_t));
}

/*
 * Round an allocation request up to the allocator alignment and include the
 * segment header stored in front of the payload.
 */
static unsigned long mem_heap_request_bytes(unsigned long bytes) {
    unsigned long request = bytes + sizeof(heap_segment_t);
    unsigned long mask = HEAP_ALIGNMENT - 1;

    return (request + mask) & ~mask;
}

/*
 * Split one free segment into an allocated front portion and a trailing free
 * segment when the remainder is large enough to be useful.
 */
static void mem_heap_split_segment(heap_segment_t* segment, unsigned long request_bytes) {
    heap_segment_t* next_segment;
    heap_segment_t* remainder;
    unsigned long remainder_size;

    if (!segment || segment->segment_size <= request_bytes) {
        return;
    }

    remainder_size = segment->segment_size - request_bytes;
    if (remainder_size <= (2 * sizeof(heap_segment_t))) {
        return;
    }

    remainder = (heap_segment_t*)((UByte*)segment + request_bytes);
    memzero((Address)remainder, sizeof(*remainder));             // initialize the new free segment header

    next_segment = segment->next;
    remainder->next = next_segment;
    remainder->prev = segment;
    remainder->is_allocated = 0;
    remainder->segment_size = remainder_size;

    segment->next = remainder;
    segment->segment_size = request_bytes;

    if (next_segment != NULL) {
        next_segment->prev = remainder;
    }
}

/*
 * Merge a free segment with any adjacent free neighbours and return the head of
 * the merged extent.
 */
static heap_segment_t* mem_heap_coalesce(heap_segment_t* segment) {
    if (!segment) {
        return NULL;
    }

    // Merge consecutive free segments on the left first so we end up at the true extent head.
    while (segment->prev != NULL && !segment->prev->is_allocated) {
        heap_segment_t* left = segment->prev;

        left->next = segment->next;
        left->segment_size += segment->segment_size;
        if (segment->next != NULL) {
            segment->next->prev = left;
        }
        segment = left;
    }

    // Merge consecutive free segments on the right into the surviving segment.
    while (segment->next != NULL && !segment->next->is_allocated) {
        heap_segment_t* right = segment->next;

        segment->segment_size += right->segment_size;
        segment->next = right->next;
        if (right->next != NULL) {
            right->next->prev = segment;
        }
    }

    return segment;
}

/*
 * Dump the current heap segment list for debugging.
 */
void mem_heap_dump(int count){
    register int i = 0;
    HEAP_SEGMENT *cur_segment;

    printf("============================== [HEAP DUMP] ==============================\n");
    cur_segment = heap_segment_list_head;
    if (!cur_segment) {
        printf(" - Heap not initialized\n");
        return;
    }

    printf(" - Entry %d: %s address 0x%lX (%d bytes)\n",
           ++i,
           (cur_segment->is_allocated ? "Allocated" : "Free     "),
           cur_segment,
           cur_segment->segment_size);

    // Walk the segment list until the caller-provided limit is reached.
    while ((cur_segment = cur_segment->next) != NULL && (!count || i < count)) {
        printf(" - Entry %d: %s address 0x%lX (%d bytes)\n",
               ++i,
               (cur_segment->is_allocated ? "Allocated" : "Free     "),
               cur_segment,
               cur_segment->segment_size);
    }
}

/*
 * Initialize the fixed kernel heap region carved out during paging setup.
 */
void mem_init_heap(unsigned long int heap_start){
    unsigned long aligned_heap_start = (heap_start + (HEAP_ALIGNMENT - 1)) & ~(HEAP_ALIGNMENT - 1);
    unsigned long prefix_bytes = aligned_heap_start - heap_start;
    unsigned long heap_bytes = KERNEL_HEAP_SIZE;

    if (prefix_bytes >= heap_bytes) {
        kerror("Heap initialization failed: invalid start address 0x%lX", heap_start);
        heap_segment_list_head = NULL;
        return;
    }

    heap_bytes -= prefix_bytes;
    heap_bytes &= ~(HEAP_ALIGNMENT - 1);

    _trace("Initializing heap ");
    _trace_printf("starting at 0x%lX. Total Size: %d MB.\n", aligned_heap_start, KERNEL_HEAP_SIZE);

    heap_segment_list_head = (heap_segment_t*)aligned_heap_start;
    memzero((Address)heap_segment_list_head, sizeof(heap_segment_t));
    heap_segment_list_head->segment_size = heap_bytes;
}

/*
 * Report the total payload bytes managed by the kernel heap.
 */
unsigned long mem_heap_total_bytes(void) {
    unsigned long total_bytes = 0;
    HEAP_SEGMENT* current = heap_segment_list_head;

    while (current != NULL) {
        total_bytes += mem_heap_segment_bytes(current);
        current = current->next;
    }

    return total_bytes;
}

/*
 * Report the payload bytes currently owned by active kernel heap allocations.
 */
unsigned long mem_heap_used_bytes(void) {
    unsigned long used = 0;
    HEAP_SEGMENT* current = heap_segment_list_head;

    while (current != NULL) {
        if (current->is_allocated) {
            used += mem_heap_segment_bytes(current);
        }
        current = current->next;
    }

    return used;
}

/*
 * Report the payload bytes currently available for future heap allocations.
 */
unsigned long mem_heap_free_bytes(void) {
    unsigned long free_bytes = 0;
    HEAP_SEGMENT* current = heap_segment_list_head;

    while (current != NULL) {
        if (!current->is_allocated) {
            free_bytes += mem_heap_segment_bytes(current);
        }
        current = current->next;
    }

    return free_bytes;
}

/*
 * Reallocate one kernel heap block, preserving the smaller of the old and new
 * payload lengths.
 */
Address krealloc(Address ptr, unsigned int bytes){
        heap_segment_t* header;
        unsigned long old_payload_bytes;
        unsigned long copy_bytes;
        Address newchunk;

        kdebug("krealloc: Reallocating 0x%x to get new %ld bytes... ", ptr, bytes);

        if (ptr == 0) {
                kdebug("  -> Not allocated. Allocating new segment");
                return kmalloc((int)bytes);
        }

        if (bytes == 0) {
                kfree(ptr);                                     // free the original block when the new size is zero
                return 0;
        }

        header = (heap_segment_t*)((UByte*)(Pointer)ptr - sizeof(heap_segment_t));
        old_payload_bytes = mem_heap_segment_bytes(header);
        if (old_payload_bytes >= bytes) {
                return ptr;
        }

        newchunk = kmalloc((int)bytes);                         // allocate the replacement block
        if (!newchunk) {
                return 0;
        }

        copy_bytes = old_payload_bytes < bytes ? old_payload_bytes : bytes;
        memcpy(newchunk, ptr, (int)copy_bytes);                 // copy only the caller-visible payload bytes
        kfree(ptr);                                             // release the original block after a successful copy
        return newchunk;
}

/*
 * Allocate one variable-sized block from the kernel heap using best fit.
 */
Address kmalloc(int bytes) {
    heap_segment_t* curr;
    heap_segment_t* best = NULL;
    unsigned long request_bytes;
    unsigned long diff;
    unsigned long best_diff = (unsigned long)-1;

    if (bytes <= 0 || !heap_segment_list_head) {
        return 0;
    }

    request_bytes = mem_heap_request_bytes((unsigned long)bytes);

    // Find the smallest free segment that can still satisfy the request.
    for (curr = heap_segment_list_head; curr != NULL; curr = curr->next) {
        if (curr->segment_size < request_bytes) {
            continue;
        }
        diff = curr->segment_size - request_bytes;
        if (!curr->is_allocated && diff < best_diff) {
            best = curr;
            best_diff = diff;
        }
    }

    // Fail cleanly if no free segment can satisfy the request.
    if (best == NULL) {
        kerror("kmalloc: Heap not available for %d bytes", request_bytes);
        return 0;
    }

    // Split oversized free segments so the trailing space remains reusable.
    mem_heap_split_segment(best, request_bytes);

    best->is_allocated = 1;
    return (Address)(best + 1);                                    // return the payload immediately after the segment header
}

/*
 * Release one kernel heap block and coalesce any adjacent free segments.
 */
void kfree(Address ptr) {
        heap_segment_t* seg;

        if (!ptr || !heap_segment_list_head)
                return;

        seg = (heap_segment_t*)((UByte*)(Pointer)ptr - sizeof(heap_segment_t));
        if (!seg->is_allocated) {
                log_warning("kfree: double free detected for 0x%lX", ptr);
                return;
        }

        seg->is_allocated = 0;
        mem_heap_coalesce(seg);                                     // repair neighbouring free extents into one segment
}
