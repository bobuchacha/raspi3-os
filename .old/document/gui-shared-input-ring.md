# Shared Input Ring Design

## Goal

Replace the current single-owner raw GUI input queue with one kernel-produced shared-memory ring that both `gwes.exe` and `core.exe` can observe at the same time.

The target properties are:

- one kernel producer,
- one shared event stream for both mouse and keyboard,
- one global tail sequence,
- one independent head sequence per consumer process,
- acquire/release through a syscall,
- no dynamic allocation on the hot input path,
- compatibility with the existing GUI transport ABI.

## Current Constraints

The current code already gives us most of the plumbing we need.

- `kernel/gui.c` owns a fixed raw input ring and already normalizes pointer and key input into `GuiInputEvent` records.
- `kernel/service-call.c` currently gates that ring to one owner with `g_gui_input_owner_pid` and `GUI_CONTROL_INPUT_ACQUIRE`.
- `kernel/arch/cortex-a53/mmu.c` already exposes `process_map_shared_page()`, which is the right primitive for mapping the same physical pages into multiple user processes.
- `kernel/ipc/ipc_mapping.c` documents the intended future direction for VM-backed shared mappings, but its current backing store is heap memory and it does not map pages into EL0 yet.

That means the clean v1 is a GUI-specific shared mapping built on `process_map_shared_page()`, not a generic IPC mapping syscall.

## Why One Unified Ring

Even though the shared area is for mouse and keyboard input, v1 should keep one unified event stream instead of separate mouse and keyboard rings.

Reasons:

- it preserves total event order across devices,
- it matches the existing `GuiInputEvent` type model,
- it keeps the user-visible contract to one tail number, as requested,
- it lets GWES replay the same stream directly into `windowKitTranslateInputEvent()`.

If later code wants device-specific views, it can filter the unified stream in user space or expose summary counters in the shared header.

## Proposed User VA Window

Reserve one fixed user virtual window for the input mapping.

Suggested constants:

```c
#define USER_SHARED_INPUT_VIEW_BASE  0xC00000UL
#define USER_SHARED_INPUT_VIEW_LIMIT 0xC10000UL
```

Why this range:

- heap already occupies `0x100000..0x800000`,
- DLL-local task storage already occupies `0x800000..0xC00000`,
- one 64 KiB input window is enough for a header plus a reasonably deep ring.

Using one fixed VA for every process keeps acquire logic simple and avoids introducing a general shared-map allocator in the first slice.

## Proposed GUI Control Commands

Do not add new top-level syscall numbers for v1. Reuse `SYS_GUI_CONTROL` and extend the GUI control subcommand space.

Suggested additions:

```c
#define GUI_CONTROL_SHARED_INPUT_ACQUIRE 17UL
#define GUI_CONTROL_SHARED_INPUT_RELEASE 18UL
#define GUI_CONTROL_SHARED_INPUT_QUERY   19UL
```

This keeps the shared-input transport grouped with the rest of the low-level GUI ABI.

## Shared Region Layout

The kernel owns one shared region object and maps the same physical pages into every consumer process.

Suggested ABI:

```c
#define GUI_SHARED_INPUT_MAGIC         0x53474955U
#define GUI_SHARED_INPUT_VERSION       1U
#define GUI_SHARED_INPUT_MAX_CONSUMERS 8U
#define GUI_SHARED_INPUT_CAPACITY      256U

typedef struct GuiSharedInputConsumerStruct {
    uint32_t pid;
    uint32_t flags;
    uint64_t head_sequence;
    uint64_t drop_count;
    uint64_t last_seen_msec;
} GuiSharedInputConsumer;

typedef struct GuiSharedInputRecordStruct {
    uint64_t sequence;
    uint64_t uptime_msec;
    GuiInputEvent event;
    uint32_t reserved;
} GuiSharedInputRecord;

typedef struct GuiSharedInputRegionStruct {
    uint32_t magic;
    uint16_t version;
    uint16_t max_consumers;
    uint32_t capacity;
    uint32_t record_size;
    uint64_t tail_sequence;
    uint64_t produced_count;
    uint64_t overflow_count;
    uint64_t pointer_event_count;
    uint64_t key_event_count;
    GuiPointerState last_pointer_state;
    GuiSharedInputConsumer consumers[GUI_SHARED_INPUT_MAX_CONSUMERS];
    GuiSharedInputRecord records[GUI_SHARED_INPUT_CAPACITY];
} GuiSharedInputRegion;

typedef struct GuiSharedInputViewStruct {
    uint32_t version;
    uint32_t flags;
    uint64_t view_address;
    uint32_t view_size;
    uint32_t consumer_index;
    uint64_t initial_head_sequence;
} GuiSharedInputView;
```

Notes:

- `tail_sequence` is the next sequence number to publish. Valid unread records are in `[head_sequence, tail_sequence)`.
- `sequence` inside each record is required so readers can detect wrap and overrun precisely.
- `GuiPointerState last_pointer_state` gives cheap access to the latest pointer position without forcing every observer to walk the ring.
- the consumer slot is intentionally small and written by one process only.

## Consumer Semantics

Each participating process gets one slot in `consumers[]`.

Rules:

- the kernel is the only writer of `tail_sequence` and `records[]`,
- each process writes only its own `head_sequence`, `drop_count`, and `last_seen_msec`,
- no process writes another process's slot,
- the kernel clears a slot when the owning process exits or releases it.

This gives the required model: same shared tail, separate head per process.

## Acquire Flow

`core.exe` or `gwes.exe` calls `SYS_GUI_CONTROL(GUI_CONTROL_SHARED_INPUT_ACQUIRE, &view)`.

Kernel-side flow:

1. Validate the caller and the output buffer.
2. Create the shared region lazily on first acquire if it does not exist yet.
3. Find an existing consumer slot for `current_process->id`, or allocate a free one.
4. Map the shared pages into the caller at `USER_SHARED_INPUT_VIEW_BASE` with `process_map_shared_page()`.
5. Initialize the caller's `head_sequence` to `tail_sequence` by default.
6. Return `GuiSharedInputView` with the mapped VA, size, and consumer index.

Defaulting `head_sequence` to the current tail avoids replaying stale boot-time backlog when a process attaches late.

Optional later flag:

- `GUI_SHARED_INPUT_ACQUIRE_FROM_OLDEST` to start at `max(0, tail_sequence - capacity)`.

## Release Flow

`SYS_GUI_CONTROL(GUI_CONTROL_SHARED_INPUT_RELEASE, 0)` should:

- clear the caller's consumer slot,
- unmap the fixed input window from that task,
- leave the shared object alive while at least one other process still uses it.

Process-exit cleanup should also call the same kernel helper so slots do not stay pinned by dead tasks.

## Producer Algorithm

The kernel producer path should publish immutable records.

Suggested algorithm:

```text
slot = tail_sequence % capacity
records[slot].sequence = tail_sequence
records[slot].uptime_msec = get_system_timer() / 1000
records[slot].event = normalized GuiInputEvent
release barrier
tail_sequence += 1
produced_count += 1
update pointer/key counters
```

Important design choice:

- do not mutate the last published record in place for move coalescing once the shared ring is the source of truth.

Reason:

- with multiple consumers, one process may already have advanced past the previous move while another has not.

If producer-side coalescing remains important, keep it in a private kernel staging queue before immutable records are committed to the shared ring.

## Consumer Read Algorithm

Suggested user-space read loop:

```text
tail = load_acquire(region->tail_sequence)
oldest = (tail > capacity) ? (tail - capacity) : 0

if (consumer->head_sequence < oldest) {
    consumer->drop_count += oldest - consumer->head_sequence
    consumer->head_sequence = oldest
}

while (consumer->head_sequence < tail) {
    record = &region->records[consumer->head_sequence % capacity]
    if (record->sequence != consumer->head_sequence) {
        break
    }

    consume(record->event)
    consumer->head_sequence += 1
}

consumer->last_seen_msec = getUptimeMs()
```

This gives lock-free multi-consumer reads with explicit overrun recovery.

## Overflow Policy

The producer must never block on a slow reader.

That means:

- the ring is always overwrite-capable,
- the oldest available sequence at any moment is `max(0, tail_sequence - capacity)`,
- each slow consumer detects its own data loss and updates its own `drop_count`.

This is better than tying producer progress to the slowest reader.

## Memory Ordering

Because this is a kernel-to-user shared page on AArch64, publish order matters.

Rules:

- the kernel writes the record first,
- the kernel publishes `tail_sequence` last with release ordering,
- user space reads `tail_sequence` with acquire ordering before reading records,
- consumer head updates can be plain stores in v1 because only the owning process writes them.

In the current codebase, this can be implemented with explicit barriers near the tail update even before a full C11 atomic layer exists.

## Security Model

For v1, map the region read-write and treat `core.exe` and `gwes.exe` as trusted system consumers.

That keeps the implementation small.

If later the shared input stream is exposed to untrusted apps, split the mapping into:

- one read-only common ring/header page set,
- one small read-write per-consumer control page.

That prevents one consumer from corrupting another consumer's head state.

## Compatibility Plan

The shared ring should become the source of truth for raw GUI input.

Compatibility options:

- keep `GUI_CONTROL_INPUT_EVENT_POLL` as a wrapper that reads one event from the caller's shared consumer slot and advances that head,
- or keep the old single-event poll path temporarily while GWES and core move to the mapping first.

The better long-term shape is the first one, because it removes duplicate queueing logic.

## Kernel Bookkeeping Additions

The minimum new kernel state is:

- one static or lazily allocated `GuiSharedInputRegion`,
- one small array of physical pages backing it,
- one helper to map or unmap that region at `USER_SHARED_INPUT_VIEW_BASE`,
- one per-process association to remember whether the process owns a consumer slot.

Suggested new per-task or per-process fields:

```c
int gui_input_consumer_index;
Bool gui_input_view_mapped;
```

If the association lives on `Process`, both `core.exe` and `gwes.exe` can have independent slots even if thread ownership changes inside the process later.

## Why Not Use Generic IPC Mapping First

The IPC mapping module is the right long-term abstraction, but not the right first landing point.

Reasons:

- its current backend is heap memory, not VM-backed shared pages,
- there is no user-visible mapping syscall for it yet,
- GUI input already has a stable syscall multiplexor in `SYS_GUI_CONTROL`.

So the pragmatic order is:

1. ship a GUI-specific shared input mapping on top of `process_map_shared_page()`,
2. later rebase that implementation onto a generic VM-backed IPC mapping layer.

## Migration Plan

Recommended implementation order:

1. Add the new GUI ABI structs and control IDs to both kernel and app headers.
2. Add one fixed shared-input mapping window at `0xC00000..0xC10000`.
3. Build kernel acquire/release helpers in `service-call.c`.
4. Move kernel input publication from the single-owner queue into the shared ring.
5. Port `gwes.exe` to consume the shared ring directly.
6. Add a small observer in `core.exe` so it can inspect the same stream without draining GWES.
7. Rework `GUI_CONTROL_INPUT_EVENT_POLL` as a compatibility wrapper or remove it once all consumers have migrated.

## Recommended v1 Decision

The cleanest first implementation is:

- one unified immutable event ring,
- fixed shared VA window,
- GUI-control acquire/release commands,
- trusted RW mapping for `core.exe` and `gwes.exe`,
- per-process consumer slots with independent head sequences,
- no producer blocking and no in-place mutation of published records.

That meets the requested behavior while reusing the repo's existing MMU and GUI syscall structure.