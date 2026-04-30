#pragma once

#include "types.h"

#define FILEMAPPING_DEMO_PATH "filemapping:/demo-counter"
#define FILEMAPPING_DEMO_TEXT_BYTES 384U

typedef struct FileMappingDemoState {
    unsigned long counter;
    unsigned long text_length;
    char text[FILEMAPPING_DEMO_TEXT_BYTES];
} FileMappingDemoState;

static inline unsigned long filemapping_demo_state_size(void) {
    return (unsigned long)sizeof(FileMappingDemoState);
}