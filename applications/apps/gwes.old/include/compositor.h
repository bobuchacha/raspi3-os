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

/*
 * Initialize the compositor and its desktop surface.
 *
 * @return Zero on success, or a negative status code on failure.
 */
long compositor_init(void);