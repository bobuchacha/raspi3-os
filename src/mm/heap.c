#include "ros.h"
#include "memory.h"
#include "device.h"
#include "log.h"
#include "utils.h"

static HEAP_SEGMENT         *heap_segment_list_head;

void mem_heap_dump(int count){
    register int i = 0;
    HEAP_SEGMENT *cur_segment;

    printf("============================== [HEAP DUMP] ==============================\n");
    cur_segment = heap_segment_list_head;
    printf(" - Entry %d: %s address 0x%lX (%d bytes)\n",
           ++i,
           (cur_segment->is_allocated ? "Allocated" : "Free     "),
           cur_segment,
           cur_segment->segment_size);

    while (cur_segment = cur_segment->next) {
        printf(" - Entry %d: %s address 0x%lX (%d bytes)\n",
               ++i,
               (cur_segment->is_allocated ? "Allocated" : "Free     "),
               cur_segment,
               cur_segment->segment_size);
    }
}

void mem_init_heap(unsigned long int heap_start){
    _trace("Initializing heap ");
    _trace_printf("starting at 0x%lX. Total Size: %d MB.\n", heap_start, KERNEL_HEAP_SIZE);

    heap_segment_list_head = (heap_segment_t *) heap_start;
    memzero((Address)heap_segment_list_head, sizeof(heap_segment_t));
    heap_segment_list_head->segment_size = KERNEL_HEAP_SIZE;

}

Address krealloc(Address ptr, unsigned int bytes){

        kdebug("krealloc: Reallocating 0x%x to get new %ld bytes... ", ptr, bytes);

        if(ptr == 0) {
                kdebug("  -> Not allocated. Allocating new segment");
                return kmalloc(bytes);
        }
        heap_segment_t * header = (Pointer)ptr - sizeof(heap_segment_t);
        Address newchunk = kmalloc(bytes);                        // allocate new chunk
        if(!newchunk) return 0;                                 // Don't know if this can actually happen
        memcpy(newchunk, ptr, header->segment_size);            // copy data from old chunk to new
        kfree(ptr);                                             // free old trunk
        return newchunk;                                        // return new trunk
}

Address kmalloc(int bytes) {
    heap_segment_t * curr, *best = NULL;
    int diff, best_diff = 0x7fffffff; // Max signed int

    // _trace("Allocating %ld bytes...", bytes);

    // Add the header to the number of bytes we need and make the size 4 byte aligned
    bytes += sizeof(heap_segment_t);
    bytes += bytes % 16 ? 16 - (bytes % 16) : 0;

    // Find the allocation that is closest in size to this request
    for (curr = heap_segment_list_head; curr != NULL; curr = curr->next) {

//       kdebug("---> curr: 0x%x, next 0x%x, address of next 0x%x", curr, curr->next, &curr->next); // some readon curr gets to a really big number curr: 0x202020317E554E5F;

        diff = curr->segment_size - bytes;
        if (!curr->is_allocated && diff < best_diff && diff >= 0) {
            best = curr;
            best_diff = diff;
        }
    }

    // There must be no free memory right now :(
    if (best == NULL){
        kerror("kmalloc: Heap not available for %d bytes", bytes);
        return 0;
    }

    // If the best difference we could come up with was large, split up this segment into two.
    // Since our segment headers are rather large, the criterion for splitting the segment is that
    // when split, the segment not being requested should be twice a header size
    if (best_diff > (int)(2 * sizeof(heap_segment_t))) {
        memzero(((Address)(best)) + bytes, sizeof(heap_segment_t));
        curr = best->next;
        best->next = ((void*)(best)) + bytes;
        best->next->next = curr;
        best->next->prev = best;
        best->next->segment_size = best->segment_size - bytes;
        best->segment_size = bytes;
    }
    
    // _trace("Allocated at address: 0%lx\n", best+1);

    best->is_allocated = 1;
    return (Address)(best + 1);    // after segment metadata
}

long int pointer_to_int(void* ptr){
        long int *u=(void*)&ptr;
        return *u;
}

void kfree(Address ptr) {
        if (!ptr)
                return;

        // _trace("Free heap at address: ");
        // _trace_printf("%lx\n", ptr);

        heap_segment_t * seg = (Pointer)ptr - sizeof(heap_segment_t);
        seg->is_allocated = 0;

        // try to coalesce segements to the left
        while(seg->prev != NULL && !seg->prev->is_allocated) {
                seg->prev->next = seg->next;
                //kinfo("kfree: setting previous segment's next pointer to 0x%x", seg->next);
                seg->prev->segment_size += seg->segment_size;
                seg = seg->prev;
//                kdebug("  -> Merge to left free segment at: 0x%x", seg->prev);
        }
        // try to coalesce segments to the right
        while(seg->next != NULL && !seg->next->is_allocated) {
                seg->segment_size += seg->next->segment_size;
                seg->next = seg->next->next;
            //kinfo("kfree: setting next segment's next pointer to 0x%x", seg->next->next);
//                kdebug("  -> Merge to right free segment at: 0x%x", seg->next);
        }
}