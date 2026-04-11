/**
 * Compositor interface
 */
#pragma once
#include "window.h"
#include "region.h"

constexpr int MAX_WINDOWS = 1024;

struct Compositor {
    Surface desktop;
    Window* windows[MAX_WINDOWS];
    int window_count;
    Region damage;
};