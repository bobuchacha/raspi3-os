# IPC Module Design Notes

This module is intentionally narrow for the first slice.

## Objects in scope

- Event: named synchronization object with manual-reset and auto-reset behavior.
- Mapping: named shared buffer that models `CreateFileMapping` / `MapViewOfFile` style IPC.
- Mailbox: bounded in-memory message queue implemented as a ring buffer.

## Algorithms

- Event wait: probe-style now, full wait-node integration later.
- Mapping write sequence: every successful write increments a sequence counter so readers can detect fresh data cheaply.
- Mailbox queue: head index for producer, tail index for consumer, message count to distinguish full from empty without wasting a slot.

## Near-term integration plan

1. Bind `ipc_wait_event` to scheduler wait nodes from `my-schedproc`.
2. Add kernel handle table and object namespace rules.
3. Replace heap-backed mapping buffers with VM-backed shared pages.
4. Add ACL checks and per-process visibility rules.
