#include "printf.h"
#include "task.h"
/**
 * this is our own timer handler
*/
void timer_tick(){
    schedler_timer_tick();
    // printf(".");
}