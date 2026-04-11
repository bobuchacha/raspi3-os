#ifndef _TOUCH_H
#define _TOUCH_H

#include "ros.h"

typedef struct touch_state
{
    Bool ready;
    Bool pressed;
    UInt x;
    UInt y;
    UInt raw_x;
    UInt raw_y;
    UInt contact_count;
} TouchState;

int touch_init(void);
void touch_poll(void);
int touch_is_ready(void);
const TouchState *touch_get_state(void);

#endif