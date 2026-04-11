# Windows CE Scheduler Study

This document is a source-backed study of the Windows CE scheduler as it appears in the trees available in this workspace.

Coverage in this study:
- WINCE300: no scheduler source tree was found in the workspace, only `Help.chm`, so this study does not claim code-level findings for CE 3.0.
- WINCE420, WINCE500, WINCE600, WINCE700, WINCE800: scheduler, process, and thread internals were inspected directly.

The goal is twofold:
- explain how the Windows CE scheduler actually works,
- extract the best techniques to reuse in your own scheduler design.

## Executive Summary

Windows CE schedules threads, not processes.

The core model is stable across the versions in this tree:
- preemptive,
- strict priority-based,
- round-robin within the same priority when a quantum expires,
- sleep and wait queues for non-runnable threads,
- fast dispatch path separated from slower bookkeeping,
- heavy use of intrusive queue links embedded directly in the thread object.

The biggest evolution across versions is not the policy. It is the implementation structure:
- CE 4.2 and 5.0 use a more explicit classic `RunList_t` plus `SleepList` design in `schedule.c`.
- CE 6.0 starts moving toward more generalized internal queue primitives.
- CE 7.0 and 8.0 add a more explicit `TWO_D_QUEUE` model, per-CPU affinity queues, processor control blocks, and compatibility mechanisms such as stub threads for older process behavior.

If you want to design your own scheduler, the best ideas to borrow are:
- per-priority ready queues,
- a wake-time-ordered sleep queue,
- explicit separation between housekeeping and selection,
- embedded queue nodes inside thread structures,
- priority inheritance / boost hooks around lock ownership,
- per-thread accounting for quantum, user time, and kernel time,
- per-CPU run queues once you move to SMP.

## Scope And Evidence

Primary files examined:

- WINCE420:
  - `PRIVATE/WINCEOS/COREOS/NK/KERNEL/schedule.c`
  - `PRIVATE/WINCEOS/COREOS/NK/INC/schedule.h`
  - `PRIVATE/WINCEOS/COREOS/NK/INC/kernel.h`
  - `AGENTS-scheduler.md`
- WINCE500:
  - `PRIVATE/WINCEOS/COREOS/NK/KERNEL/schedule.c`
  - `PRIVATE/WINCEOS/COREOS/NK/INC/schedule.h`
  - `PRIVATE/WINCEOS/COREOS/NK/INC/kernel.h`
- WINCE600:
  - `PRIVATE/WINCEOS/COREOS/NK/INC/schedule.h`
  - `PRIVATE/WINCEOS/COREOS/NK/KERNEL/schedule.c`
- WINCE700:
  - `private/winceos/COREOS/nk/inc/schedule.h`
  - `private/winceos/COREOS/nk/inc/thread.h`
  - `private/winceos/COREOS/nk/inc/process.h`
  - `private/winceos/COREOS/nk/kernel/schedule.c`
  - `private/winceos/COREOS/nk/kernel/kcalls.c`
  - `private/winceos/COREOS/nk/kernel/thread.c`
  - `private/winceos/COREOS/nk/kernel/process.c`
- WINCE800:
  - `private/winceos/COREOS/nk/inc/schedule.h`
  - `private/winceos/COREOS/nk/inc/thread.h`
  - `private/winceos/COREOS/nk/kernel/kcalls.c`

## What The Scheduler Schedules

Windows CE schedules threads.

That distinction matters:
- a process owns address space, handles, loaded modules, and a list of threads,
- a thread owns execution state, stack, current priority, quantum, wait state, and CPU context.

Processes are containers.
Threads are dispatchable work units.

## Core Definitions

### Base Priority

The thread's nominal priority. In newer trees this is `bBPrio`.

### Current Priority

The effective scheduling priority. In newer trees this is `bCPrio`.

It can differ from base priority because of:
- priority inheritance,
- temporary boost,
- scheduler policy on owned synchronization objects,
- system throttling during low-memory situations.

### Quantum

The time slice assigned to a thread. In the inspected trees this is represented by:
- `dwQuantum`
- `dwQuantLeft`

If a thread has peers at the same priority, quantum expiration causes round-robin behavior.

### Runnable

A runnable thread is ready to execute and is waiting on a ready queue.

### Running

The currently executing thread on a CPU.

### Blocked

A blocked thread is waiting for an object such as:
- event,
- mutex,
- semaphore,
- critical section,
- another thread,
- composite wait / timed wait.

### Sleeping

A sleeping thread is waiting for time to pass. It is not waiting on a synchronization object alone; it is waiting on a wakeup deadline.

## Version-By-Version Architecture

## CE 4.2

The CE 4.2 scheduler is the clearest place to study the classic design.

The important structures are in `schedule.h`:
- `RunList_t` holds:
  - `pRunnable`: the runnable list head,
  - `pth`: the currently running thread,
  - `pHashThread[32]`: a hash/index table to speed access into the priority groups.
- `PROXY` is the generic wait node placed on synchronization object queues.

The important execution paths are in `schedule.c`:
- `NextThread()` handles pending interrupts and timeout wakeups.
- `KCNextThread()` decides whether to keep running the current thread or switch.

Behavior visible in the code:
- pending interrupt events are drained first,
- expired sleepers are taken from `SleepList`, marked runnable, and enqueued,
- quantum is decremented for the running thread,
- if another thread of the same priority is ready and the quantum expired, the current thread is rotated to the tail,
- if a higher-priority runnable thread exists, the current thread is re-enqueued and preempted,
- `dwReschedTime` tracks the next scheduling deadline from either quantum expiry or the earliest sleeper wakeup.

This is a classic single-global-run-queue priority scheduler with time-based wakeup.

## CE 5.0

CE 5.0 keeps the same scheduler model and largely the same control flow.

What stands out in the inspected files:
- same `RunList_t` structure,
- same `SleepList` model,
- same `NextThread()` / `KCNextThread()` split,
- added OEM reschedule instrumentation hooks such as `pfnOEMReschedule`,
- additional CeLog instrumentation around quantum expiration and thread switch.

The design is still fundamentally the CE 4.x scheduler, just with more instrumentation and integration points.

## CE 6.0

CE 6.0 still exposes a `RunList_t` in `schedule.h`, but the broader kernel organization changes materially:
- thread lifecycle code is more clearly separated into `thread.c`,
- process lifecycle code is separated into `process.c`,
- the internal queueing model starts aligning with the later `TWO_D_QUEUE` design.

This is the transition era where the scheduler is no longer just a single file to understand in isolation.

## CE 7.0

CE 7.0 is the most useful source tree if your goal is to design a modern scheduler.

Important changes visible in the source:
- `RunList` is now an external `TWO_D_QUEUE` instead of the old explicit `RunList_t` struct in the public header.
- `THREAD` embeds queue links so the thread itself can be used as a node in intrusive run/sleep queues.
- each thread has `pRunQ`, allowing the scheduler to place it on:
  - a global run queue,
  - a per-process compatibility queue,
  - a per-CPU affinity queue.
- `PROCESS` now contains `pStubThread`, which is used for backward compatibility process scheduling behavior.
- the PCB contains per-CPU `affinityQ`, enabling SMP-aware dispatch.

In practice, CE 7.0 uses these ideas:
- per-thread effective queue selection (`pRunQ`),
- per-CPU scheduling decisions,
- process-level stub threads for older binaries or compatibility rules,
- scheduler lock protection around queue mutation,
- explicit runnable/block timestamps for debugging and starvation analysis.

The hot path moved into `kcalls.c`. Functions such as `MakeRun`, `RunqDequeue`, `CalcQuantumLeft`, `BlockCurThread`, and `SCHL_MakeRunIfNeeded` show the real modern dispatch mechanics.

## CE 8.0

CE 8.0 keeps the CE 7.0 architecture and refines it.

Visible differences from the inspected headers:
- `THREAD` adds extra state bits like `SRWLOCKBLK_SHIFT`,
- additional thread bookkeeping fields are added,
- `affinityQ` and per-CPU routing remain,
- queue and thread layout are still built around intrusive scheduling nodes.

So CE 8.0 is an extension of the CE 7.0 design, not a new scheduler policy.

## Data Structures That Matter Most

## 1. Thread Structure

The `THREAD` structure is the heart of the scheduler.

Fields repeatedly used by scheduling code across versions:
- ownership and identity:
  - owning process,
  - thread ID,
  - start address,
- runnable/sleep queue links:
  - `pPrevSleepRun`
  - `pNextSleepRun`
  - `pUpRun`
  - `pDownRun`
- priority:
  - `bBPrio`
  - `bCPrio`
- execution state:
  - `wInfo` bitfield,
  - suspend count,
  - wait state,
- wait/sleep:
  - `lpProxy`
  - `dwWakeupTime`
  - `wCount` / `wCount2`
- time accounting:
  - `dwQuantum`
  - `dwQuantLeft`
  - `dwUTime`
  - `dwKTime`
- dispatch metadata in newer versions:
  - `pRunQ`
  - `dwSeqNum`
  - `dwAffinity`
  - `dwTimeWhenRunnable`
  - `dwTimeWhenBlocked`

The important lesson is structural: CE keeps queue linkage inside the thread, which avoids allocating separate run-queue nodes.

## 2. Process Structure

The process is not scheduled directly, but it carries scheduler-relevant state.

Important fields from CE 7.0 `process.h`:
- `thrdList`: all threads in the process,
- `wThrdCnt`: thread count,
- `bPrio`: highest priority among its threads,
- `bState`: process lifecycle state,
- `dwAffinity`: affinity mask,
- `pStubThread`: compatibility stub thread,
- `dwKrnTime` and `dwUsrTime`: aggregate exited-thread CPU time.

Process states explicitly defined in the source:
- `PROCESS_STATE_STARTING`
- `PROCESS_STATE_NORMAL`
- `PROCESS_STATE_START_EXITING`
- `PROCESS_STATE_VIEW_UNMAPPED`
- `PROCESS_STATE_NO_LOCK_PAGES`
- `PROCESS_STATE_VM_CLEARED`

These states are not scheduler states, but they matter because thread creation, thread exit, and process termination all interact with scheduling and runnable-thread teardown.

## 3. Proxy / Wait Nodes

`PROXY` is one of the most useful design patterns in the tree.

It is a single wait-node abstraction used for multiple synchronization objects.
The scheduler uses it to:
- enqueue waiting threads on object-specific queues,
- support waiting on multiple objects,
- preserve the waiting thread's effective priority inside object wait queues,
- wake the thread with a reason/return value.

This is a strong pattern if your scheduler must support rich blocking semantics.

## 4. Ready Queues

Across the examined trees, the scheduler always preserves the same idea:
- ready threads are grouped by priority,
- picking the next thread should be cheap,
- queue operations should not allocate memory in the hot path.

CE 4.2 and 5.0 implement this through `RunList_t` plus hash buckets.
CE 7.0 and 8.0 generalize it through `TWO_D_QUEUE` and per-thread embedded nodes.

## How Scheduling Actually Works

## Trigger Sources

Scheduling work is triggered by:
- timer tick,
- interrupt completion,
- an object becoming signaled,
- a thread explicitly yielding,
- a thread blocking,
- a priority change,
- affinity changes in SMP-aware versions.

## Phase 1: Housekeeping

In CE 4.2 and 5.0, this is visible in `NextThread()`.

What it does:
- drains pending interrupt events,
- wakes sleepers whose `dwWakeupTime` has passed,
- handles wait completions that were deferred,
- turns newly-unblocked threads back into runnable threads using `MakeRun`.

Key insight:
- the scheduler does not mix wakeup bookkeeping with selection logic more than necessary.

In CE 7.0 and 8.0, the same responsibilities still exist, but queue work is more split between `schedule.c` and `kcalls.c` helpers.

## Phase 2: Dispatch Decision

In CE 4.2 and 5.0, `KCNextThread()` does the dispatch work.

The logic is effectively:
1. subtract elapsed time from the current thread's remaining quantum,
2. if quantum expired and there is another runnable thread at the same priority, rotate the current thread to the tail,
3. if there is a higher-priority runnable thread, re-enqueue the current thread and preempt it,
4. if no current running thread remains, dequeue the best runnable thread and make it current,
5. update time accounting and next reschedule deadline.

In CE 7.0 and 8.0, this same policy is implemented with queue primitives and PCB-aware logic:
- `CalcQuantumLeft()` updates quantum and accounting,
- `BlockCurThread()` transitions the running thread out of execution,
- `MakeRun()` inserts a thread into the appropriate run queue,
- `RunqDequeue()` removes the chosen runnable thread,
- per-CPU and per-process queue routing influences the final selected thread.

## Strict Priority Rule

The scheduler is strict-priority-first.

That means:
- if a higher-priority thread becomes runnable, it wins,
- lower-priority threads do not get fairness against higher-priority ones,
- fairness exists only within a priority band.

This is good for embedded and real-time responsiveness.
It is not good for general-purpose fairness unless you layer additional policy on top.

## Quantum Rule

The scheduler uses quantums only to share CPU among equal-priority threads.

Observed behavior:
- if no equal-priority peer is ready, a thread may continue to run,
- if an equal-priority peer exists and the quantum expires, the current thread moves to the tail,
- some fast paths decrement `dwQuantLeft` even when a thread is reinserted at the front, to avoid starvation via mutual signaling.

That last detail from CE 7.0 `MakeRun()` is worth copying: it prevents pathological ping-pong threads from starving other runnable peers.

## Sleep Queue Behavior

Sleeping is handled separately from ordinary blocking.

The scheduler stores sleepers in wake-time order.
When time reaches the earliest sleeper's deadline:
- the thread is removed from `SleepList`,
- the sleeping bit is cleared,
- timeout bookkeeping counters are updated,
- the thread is re-enqueued if it is otherwise eligible to run.

This keeps the timer path efficient because the scheduler mostly needs to inspect the head of the sleep queue.

## Wait / Block Behavior

Blocking on objects uses `PROXY` nodes.

General pattern:
- thread creates or uses a wait proxy,
- proxy is inserted into the object's wait queue,
- thread enters blocked state,
- object signal or timeout removes the proxy,
- thread is made runnable again.

This is the part of CE worth studying carefully if your kernel needs `wait for one`, `wait for many`, timeout waits, and priority-aware lock handoff.

## Priority Inversion Handling

The trees show explicit lock-ownership bookkeeping on critical sections and mutexes.

The scheduler recalculates effective priority based on owned synchronization objects.
In the classic trees:
- owned critical sections and mutexes carry listed priority state,
- owner threads can be reprioritized.

In later trees:
- owned sync objects remain tracked in intrusive lists,
- the scheduler reevaluates current priority from base priority plus ownership state.

This is a direct anti-priority-inversion mechanism.

## Low-Memory Throttling

Another interesting CE-specific feature is low-memory throttling.

Observed in the classic scheduler:
- `currmaxprio` limits the lowest scheduled priority threshold,
- `pOOMThread` can be favored to keep the system alive,
- the scheduler may temporarily cap execution priority during out-of-memory conditions.

This is not essential for a first scheduler, but it is a useful system-protection mechanism once memory pressure becomes a real issue.

## Process And Thread Lifecycle

## Process Lifecycle

From the newer process code, the process lifecycle is roughly:
1. process object allocated and initialized,
2. state set to `PROCESS_STATE_STARTING`,
3. main thread created,
4. loader sets up image, VM, modules, stacks, and start context,
5. state becomes `PROCESS_STATE_NORMAL`,
6. process runs until explicit termination or main-thread exit,
7. state advances through exit stages,
8. VM and handle-visible process state are torn down.

Important point:
- processes are not scheduled entities,
- but process state gates whether new threads, handles, views, and VM operations are still allowed.

## Thread Creation Lifecycle

In CE 7.0 `THRDCreate()`:
- stacks are allocated,
- user and kernel stacks are created as appropriate,
- `SetupThread()` initializes the thread object and CPU context,
- a locked handle for the new thread is returned.

In `NKCreateThread()`:
- parameters are validated,
- stack size is normalized,
- `THRDCreate()` builds the thread,
- the thread is resumed unless created suspended,
- debugger notification hooks run if needed.

The important design detail is that new threads are created suspended first, then made runnable deliberately.

## Runnable To Running

When a runnable thread is selected:
- it is dequeued from the ready queue,
- its run state becomes running,
- current-thread / PCB pointers are updated,
- co-processor and CPU context handling occurs,
- active VM / token state may also change in later trees.

## Running To Blocked

A running thread becomes blocked when it:
- waits on a sync object,
- hits a timed wait,
- is suspended,
- exits,
- gets forced into a non-runnable state by termination or debugger logic.

## Running To Sleeping

A running thread enters sleeping state via `NKSleep()` or timed waits.
The scheduler records `dwWakeupTime` and inserts the thread into the sleep queue.

## Thread Exit

The exit path in CE 7.0 is instructive:
- `NKPrepareThreadExit()` handles special cases such as main-thread process exit,
- `NKExitThread()` marks the thread dead/dying, publishes exit time, performs DLL notifications, coordinates with debuggers, and synchronizes with waiters,
- process termination path may cascade from main-thread exit,
- final removal happens under scheduler control.

This is more elaborate than a hobby OS usually needs, but the design lesson is important:
- exiting a thread is not just removing it from a queue,
- it is a coordinated state transition with waiters, owners, locks, and process lifetime rules.

## Process Termination

`PROCTerminate()` in CE 7.0 shows the kernel's process termination strategy:
- kernel process cannot be terminated,
- if the target is the active process, main thread is killed and other threads are forced out,
- if it is a remote process, loader lock is used to safely inspect and terminate it,
- the caller may wait for the process object to signal completion.

This is a strong reference if you want a clean process teardown model in your own kernel.

## Evolution Summary

What stayed the same from 4.2 to 8.0:
- threads are the scheduling unit,
- priority dominates scheduling policy,
- quantum only arbitrates within equal priority,
- sleep queue is ordered by wakeup time,
- blocking uses explicit wait nodes,
- priority inversion mitigation is built into synchronization handling.

What changed:
- queue internals became more generalized and intrusive,
- scheduling state became more PCB-aware,
- SMP and affinity routing became first-class in 7.0+,
- compatibility support introduced stub-thread / per-process ready-queue behavior,
- later versions added more instrumentation and fine-grained thread state bits.

## Best Techniques To Reuse In Your Own Scheduler

If you are designing your own scheduler, these are the best techniques in this tree.

## Must Keep

1. Per-priority ready queues

This gives predictable dispatch and cheap enqueue/dequeue.

2. Wake-time-ordered sleep queue

This keeps timer processing cheap.

3. Intrusive queue nodes embedded in the thread

No allocation in the hot path.

4. Separate housekeeping from selection

Keep the actual next-thread choice fast.

5. Priority inheritance or at least priority boost on lock ownership

Without this, strict priority schedulers are vulnerable to obvious inversion problems.

6. Per-thread time accounting

You will want this for debugging, profiling, starvation detection, and policy tuning.

## Add Later

1. Per-CPU ready queues

Add this once you support SMP.

2. Affinity routing

Useful after SMP exists.

3. OOM throttling

Good operational feature, but not first-wave scheduler work.

4. Rich proxy-based wait-many support

Powerful, but more complexity than a minimal kernel needs initially.

## Probably Do Not Copy Directly

1. Compatibility stub-thread machinery

This exists for CE binary/process model constraints. Use it only if your ABI needs something similar.

2. Full CE teardown semantics

The CE exit path is production-grade and intertwined with loader, debugger, and token behavior. Copy the state-machine idea, not the exact amount of machinery.

## Recommended Reading Order In This Workspace

For the scheduler itself:
1. WINCE420 `schedule.h`
2. WINCE420 `schedule.c`
3. WINCE500 `schedule.c`
4. WINCE700 `thread.h`
5. WINCE700 `process.h`
6. WINCE700 `kcalls.c`
7. WINCE700 `thread.c`
8. WINCE700 `process.c`
9. WINCE800 `thread.h`

This order goes from the clearest simple model to the more modern implementation.

## Annotated Source Path Map

This section points at the exact implementation entry points worth reading first.

## CE 4.2 Classic Scheduler Path

- `WINCE420/PRIVATE/WINCEOS/COREOS/NK/KERNEL/schedule.c:2123`
  - `NextThread()`
  - drains pending interrupt events,
  - handles manual-event wakeups,
  - checks `SleepList` head for expired wakeups,
  - moves woken threads back to runnable state with `MakeRun()`.
- `WINCE420/PRIVATE/WINCEOS/COREOS/NK/KERNEL/schedule.c:2263`
  - `KCNextThread()`
  - subtracts elapsed time from `dwQuantLeft`,
  - rotates the current thread when a same-priority peer is ready and the quantum expired,
  - preempts when a higher-priority runnable thread exists,
  - recalculates effective priority from owned synchronization objects,
  - updates `dwReschedTime` from the earlier of next wakeup or next quantum deadline.

This is the clearest end-to-end single-file scheduler implementation in the workspace.

## CE 5.0 Classic Scheduler Plus Instrumentation

- `WINCE500/PRIVATE/WINCEOS/COREOS/NK/KERNEL/schedule.c:2302`
  - `NextThread()`
  - same housekeeping model as 4.2, now with split pending-event words and more instrumentation.
- `WINCE500/PRIVATE/WINCEOS/COREOS/NK/KERNEL/schedule.c:2456`
  - `KCNextThread()`
  - same dispatch policy,
  - adds CeLog and OEM reschedule notifications,
  - preserves the same strict-priority and same-priority round-robin rules.

## CE 7.0 Modern Dispatch Path

The later trees spread the hot path across scheduler helpers instead of putting everything in one function.

- `WINCE700/private/winceos/COREOS/nk/kernel/kcalls.c:433`
  - `MakeRun()`
  - chooses the actual run queue through `pRunQ`,
  - optionally applies a front-insert quantum penalty,
  - updates runnable timestamps,
  - cooperates with stub-thread and per-CPU affinity queue routing.
- `WINCE700/private/winceos/COREOS/nk/kernel/kcalls.c:553`
  - `RunqDequeue()`
  - removes the selected thread from the ready queue,
  - keeps process stub-thread state consistent when per-process ready queues are used.
- `WINCE700/private/winceos/COREOS/nk/kernel/kcalls.c:609`
  - `CalcQuantumLeft()`
  - decrements quantum,
  - emits quantum-expire instrumentation,
  - charges elapsed time to user or kernel accounting.
- `WINCE700/private/winceos/COREOS/nk/kernel/kcalls.c:665`
  - `BlockCurThread()`
  - converts the running thread into blocked state,
  - saves coprocessor state,
  - triggers reschedule.

## CE 7.0 Thread And Process Lifecycle Path

- `WINCE700/private/winceos/COREOS/nk/kernel/thread.c:1091`
  - `THRDCreate()`
  - allocates stacks,
  - sets up the thread object and architecture context,
  - returns a locked thread handle.
- `WINCE700/private/winceos/COREOS/nk/kernel/thread.c:1158`
  - `NKCreateThread()`
  - public wrapper around thread creation,
  - normalizes stack size,
  - duplicates a caller-visible handle,
  - resumes unless created suspended.
- `WINCE700/private/winceos/COREOS/nk/kernel/thread.c:1378`
  - `NKExitThread()`
  - marks thread dead/dying,
  - runs detach notifications,
  - coordinates with process exit and debugger logic,
  - transitions to final removal under scheduler control.
- `WINCE700/private/winceos/COREOS/nk/kernel/process.c:756`
  - `PROCTerminate()`
  - terminates the main thread or remote process,
  - blocks for completion when needed,
  - enforces process exit policy separately from thread dispatch.

## CE 7.0 Affinity And Backward-Compatibility Routing

- `WINCE700/private/winceos/COREOS/nk/kernel/thread.c:334-336`
  - newly created threads choose `pRunQ` based on affinity and backward-compatibility process rules.
- `WINCE700/private/winceos/COREOS/nk/inc/thread.h:123`
  - `ProcReadyQ` inside `STUBTHREAD` holds a per-process ready queue.
- `WINCE700/private/winceos/COREOS/nk/inc/process.h:81`
  - `pStubThread` in the process object enables BC scheduling behavior.
- `WINCE700/private/winceos/COREOS/nk/inc/process.h:110`
  - `FIRST_SMP_SUPPORT_VERSION` marks the version boundary where SMP-aware behavior becomes first-class.
- `WINCE700/private/winceos/COREOS/nk/inc/pcb_common.h:85`
  - each PCB owns an `affinityQ` for per-CPU runnable work.
- `WINCE700/private/winceos/COREOS/nk/kernel/loader.c:3960-4093`
  - process startup creates and installs a stub thread when BC process scheduling is needed.

See also `documents/scheduler_ce7_affinity.mmd` for a visual summary of this routing model.

## Reference Implementation In This Workspace

There is now a small host-runnable reference implementation derived from this study at:
- `my-loader/reference_schedproc/`

It contains:
- intrusive per-priority ready queues,
- ordered sleep queue,
- single-CPU dispatch logic,
- process/thread lifetime modeling,
- a demo you can compile and run with `make`.

## Bottom Line

Windows CE's scheduler is best understood as:
- a strict-priority thread scheduler,
- with round-robin sharing only inside a priority band,
- backed by explicit ready, sleep, and wait queues,
- designed for embedded responsiveness first,
- and evolved over time toward more generalized queue infrastructure and SMP-aware dispatch.

If you want a scheduler that is practical, understandable, and proven, the best CE-inspired design is:
- start with CE 4.2's conceptual model,
- implement it with CE 7.0's intrusive queue structure and hot-path discipline,
- then add CE 7.0 / 8.0 style per-CPU queues and affinity once the single-core version is stable.
