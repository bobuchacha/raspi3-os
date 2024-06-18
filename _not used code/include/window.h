#ifndef __window_h_
#define __window_h_

#include "typedef.h"


typedef struct {
        INT left;
        INT top;
        INT width;
        INT height;
        STRING title;
        INT id;
} WINDOW;

typedef WINDOW* WINDOWPTR;

#endif