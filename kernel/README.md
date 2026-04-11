# Kernel

This directory holds the higher-half 64-bit kernel.

## Rules

- Public subsystem interfaces stay flat under `kernel/include/`.
- Subsystem-private headers stay under `kernel/include/internal/`.
- ISA code stays under `kernel/arch/`.
- Board-family and CPU-family composition stays under `kernel/platform/`.
- Common subsystem code stays under its own source directory and only depends on public headers unless it is explicitly private.

## Current Focus

The current tree is intentionally skeletal. It establishes the rewrite boundaries so implementation can be added incrementally without mixing public contracts, board code, and internal subsystem state.
