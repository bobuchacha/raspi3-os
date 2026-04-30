# my-gwes Design Notes

`my-gwes` is a host-buildable GUI-server scaffold derived from the CE7/CE8 GWES analysis.

## Implemented module split

- `gwe_context.c`: module lifetime, shared helpers, region logic, desktop state
- `gwe_window.c`: window creation, destruction, z-order, capture, query
- `gwe_message.c`: per-thread queues, post/send/get/dispatch
- `gwe_input.c`: pointer and key routing
- `gwe_graphics.c`: invalidation, paint lifecycle, software composition
- `gwe_debug.c`: text dump helpers

## Current semantic boundaries

- `PostMessage` is queue-based.
- `SendMessage` is synchronous immediate delivery in this scaffold.
- `BeginPaint` and `EndPaint` operate on host-side backing surfaces.
- The compositor blends visible windows from bottom to top into a desktop buffer.
- Invalid regions are bounding rectangles, not arbitrary region lists.

## Next likely extension points

- integrate wait objects from `my-schedproc`
- export surfaces through `my-ipc` shared mappings
- add timer queue delivery and non-client messages
- add child-window clipping and occlusion-aware invalidation
- add control classes and shell/taskbar policy on top of this module
