# Kernel Internal Headers

Everything below this directory is private to the kernel implementation.

- Public callers should include headers directly from `kernel/include/`.
- Subsystem sources may include `kernel/include/internal/...` for private state.
- Board-specific and CPU-specific internals should stay under `kernel/platform/` unless they are genuinely cross-subsystem.
