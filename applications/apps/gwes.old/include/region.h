/**
 * region.h
 *
 *  tracks dirty region
 */

#pragma once
#include "rect.h"

constexpr int MAX_RECTS = 64;

struct Region {
    Rect rects[MAX_RECTS];
    int count;

    void clear() {
        count = 0;
    }

    void add(const Rect& rect) {
        if (!rect.is_empty() && count < MAX_RECTS) {
            rects[count++] = rect;
        }
    }
};