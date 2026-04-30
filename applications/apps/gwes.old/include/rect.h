#pragma once
#include "types.h"

typedef struct Rect {
    U32 x;
    U32 y;
    U32 width;
    U32 height;

    bool is_empty() const { return width == 0U || height == 0U; }
    U32 right() const { return x + width; }
    U32 bottom() const { return y + height; }

    bool intersects(const Rect& other) const {
        return !(right() <= other.x || other.right() <= x || bottom() <= other.y || other.bottom() <= y);
    }

    Rect intersection(const Rect& other) const {
        if (!intersects(other)) {
            return { 0, 0, 0, 0 };
        }
        U32 nx = x > other.x ? x : other.x;
        U32 ny = y > other.y ? y : other.y;
        U32 nright = right() < other.right() ? right() : other.right();
        U32 nbottom = bottom() < other.bottom() ? bottom() : other.bottom();
        return { nx, ny, nright - nx, nbottom - ny };
    }

} Rect;