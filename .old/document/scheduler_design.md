# Custom Scheduler Design Derived From Windows CE

This document turns the Windows CE findings in `scheduler_overview.md` into a concrete scheduler design you can implement.

The design goal is not to clone Windows CE. It is to reuse its strongest ideas while avoiding legacy complexity that only exists for CE compatibility.

## Design Goals

Primary goals:
- deterministic priority scheduling,
- simple hot path,
- no allocation inside scheduling primitives,
- clean sleep, wake, and wait handling,
- compatibility with future SMP expansion.

Secondary goals:
- good observability,
- starvation resistance inside a priority band,
- lock-aware priority boost,
- a clean exit path for threads and processes.

## Design Choice Summary

Recommended choices:
- scheduling unit: thread,
- priority model: strict fixed priorities,
- time sharing: quantum only among equal-priority threads,
- ready queues: intrusive per-priority queues,
- sleep queue: ordered by wake time,
- wait queues: intrusive wait-node abstraction,
- lock policy: priority inheritance for mutex-like ownership,
- SMP path: per-CPU ready queues with optional affinity,
- accounting: per-thread user time, kernel time, quantum left, runnable timestamp, blocked timestamp.

## Scheduler Model

The scheduler should be split into three layers.

## 1. Data Layer

This is only the persistent state:
- thread object,
- process object,
- ready queues,
- sleep queue,
- wait queues,
- per-CPU scheduler state.

## 2. Housekeeping Layer

This handles:
- timer tick bookkeeping,
- wakeups,
- deferred unblock processing,
- recomputing effective priority when lock ownership changes,
- deciding whether a reschedule is required.

This corresponds to the role CE gives to `NextThread()` and later scheduler helpers.

## 3. Dispatch Layer

This handles only:
- picking the best runnable thread,
- deciding whether the current thread can keep running,
- updating current-thread bookkeeping,
- switching CPU context.

This should stay small.

## Suggested Structures

## Thread

Suggested C-like structure:

```c
typedef struct Thread Thread;
typedef struct WaitNode WaitNode;

struct Thread {
    uint32_t id;
    struct Process *owner;

    uint8_t base_prio;
    uint8_t cur_prio;
    uint8_t suspend_count;
    uint8_t wait_state;

    uint32_t state_bits;
    uint32_t quantum;
    uint32_t quantum_left;
    uint32_t wake_time;

    uint32_t user_time;
    uint32_t kernel_time;
    uint32_t time_when_runnable;
    uint32_t time_when_blocked;

    uint32_t affinity_mask;
    uint32_t seq_num;

    CpuContext context;
    WaitNode *wait_list;

    Thread *rq_prev;
    Thread *rq_next;
    Thread *sleep_prev;
    Thread *sleep_next;
};
```

Key lessons from CE reflected here:
- intrusive queue links live inside the thread,
- base and current priority are separate,
- quantum and remaining quantum are explicit,
- timestamps are built in for observability.

## Process

Suggested fields:

```c
typedef struct Process {
    uint32_t pid;
    uint32_t state;
    uint32_t thread_count;
    uint32_t affinity_mask;

    ThreadList threads;

    uint64_t exited_user_time;
    uint64_t exited_kernel_time;
};
```

Do not schedule the process directly.

Use the process for:
- lifetime control,
- address-space ownership,
- aggregate stats,
- process-wide exit behavior.

## Wait Node

This is the CE `PROXY` idea simplified for your system.

```c
struct WaitNode {
    Thread *thread;
    void *object;
    uint32_t wait_type;
    uint32_t result;
    uint8_t enqueue_prio;

    WaitNode *obj_prev;
    WaitNode *obj_next;
    WaitNode *thread_next;
};
```

Use one abstraction for:
- event waits,
- mutex waits,
- semaphore waits,
- timed waits,
- optionally wait-many.

## CPU / Scheduler State

For uniprocessor:

```c
typedef struct Scheduler {
    Thread *current;
    ReadyQueue ready[MAX_PRIORITY];
    SleepQueue sleepq;
    uint32_t now;
    uint32_t next_resched_time;
    SpinLock lock;
} Scheduler;
```

For SMP, evolve this into per-CPU scheduler state:
- one current thread per CPU,
- one ready queue set per CPU,
- optional global balancing,
- affinity filtering before enqueue.

## State Machine

Minimal thread states:
- created,
- suspended,
- runnable,
- running,
- blocked,
- sleeping,
- dying,
- dead.

These transitions should be explicit.
Avoid implicit state changes spread across unrelated code.

## Queue Design

## Ready Queues

Use one queue per priority.

Properties:
- O(1) enqueue to head or tail,
- O(1) dequeue head,
- fast check for highest non-empty priority.

Implementation choices:
- array of doubly-linked lists,
- plus a bitmap of non-empty priorities.

This is better than scanning all priorities every time.

Recommended layout:

```c
typedef struct ReadyQueue {
    Thread *head;
    Thread *tail;
} ReadyQueue;
```

and separately:

```c
uint64_t ready_bitmap[BITMAP_WORDS];
```

The CE 4.x hash table and CE 7.x intrusive `TWO_D_QUEUE` are both solving the same problem: cheap access to the highest-priority runnable thread.

## Sleep Queue

Use an ordered queue keyed by `wake_time`.

Recommended options:
- sorted linked list for simplicity,
- min-heap for scale.

If you are building a small kernel first, start with a sorted linked list.

Why:
- insertion cost is acceptable for small systems,
- wakeup path is trivial because head is always the next thread to wake.

## Wait Queues

Each synchronization object owns a wait queue.

Policy recommendation:
- for mutexes and critical sections, order by effective priority,
- for events/semaphores, priority-aware wakeup is usually still preferable,
- keep FIFO among equal priorities.

That matches the intent visible in CE.

## Scheduling Policy

## Rule 1: Higher Priority Always Wins

If a higher-priority runnable thread exists, preempt.

This gives you embedded-style responsiveness.

## Rule 2: Same Priority Uses Round Robin

If the current thread's quantum expires and there is another runnable thread at the same priority, move the current thread to the tail and run the peer.

## Rule 3: No Unnecessary Context Switch

If the current thread is still the best choice, keep it running.

This is important because context switches are expensive relative to queue checks.

## Rule 4: Quantum Is About Fairness Within A Priority Band

Do not use quantum to let lower-priority work steal time from higher-priority work.

## Rule 5: Reinsert Carefully

Copy the CE 7.0 idea that reinserting a thread at the front should still reduce some effective budget.

Reason:
- it prevents a pair of signaling threads from starving equal-priority peers.

You do not need to copy the exact CE logic, but you do want that protection.

## Core Algorithms

## Make Runnable

Pseudo-code:

```text
make_runnable(thread, at_tail):
  assert scheduler lock held
  if thread.suspend_count != 0:
    return

  if not at_tail and thread.quantum_left > 0:
    thread.quantum_left -= 1
    if thread.quantum_left == 0:
      at_tail = true

  thread.state = RUNNABLE
  thread.time_when_runnable = now
  enqueue ready[thread.cur_prio], thread, at_tail
  set ready bitmap bit

  if current is null or thread.cur_prio is better than current.cur_prio:
    request reschedule
```

## Block Current Thread

```text
block_current(reason):
  assert scheduler lock held
  update_quantum_and_accounting(current)
  current.state = BLOCKED
  current.time_when_blocked = now
  request reschedule
```

## Timer Tick

```text
on_timer_tick():
  lock scheduler
  now += tick

  if current exists and current.quantum > 0:
    decrement current.quantum_left
    if current.quantum_left == 0:
      request reschedule

  while sleepq.head and sleepq.head.wake_time <= now:
    t = pop sleepq.head
    clear sleeping flag
    make_runnable(t, true)

  unlock scheduler

  if reschedule requested:
    dispatch()
```

## Dispatch

```text
dispatch():
  lock scheduler

  best = highest_priority_runnable()

  if no best:
    best = idle_thread

  if current exists:
    update_quantum_and_accounting(current)

    if current.state == RUNNING:
      if best.priority is better than current.priority:
        make_runnable(current, false)
        switch_to(best)
      else if best.priority == current.priority and current.quantum_left == 0:
        make_runnable(current, true)
        switch_to(best)
      else:
        keep current
    else:
      switch_to(best)
  else:
    switch_to(best)

  unlock scheduler
```

## Synchronization And Priority Inheritance

You should copy this concept from CE almost verbatim.

For mutex-like ownership:
- each mutex knows its owner,
- each owner tracks owned mutexes,
- the owner's effective priority is the best priority among:
  - its base priority,
  - the highest waiting priority on owned locks.

When a waiter blocks on a mutex:
- recompute owner effective priority,
- if owner priority improves, reschedule if needed.

When the mutex is released:
- wake the next waiter,
- recompute old owner's priority,
- recompute new owner's priority if handoff is immediate.

This solves the most common inversion problem in a strict-priority scheduler.

## Process Semantics

Recommended process model:
- process owns threads,
- process exit is driven by main-thread exit or explicit terminate,
- when the main thread exits:
  - mark process exiting,
  - stop new thread creation,
  - terminate or join secondary threads,
  - then destroy address-space resources.

This is the clean lesson from CE.

What not to copy unless you need it:
- CE's stub-thread compatibility machinery,
- loader/debugger/token coupling.

## SMP Roadmap

When you move from one CPU to multiple CPUs:

Phase 1:
- one ready queue set per CPU,
- fixed affinity mask per thread,
- schedule only from the local CPU queue.

Phase 2:
- add wakeup placement logic,
- enqueue to the target CPU queue based on affinity and current load.

Phase 3:
- add load balancing,
- add work stealing or periodic rebalance if needed.

The CE 7.0 / 8.0 affinity queue design is a good reference for this progression.

## Instrumentation You Should Build In Immediately

Record at least these events:
- thread create,
- thread resume,
- thread block,
- thread sleep,
- thread wake,
- quantum expire,
- context switch,
- thread exit,
- process exit.

Track counters / timestamps for:
- runnable duration,
- blocked duration,
- number of preemptions,
- number of quantum expiries,
- maximum wait latency.

This is cheap to add early and hard to retrofit later.

## Suggested Implementation Order

1. Build the thread object and a single ready queue set.
2. Add strict priority scheduling without sleeping.
3. Add quantum-based round-robin for equal priorities.
4. Add sleep queue and timer wakeups.
5. Add blocking on one synchronization primitive.
6. Generalize waits with a `WaitNode` abstraction.
7. Add priority inheritance.
8. Add process lifetime rules.
9. Add per-thread accounting and instrumentation.
10. Add SMP-aware per-CPU queues and affinity.

## Recommended Minimal v1

If you want a first implementation that is strong but not overbuilt, make v1 include only this:
- strict priorities,
- per-priority ready queues,
- quantum round-robin inside one priority,
- ordered sleep queue,
- event + mutex wait support,
- priority inheritance,
- idle thread,
- basic logging.

That already captures the best of the CE design.

## Final Recommendation

The best scheduler to build from this source tree is:
- conceptually CE 4.2,
- structurally CE 7.0,
- with CE 8.0's extra state richness added later when you need it.

That combination gives you:
- a scheduler you can reason about,
- a fast hot path,
- realistic synchronization behavior,
- and a clean path to SMP.