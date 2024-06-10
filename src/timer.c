#include "printf.h"
#include "task.h"
extern void dump_registers();
/**
 * this is our own timer handler
*/
void timer_tick(){
    schedler_timer_tick();
    // printf("[TIMER_TICKED]\n");
}