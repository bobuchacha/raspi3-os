# Kernel Scheduler And Process-Manager Module Plan

This document is the concrete implementation plan for building new scheduler and process-manager modules for your kernel in the same style as `my-loader`.

The intent is:
- modular,
- callback-friendly,
- documented like `my-loader`,
- small enough to land incrementally,
- structured so the loader can depend on the process manager and scheduler cleanly.

## Target Architecture

Build this as one new kernel subsystem package with two public faces:
- scheduler module,
- process-manager module.

Keep them in one repository subtree because they are tightly coupled, but separate their public headers and implementation units.

Recommended package shape:

```text
my-schedproc/
  README.md
  documents/
    scheduler_design.md
    process_manager_design.md
    scheduler_flow.mmd
    process_lifecycle.mmd
  include/
    sp_api.h
    sp_types.h
    sp_scheduler.h
    sp_process.h
    sp_thread.h
    sp_wait.h
    sp_internal.h
  src/
    sp_scheduler.c
    sp_runqueue.c
    sp_sleep.c
    sp_wait.c
    sp_process.c
    sp_thread.c
    sp_sync.c
    sp_debug.c
  tests/
    README.md
    scheduler_demo.c
    scheduler_test_round_robin.c
    scheduler_test_sleep.c
    scheduler_test_preempt.c
    scheduler_test_exit.c
```

If you prefer to keep everything inside the existing project tree, mirror this under `my-loader/` as a sibling subsystem rather than mixing files into loader source immediately.

## Module Boundary

## Scheduler module owns
- ready queues
- sleep queue
- current thread per CPU
- dispatch logic
- quantum accounting
- wakeup logic
- priority recomputation trigger points
- idle-thread fallback
- scheduler instrumentation

## Process-manager module owns
- process objects
- thread objects
- process and thread IDs
- process state machine
- thread creation and destruction
- user/kernel stack metadata
- exit / terminate state transitions
- process-thread relationships
- loader-facing process bootstrap operations

## Shared ownership rules
- process manager allocates and frees thread/process objects
- scheduler never owns thread memory
- scheduler only mutates scheduling fields and queue links
- process manager asks scheduler to enqueue, block, wake, or remove threads

That ownership split will prevent the most common kernel design mistake: lifetime logic mixed into dispatch code.

## Public API Design

Model the callback style on `my-loader/include/ldr_api.h`.

## `sp_api.h`

This should define the kernel integration contract for scheduler/process-manager code.

Suggested callback groups:
- memory:
  - `heapAlloc`
  - `heapFree`
  - `vmCreateUserStack`
  - `vmCreateKernelStack`
  - `vmFreeStack`
- arch:
  - `archInitThreadContext`
  - `archSwitchContext`
  - `archSetThreadEntry`
  - `archSaveFpState`
  - `archRestoreFpState`
- time and interrupts:
  - `getTickCount`
  - `setTimerDeadline`
  - `requestReschedule`
  - `disableInterrupts`
  - `enableInterrupts`
- debug/log:
  - `logPrintf`
  - `traceEvent`

That makes the module portable across your kernel evolution the same way `my-loader` is portable across loader-host kernels.

## `sp_types.h`

Put all shared enums and fundamental types here:
- result codes
- process states
- thread states
- wait reasons
- priority constants
- affinity mask type

## `sp_process.h`

Public process-manager API should include:
- `sp_init_process_manager`
- `sp_create_process`
- `sp_destroy_process`
- `sp_create_thread`
- `sp_destroy_thread`
- `sp_begin_process_exit`
- `sp_terminate_process`
- `sp_mark_thread_exit`
- `sp_set_main_thread_entry`
- `sp_attach_module_to_process`

## `sp_scheduler.h`

Public scheduler API should include:
- `sp_init_scheduler`
- `sp_make_thread_runnable`
- `sp_block_current_thread`
- `sp_sleep_current_thread`
- `sp_unblock_thread`
- `sp_yield_current_thread`
- `sp_tick`
- `sp_dispatch`
- `sp_set_thread_priority`
- `sp_set_thread_affinity`
- `sp_dump_scheduler_state`

## `sp_wait.h`

Keep wait semantics isolated from base scheduler logic.

Suggested APIs:
- `sp_wait_event`
- `sp_wait_mutex`
- `sp_signal_event`
- `sp_release_mutex`
- `sp_timeout_waiter`
- `sp_cancel_waits`

Even if you do not implement all of them at first, defining the separation early will keep the code organized.

## Internal Data Model

## Thread object

Keep these fields grouped together:
- identity:
  - thread ID
  - owner process
  - thread name
- scheduling:
  - base priority
  - current priority
  - quantum
  - quantum left
  - run state
  - suspend count
  - affinity
- timing:
  - wake tick
  - runnable-since tick
  - blocked-since tick
  - user time
  - kernel time
- queue links:
  - ready queue prev/next
  - sleep queue prev/next
  - owned-lock list
  - wait-node list
- architecture:
  - saved context
  - kernel stack
  - user stack
  - floating point or coprocessor save area
- teardown:
  - exit code
  - dying/dead flags

The key Windows CE lesson is to embed the run/sleep links inside the thread object.

## Process object

Keep these fields:
- process ID
- process name
- process state
- thread list head
- main thread pointer
- thread count
- address-space handle or VM root
- loaded module list
- affinity mask
- aggregate exited-thread user/kernel time

Do not let scheduler code allocate or destroy process objects.

## Wait node

Add one internal wait-node structure similar to CE `PROXY`:
- thread pointer
- wait object pointer
- wait type
- effective enqueue priority
- completion result
- links in object wait queue
- links in per-thread wait list

You may not implement `wait for many` immediately, but this structure should still exist from the start.

## File-By-File Implementation Plan

## Phase 1: Type And API Skeleton

Files:
- `include/sp_types.h`
- `include/sp_api.h`
- `include/sp_scheduler.h`
- `include/sp_process.h`
- `include/sp_internal.h`

Tasks:
- define result codes and state enums
- define opaque public handles or pointers
- define callback table consumed by the module
- define naming and prefix rules
- define which module owns which fields

Exit criteria:
- headers compile on their own
- no scheduler policy code yet
- API names and state model are frozen enough to code against

## Phase 2: Process And Thread Lifetime Core

Files:
- `src/sp_process.c`
- `src/sp_thread.c`

Tasks:
- implement process creation/destruction
- implement thread creation with suspended initial state
- allocate kernel and user stack metadata through callbacks
- initialize architecture context through `archInitThreadContext`
- add process state transitions:
  - STARTING
  - NORMAL
  - EXITING
  - DEAD
- add thread state transitions:
  - CREATED
  - SUSPENDED
  - RUNNABLE
  - RUNNING
  - BLOCKED
  - SLEEPING
  - DYING
  - DEAD

Exit criteria:
- can create process and main thread objects
- can create additional threads
- no actual scheduling required yet

## Phase 3: Ready Queue Implementation

Files:
- `src/sp_scheduler.c`
- `src/sp_runqueue.c`

Tasks:
- implement per-priority ready queues
- add ready bitmap for fast highest-priority lookup
- add `make_thread_runnable`
- add `dequeue_best_thread`
- add `yield_current_thread`
- add dispatch decision for one CPU

Recommended policy:
- priority `0` is highest priority
- queue head is next to run
- same-priority quantum expiration rotates current to tail

Exit criteria:
- single-CPU dispatch works
- preemption on higher-priority wake works
- equal-priority round-robin works

## Phase 4: Time And Sleep Queue

Files:
- `src/sp_sleep.c`
- `src/sp_scheduler.c`

Tasks:
- implement ordered sleep queue keyed by wake tick
- add `sleep_current_thread`
- add timer tick accounting
- update current thread quantum on each tick
- wake expired sleepers and requeue them
- compute next timer deadline

Exit criteria:
- sleeping thread wakes at the right tick
- timer tick drives quantum expiration and wakeups
- no polling loop is required

## Phase 5: Wait And Block Layer

Files:
- `include/sp_wait.h`
- `src/sp_wait.c`
- `src/sp_sync.c`

Tasks:
- add wait-node structure
- implement block-on-event and wake-one
- implement block-on-mutex and wake-owner handoff
- insert waiters by effective priority
- support timeout wakeups through the sleep queue
- add cancel-on-thread-exit cleanup path

Exit criteria:
- blocked thread can wake through signal or timeout
- mutex handoff is deterministic
- queue ownership invariants hold

## Phase 6: Priority Inheritance And Reprioritization

Files:
- `src/sp_sync.c`
- `src/sp_scheduler.c`

Tasks:
- track owned locks on each thread
- compute effective priority from:
  - base priority
  - highest waiting priority on owned locks
- trigger reschedule when owner priority improves
- restore priority when lock is released

Exit criteria:
- simple priority inversion test passes
- owner thread is boosted when a higher-priority waiter blocks
- boost is removed when ownership ends

## Phase 7: Exit, Kill, And Process Termination

Files:
- `src/sp_process.c`
- `src/sp_thread.c`
- `src/sp_wait.c`

Tasks:
- implement thread exit path
- implement main-thread-driven process exit
- terminate or join secondary threads
- remove dead threads from scheduler queues safely
- signal process-completion object or status
- prevent new thread creation once process starts exiting

Exit criteria:
- thread exit does not leak queue links
- process exit drains all threads cleanly
- waiting threads see completion state correctly

## Phase 8: Loader Integration

This is where the new modules start replacing ad hoc kernel process-start logic.

Current loader hooks in `my-loader` already anticipate this shape:
- `procCreate`
- `procSetEntry`
- `procAddModule`
- `procStart`

Map them to your real modules like this:
- `procCreate`
  - call process manager to create process object
  - allocate main thread in suspended state
- `procSetEntry`
  - set main thread entry PC and stack top in architecture context
- `procAddModule`
  - register loaded executable/DLL with process object
- `procStart`
  - call scheduler `make_thread_runnable` on the main thread

Recommended additions to the loader callback contract once scheduler/process manager mature:
- `threadCreateMain`
- `threadSetUserContext`
- `threadStart`
- `procSetAddressSpace`
- `procMapImage`

That will make the loader-process boundary much cleaner.

## Phase 9: SMP Expansion

Only do this after the single-CPU scheduler is solid.

Files:
- `src/sp_scheduler.c`
- `src/sp_runqueue.c`
- architecture-specific glue files

Tasks:
- add per-CPU current-thread pointer
- add per-CPU ready queues
- add affinity masks
- choose target CPU on wakeup
- send reschedule IPIs if another CPU must preempt
- add idle thread per CPU

Exit criteria:
- one runnable thread per CPU
- affinity is enforced
- cross-CPU wakeups work predictably

## Test Plan

Your module should land with tests from the beginning.

Minimum tests:
- same-priority round-robin
- higher-priority preemption
- sleep and wake timing
- block/unblock event path
- mutex priority inheritance
- thread exit while blocked
- process exit with multiple threads
- loader starts a process and first thread becomes runnable

Recommended harness shape:
- host-side simulation tests first
- then kernel-integrated tests under a debug build

## Documentation Plan

Mirror the `my-loader` documentation style.

Create these docs early:
- `documents/scheduler_design.md`
- `documents/process_manager_design.md`
- `documents/scheduler_flow.mmd`
- `documents/process_lifecycle.mmd`
- `tests/README.md`

Each `.c` file should have:
- one header comment explaining its responsibility
- structured comments on non-trivial functions
- short inline intent comments for queue or state transitions

## Recommended Milestone Order

1. API headers only
2. process/thread objects
3. single-CPU ready queues
4. timer + sleep queue
5. block/unblock waits
6. exit/terminate flows
7. loader integration
8. priority inheritance
9. SMP queues and affinity

This order matches the dependency chain instead of mixing unrelated work.

## Immediate Next Coding Steps

If you start coding now, do these first:

1. create `sp_types.h`, `sp_api.h`, `sp_process.h`, `sp_scheduler.h`
2. implement process and thread allocation in `sp_process.c` and `sp_thread.c`
3. implement a single-CPU ready queue in `sp_runqueue.c`
4. implement `sp_make_thread_runnable`, `sp_dispatch`, and `sp_tick`
5. wire `my-loader`'s `procCreate`, `procSetEntry`, and `procStart` to thin adapters over the new module

That gives you the first end-to-end slice where:
- loader creates a process,
- process manager creates a main thread,
- scheduler makes it runnable,
- kernel can dispatch it.

## Final Recommendation

Treat this subsystem exactly the way `my-loader` was treated:
- stable external API first,
- internal structs second,
- small vertical slice next,
- behavior tests before feature growth,
- add the complex policy pieces only after the base state machine is trustworthy.

If you follow that order, you will get a scheduler/process-manager pair that is easier to debug, easier to integrate with your loader, and much closer to the strongest design patterns visible in the Windows CE source trees.
