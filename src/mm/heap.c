#include "ros.h"
#include "memory.h"
#include "device.h"
#include "log.h"
#include "utils.h"

#define _trace          log_info
#define _trace_printf   printf

static HEAP_SEGMENT         *heap_segment_list_head;

void memory_heap_dump(int count){
    register int i = 0;
    HEAP_SEGMENT *cur_segment;

    printf("============================== [HEAP DUMP] ==============================\n");
    cur_segment = heap_segment_list_head;
    printf(" - Entry %d %s address 0x%lX (%d bytes)\n",
           ++i,
           (cur_segment->is_allocated ? "Allocated" : "Free"),
           cur_segment,
           cur_segment->segment_size);

    while (cur_segment->next) {
        cur_segment = cur_segment->next;
        printf(" - Entry %d %s address 0x%lX (%d KB)\n",
               ++i,
               (cur_segment->is_allocated ? "Allocated" : "Free"),
               cur_segment->segment_size);
    }
}

void memory_init_heap(unsigned long int heap_start){
    _trace("Initializing heap ");
    _trace_printf("starting at 0x%lX. Total Size: %d MB.\n", heap_start, KERNEL_HEAP_SIZE);

    heap_segment_list_head = (heap_segment_t *) heap_start;
    memzero(heap_segment_list_head, sizeof(heap_segment_t));
    heap_segment_list_head->segment_size = KERNEL_HEAP_SIZE;

    memory_heap_dump(100);
}
