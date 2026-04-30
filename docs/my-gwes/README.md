# my-gwes

`my-gwes` is the production-facing scaffold for a new graphics, windowing, events, and shell-style UI server module.

It follows the same modular style as `my-ipc` and `my-schedproc`:

- public headers in `include/`
- implementation units in `src/`
- host-side tests in `tests/`
- module-local design notes in `documents/`

## Current scope

- module context initialization and shutdown
- per-thread GUI queue registration and teardown
- top-level and child window creation, destruction, query, capture, and focus state
- asynchronous posted messages and synchronous send-message delivery
- pointer and keyboard routing with topmost-window hit-testing
- rectangle-based invalidation and `BeginPaint` / `EndPaint` lifecycle
- host-side backing surfaces and simple software composition into a desktop buffer
- host-side smoke test

## Not implemented yet

- scheduler-backed blocking waits for GUI threads
- kernel/user shared-memory surface export
- real display-present callbacks and scanout synchronization
- non-rectangular regions and advanced clipping
- menu, dialog, control, IME, and accessibility layers
- multi-session desktops and multi-monitor policy
- hardware-accelerated rendering backends

## Why this exists

This module turns the GWES design document into a first working implementation.

It keeps the kernel-facing contract small while putting window semantics, input routing,
message queues, painting, and composition inside a dedicated GUI server module that can
later be wired to your kernel primitives.

## Build

```sh
make
```

## Run host smoke test

```sh
make test
```

## When you need kernel wiring

You only need kernel wiring for:

- true thread blocking and wakeup integration
- shared-memory surface mapping across processes
- input device packet ingestion from the kernel
- timer delivery from the scheduler
- real display present and mode-set callbacks

The current host smoke test is enough to validate the first end-to-end slice:
window creation, queue delivery, paint invalidation, hit-testing, focus, capture,
and desktop composition.
