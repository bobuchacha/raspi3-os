# Tasks And Process Lifecycle For Kids

This note explains your kernel's **tasks**, **processes**, and how they live from the moment they are created until they end.

It also explains:

- what `start_thread_context` is
- why the kernel does not just jump directly into the new task

This matches the live code in:

- `kernel/include/task.h`
- `kernel/process.c`
- `kernel/scheduler/scheduler.c`
- `kernel/arch/cortex-a53/scheduler.S`
- `kernel/user-exe.c`

## The Big Idea

Imagine the kernel is a school.

- A **task** is one student record in the school office.
- The **scheduler** is the teacher deciding whose turn it is.
- The **CPU** is the playground.
- A **kernel thread** is a student doing teacher work inside the school.
- A **user process** is a student sent outside to play in user space.

So the kernel's real working object is:

```text
Task
```

Everything else is built around that.

## What Is A Task?

In your code, a task is `struct task_struct`.

It stores important things like:

- saved CPU registers
- task id
- running, sleeping, or zombie state
- timeslice counter
- priority
- memory bookkeeping
- kernel stack page
- user-visible name

So a task is like one complete little record that says:

> "Here is who I am, where I was running, and what memory belongs to me."

## What Is The Scheduler Doing?

The scheduler looks at all tasks and picks the next one that should run.

The tasks live in:

```text
tasks[]
```

And the current running one is:

```text
current_task
```

Child version:

> The teacher looks at all students and says, "now it is your turn."

## Task States

Your tasks can be:

- `TASK_RUNNING`
- `TASK_SLEEPING`
- `TASK_ZOMBIE`

That means:

- **running**: ready to use the CPU
- **sleeping**: waiting for time to pass
- **zombie**: finished running, waiting to be cleaned up

Child version:

- running = awake and ready
- sleeping = napping
- zombie = done, but the classroom has not been cleaned yet

## What Is A Process In Your Kernel?

In your kernel, a process is basically a task that has:

- user memory pages
- a user stack
- user page tables
- a user entry point

So a process is not a totally different creature.

It is still a task.

It is just a task that has been prepared to run user code.

## What Is A Kernel Thread?

A kernel thread is also a task.

But instead of starting in user code, it starts by calling a kernel function.

Child version:

> Some students work inside the school building.
> Those are kernel threads.

## What Happens When A New Task Is Created?

Your kernel creates a new task in functions like:

- `process_copy_thread()`
- `create_user_process()`

The creation story is:

1. ask memory manager for a page for the task object
2. ask memory manager for a page for the kernel stack
3. clear the memory
4. fill in the task fields
5. set up saved CPU registers
6. put the task into `tasks[]`
7. let the scheduler run it later

## Visual: Creating A Task

```text
kernel wants a new task
    |
    v
allocate task page
allocate kernel stack page
fill task struct
set initial CPU context
put task into tasks[]
    |
    v
scheduler may run it later
```

## Why The Task Does Not Run Right Away

This is the most important idea.

When you create a task, the kernel does **not** immediately jump into it.

Instead it only prepares the task so that **later**, when the scheduler picks it, the normal context-switch code can enter it safely.

Child version:

> When a new student joins class, the teacher writes their name on the list first.
> The teacher does not instantly throw them onto the playground.
> They wait until it is really their turn.

## Why `start_thread_context` Exists

`start_thread_context` is the special starting doorway for a brand-new task.

When a new task is created, your code does this:

- it sets the new task's saved `pc` to `start_thread_context`

That means:

> "The first time this task ever runs, start at `start_thread_context`."

So `start_thread_context` is like the **welcome desk** for brand-new tasks.

## Why Not Just Jump Directly Into The Task?

Because a new task must enter the system the same safe way as every other scheduled task.

If the kernel jumped directly into the task at creation time, many things would go wrong.

### Problem 1: The Scheduler Would Be Skipped

The scheduler is the one that decides whose turn it is.

If you directly jumped into the task during creation, you would be cheating the scheduler.

That would break the normal rule:

> only the scheduler chooses the next task

### Problem 2: CPU Context Must Be Switched Cleanly

Tasks need:

- correct kernel stack
- correct saved registers
- correct current task pointer
- correct page tables

The normal way to do that is through:

```text
cpu_switch_to()
```

and then the new task begins at `start_thread_context`.

That keeps the world tidy.

### Problem 3: Kernel Threads And User Tasks Need Different First Steps

Some new tasks are kernel threads.
Some new tasks are going to end up in user mode.

`start_thread_context` is the tiny traffic police officer that checks:

- should I call a kernel function?
- or should I return to user mode?

That is why one common entry point is useful.

## What `start_thread_context` Actually Does

The first time a new task runs, `start_thread_context` does this:

1. finish scheduler bookkeeping with `schedule_tail`
2. check register `x19`
3. if `x19 != 0`, call that kernel function
4. if `x19 == 0`, go to `switch_to_user_mode`
5. `switch_to_user_mode` performs `exit_to_user_mode` to enter user mode

So `start_thread_context` is the launcher that decides how the new task begins life.

## Visual: First Run Of A New Task

```text
new task is created
    |
    v
saved PC = start_thread_context
    |
    v
scheduler picks task
    |
    v
cpu_switch_to()
    |
    v
start_thread_context
    |
    +--> x19 != 0 ? call kernel function
    |
    +--> x19 == 0 ? return to user mode
```

## What Is `x19` Being Used For?

In your kernel, `x19` is used as a little hidden note for a new task.

It says what the task should do first.

For a new kernel thread, creation code puts a function pointer into `x19`.

Then `start_thread_context` says:

> "Ah, there is a function in `x19`. I should call it."

If `x19` is zero, then it means:

> "This task is ready to go to user mode instead."

## Kernel Thread Path

For a kernel thread, creation looks like this:

1. create task
2. set `x19 = kernel function`
3. set `x20`, `x21`, maybe more args
4. set saved `pc = start_thread_context`
5. scheduler eventually runs task
6. `start_thread_context` calls the kernel function

So the kernel thread begins in a very controlled way.

## User Process Path

For a user process, the flow is a little different.

Usually the task begins as a kernel-side setup path first.

Then some loader code prepares:

- user page tables
- user stack
- user program counter
- user pstate

After that, when `start_thread_context` goes to `switch_to_user_mode`, `exit_to_user_mode` restores the saved user registers and drops into EL0.

Child version:

> First the teacher helps the child put on their shoes and backpack.
> Then the teacher opens the outside door and lets them go play.

## How A Task Gets Into The Scheduler

After creation, the new task is placed into the global task list:

```text
tasks[pid] = new_task
```

Then it sits there as `TASK_RUNNING` until the scheduler gives it a turn.

The scheduler does not care whether it is:

- a kernel thread
- a future user process
- a sleeping task that just woke up

It only sees:

> "Here is a runnable task."

## How A Task Is Switched In

When the scheduler picks a task, it calls:

```text
schedler_switch_to(next)
```

That function:

1. changes `current_task`
2. installs the task's page tables with `set_pgd(next->mm.pgd)`
3. calls `cpu_switch_to(prev, next)`

Then the assembly code saves the old task's registers and restores the new task's registers.

Child version:

> The teacher puts one child's notebook away and opens the next child's notebook.

## Visual: Normal Switching

```text
current task = A
next task    = B

scheduler chooses B
    |
    v
schedler_switch_to(B)
    |
    v
save A registers
load B registers
switch page tables
resume where B left off
```

For a brand-new task, where it "left off" is `start_thread_context`.

## What Happens When A Task Sleeps?

If a task sleeps:

1. kernel marks it `TASK_SLEEPING`
2. writes the wake-up tick
3. scheduler picks someone else

Later the timer tick code wakes it when the time comes.

So sleeping tasks are still real tasks.
They are just waiting.

## What Happens When A Task Ends?

When a task is done, it does **not** immediately erase itself.

Instead:

1. it becomes `TASK_ZOMBIE`
2. it yields the CPU
3. later `cleanup_zombie_processes()` frees its pages
4. then it is removed from `tasks[]`

Child version:

> The student is done, but the room still has their books in it.
> A cleaner comes later and removes everything safely.

## Why Not Free Everything Immediately?

Because the task is still standing on its own kernel stack at the moment it says "I am done".

If it freed everything instantly, it could destroy the ground under its own feet.

So the kernel uses a safer plan:

- mark as zombie first
- switch away
- clean up later from another running context

That is why zombie cleanup exists.

## The Full Life Story

Here is the whole task lifecycle as one story.

```text
1. kernel creates task
2. kernel allocates task page and stack page
3. kernel fills task struct
4. task saved PC is set to start_thread_context
5. task is put into tasks[]
6. scheduler eventually picks it
7. cpu_switch_to restores its context
8. task starts at start_thread_context
9. start_thread_context either:
   - calls a kernel function, or
   - returns to user mode
10. task runs
11. task may sleep, wake, yield, or exec
12. task ends and becomes zombie
13. cleanup_zombie_processes frees its memory
14. tasks[] entry is cleared
```

## The Simplest Mental Model

If you want one simple picture, use this:

```text
task = the kernel's little running record

start_thread_context = the first doorway for a brand-new task

scheduler = the chooser that decides when a task gets a turn

zombie cleanup = the janitor that cleans finished tasks later
```

## The Short Answer To Your Question

### "Why do we use `ret_from_fork`?"

Because every new task needs one safe, common first entry point after the scheduler switches to it.

### "Why don't we just load the task directly?"

Because direct jumping would skip the scheduler's normal switch path, skip safe register restoration, and make it harder to cleanly support both kernel-thread start and user-mode start.

`ret_from_fork` makes the first run of every new task neat and predictable.

## One-Line Summary

Your kernel creates a task, puts it on the scheduler's list, and the first time it ever runs it enters through `ret_from_fork`, which safely decides whether that task should begin as a kernel worker or return to user mode.