/**
 * Compositor interface
 */
#pragma once
#include "window.h"
#include "region.h"

struct Compositor {
    Surface desktop;
    Window** windows;
    int window_count;
    unsigned long window_capacity;
    Region damage;
};

/*
 * Initialize the compositor and its desktop surface.
 *
 * @return Zero on success, or a negative status code on failure.
 */
long compositor_init(void);