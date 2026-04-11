# Kernel Module Explanation

## Imagine The Kernel Is A Big Robot Brain

Think of the kernel as the main brain of your OS robot.

It already knows how to do important things:

- talk to memory
- talk to devices
- run programs
- read files

But sometimes we want to give the robot a new skill without rebuilding the whole brain.

That new skill is called a **kernel module**.

A kernel module is like a small tool box the kernel can pick up at boot time.

Example:

- a driver for a new device
- a background service
- a helper that logs information

---

## What A Module File Looks Like

The module lives in a file like:

```text
/system/sample_sys.sys
```

That `.sys` file is a package with 3 important parts:

1. a small header
2. a manifest
3. the real machine code

### 1. Header

The header is like the label on a lunch box.

It says:

- "I am really a kernel module"
- my version
- my name
- where the real code is inside the file

If the label looks wrong, the kernel refuses to load it.

### 2. Manifest

The manifest is a tiny description file.

It says things like:

- module name
- entry function name
- optional capabilities

For example:

```json
{
  "name": "sample_sys",
  "entry": "sample_sys_module_entry"
}
```

### 3. ELF Code

This is the real compiled program code for the module.

It contains:

- instructions the CPU can run
- read-only data
- writable data

---

## Module Lifecycle

Lifecycle means: the full story from start to finish.

Here is the module life story:

1. The kernel boots.
2. The file system is mounted.
3. The kernel looks inside `/system/`.
4. It finds every file ending in `.sys`.
5. It checks if each file is a valid module package.
6. It loads the module code into memory.
7. It finds the module's entry function.
8. It calls the entry function.
9. The module gives the kernel its callbacks like `init()` and `idle()`.
10. The kernel runs `init()` once.
11. Later, during normal kernel looping, the kernel may call `idle()` again and again.

Right now there is no real unload step yet, so modules mostly stay alive until reboot.

---

## Step By Step Load Procedure

### Step 1: Find The File

The kernel opens `/system/` and checks every file.

If a file ends with `.sys`, it tries to load it.

This happens in the module loader during boot.

### Step 2: Read The Header

The kernel reads the first bytes of the file.

It checks:

- magic name: is this really a module bundle?
- version: do we understand this version?
- sizes: are the numbers sensible?
- name and entry symbol: are they present?

If anything is broken, the kernel skips that file.

This is why you saw:

```text
Skipping legacy or invalid module image
```

That happened because the loader used to expect the old format.

Now it understands the new module bundle format.

### Step 3: Read The Embedded ELF

Inside the package is the real compiled module code.

The kernel reads that ELF image into memory.

Then it checks:

- is it AArch64?
- is it a shared object (`ET_DYN`)?
- are the tables inside the file valid?

If not, load fails.

### Step 4: Create A Small Home For The Module

The kernel gives the module its own mapped memory pages.

Think of this like giving the module its own room inside the robot brain.

The loader copies:

- code pages
- read-only data
- writable data

into that room.

### Step 5: Find The Entry Function

The manifest says which function is the front door.

Example:

```c
int sample_sys_module_entry(const RosKernelModuleApi* api, RosKernelModuleExports* exports)
```

The kernel looks inside the ELF symbol table to find that function address.

If it cannot find it, loading stops.

### Step 6: Give The Module A Tool Belt

Before calling the module, the kernel prepares an API table.

This is like handing the module a little tool belt.

Right now it includes helpers like:

- log messages
- allocate memory
- free memory
- read current tick time

So the module does not need to know every secret inside the kernel.

### Step 7: Call The Entry Function

The kernel jumps into the module's entry function.

The module receives:

- `api`: tools the kernel offers
- `exports`: empty slots to fill in

The module fills in things like:

- `init`
- `shutdown`
- `idle`

If the module says "I don't understand this ABI" or returns an error, loading fails.

### Step 8: Run `init()`

If the module provided an `init()` callback, the kernel calls it once.

This is where the module can say:

- "hello"
- set up counters
- prepare internal state

For `sample_sys`, this is where it logs that the sample module was initialized.

### Step 9: Run `idle()` Later

The kernel main loop keeps running forever.

On each loop, it may call the module's `idle(now_ms)` function.

This is for small background work.

Important:

- `idle()` must be quick
- it must return
- it should not block forever

So think of `idle()` like: "do a tiny chore, then give control back."

---

## What The Entry Function Really Does

Here is the idea:

```c
int sample_sys_module_entry(const RosKernelModuleApi* api, RosKernelModuleExports* exports)
{
    if (!api || !exports) {
        return -1;
    }

    exports->init = sample_sys_init;
    exports->idle = sample_sys_idle;
    return 0;
}
```

That means:

- check the inputs are real
- remember the tools from the kernel
- tell the kernel which functions to call later

It is like introducing yourself and saying:

"Hi kernel, when you want me to start, call `init()`. When you want me to do background work, call `idle()`."

---

## Why We Switch Into The Module's Memory

The kernel does not just call random addresses blindly.

The module code was copied into its own mapped address space.

So before calling module code, the kernel temporarily switches to the module's page table.

That means:

- the module's code pages are visible
- the module's data pages are visible

Then after the call finishes, the kernel switches back.

Think of it like:

1. open the module's room
2. step inside
3. talk to the module
4. step back out

---

## What Works Right Now

The current loader can do these things:

- discover `.sys` files automatically
- validate the new module bundle header
- read the embedded ELF image
- map loadable segments into memory
- find the entry function by name
- call `entry()`
- call `init()`
- call `idle()`

So `sample_sys` can now load and run at boot.

---

## What Does Not Work Yet

Some advanced things are still missing.

### No relocation support yet

If a module needs extra ELF fix-ups, the loader cannot handle that yet.

That means only simple modules are safe right now.

### No kernel symbol imports yet

We are not yet resolving lots of external kernel symbols from the ELF.

Instead, the module should use the API table handed to it by the kernel.

### No unload yet

The kernel does not fully remove a module and clean everything up at runtime yet.

For now, modules are basically "load and stay loaded."

---

## A Simple Story Version

Here is the whole thing in one tiny story:

1. The kernel opens the `/system` toy box.
2. It finds a module package.
3. It checks the package label.
4. It opens the package and pulls out the code.
5. It gives the module a little room in memory.
6. It asks, "Where is your front door function?"
7. It knocks on that door by calling the entry function.
8. The module says, "Call my `init()` first, then call my `idle()` later."
9. The kernel does that.
10. The module becomes part of the running system.

---

## Where This Lives In The Code

Important files:

- `kernel/module/module_loader.c`
- `kernel/module/module_validate.c`
- `kernel/module/module_format.h`
- `kernel/include/module.h`
- `applications/include/app/kernel_module.h`
- `applications/system/sample_sys/module.c`
- `applications/system/sample_sys/module.json`

---

## Final Tiny Summary

A kernel module is a mini-program that the kernel loads during boot.

The kernel:

- finds it
- checks it
- copies it into memory
- calls its entry function
- runs its startup code
- lets it do tiny background work later

So it is basically:

**"plug in a new kernel skill while the OS starts."**

---

## Where Modules Live In Memory

The kernel reserves one special area of **kernel virtual address space** just for modules.

That area is:

```text
MODULE_REGION_BASE  = 0xFFFF000010000000
MODULE_REGION_LIMIT = 0xFFFF000011000000
MODULE_REGION_SIZE  = 16 MB
```

So all modules live inside this one big 16 MB neighborhood.

Think of it like a long apartment hallway.

- the hallway starts at `0xFFFF000010000000`
- the hallway ends at `0xFFFF000011000000`
- each module gets one apartment inside the hallway

### Visual Picture

```text
Kernel virtual address space

... kernel text/data/heap ...
          |
          v

0xFFFF000010000000  +--------------------------------------+
                    | Module region begins                 |
                    |                                      |
                    | sample_sys                           |
                    |   .text                              |
                    |   .rodata                            |
                    |   .data                              |
                    |   .bss                               |
                    |--------------------------------------|
                    | net_driver                           |
                    |   .text                              |
                    |   .rodata                            |
                    |   .data                              |
                    |   .bss                               |
                    |--------------------------------------|
                    | sensor_module                        |
                    |   .text                              |
                    |   .rodata                            |
                    |   .data                              |
                    |   .bss                               |
                    |--------------------------------------|
                    | more modules...                      |
                    |                                      |
0xFFFF000011000000  +--------------------------------------+
                    | Module region ends                   |
```

So modules are:

- inside kernel address space
- not in user space
- all together in one reserved area

---

## How The Kernel Chooses Each Module Address

The kernel uses a very simple idea.

It keeps a pointer called:

```text
module_region_next
```

At boot it starts here:

```text
module_region_next = MODULE_REGION_BASE
```

When a new module loads:

1. the kernel checks how big the module image is
2. it rounds for alignment
3. it places the module at the next free address
4. it moves `module_region_next` forward

So modules are packed one after another.

### Example

```text
Start:
module_region_next = 0xFFFF000010000000

Load module A, size 0x3000:
A base = 0xFFFF000010000000
next   = 0xFFFF000010003000

Load module B, size 0x5000:
B base = 0xFFFF000010003000
next   = 0xFFFF000010008000

Load module C, size 0x2000:
C base = 0xFFFF000010008000
next   = 0xFFFF00001000A000
```

This is called a **bump allocator**.

It is simple and fast.

But it also means:

- modules are loaded in order
- there is no smart packing
- there is no moving things around later

---

## What Is Inside One Module's Memory Area

A module usually has these parts:

- `.text` = code
- `.rodata` = read-only constants
- `.data` = writable data with initial values
- `.bss` = writable data that starts as zero

### One Module Picture

```text
sample_sys base address
0xFFFF000010000000

+---------------------------+
| .text    executable code  |
+---------------------------+
| .rodata  strings/constants|
+---------------------------+
| .data    writable values  |
+---------------------------+
| .bss     zero-filled data |
+---------------------------+
```

At first, pages may be writable while the loader copies bytes in.

After loading:

- code becomes executable
- read-only data becomes read-only
- writable data stays writable

That helps protect the module a little bit.

---

## Are Modules Isolated From Each Other?

Not really.

This is very important.

All modules live in the same kernel EL1 address space.

That means:

- the kernel can see all modules
- one module can accidentally damage memory if it is buggy
- there is no strong sandbox between modules

So the module region is organized, but not safely isolated like separate user processes.

Think of it like many people sharing one workshop.

Each person has a table, but there is no locked wall between the tables.

---

## What Happens If We Have 10+ Modules?

Having 10 modules is not automatically a problem.

But there are limits.

### Limit 1: Maximum number of modules

Right now the code only allows:

```text
MODULE_MAX_COUNT = 16
```

So:

- 1 module: okay
- 10 modules: okay
- 16 modules: maximum
- 17 modules: load fails

The kernel has fixed-size arrays for module bookkeeping.

So after 16, there is no room to remember more modules.

---

## Limit 2: The module region is only 16 MB

All modules together must fit inside:

```text
0xFFFF000010000000 .. 0xFFFF000011000000
```

That is only 16 MB total.

So if your modules are tiny:

- 10 modules may fit easily

If your modules are big:

- maybe only 3 or 4 fit

### Example

If each module is about 1 MB:

- about 10 modules = about 10 MB
- still okay

If each module is about 2 MB:

- 10 modules = about 20 MB
- too big
- later modules fail to load

When the region is full, the loader cannot reserve more space.

That means load fails.

---

## Limit 3: Each module has page bookkeeping limits

The loader also has a per-module page tracking limit.

So even if the whole 16 MB region is not full yet, one very large module can still fail if it needs too many pages to track.

So there are really two size problems:

- total region full
- one module too large

---

## What The Kernel Does With 10 Idle Modules

If many modules provide an `idle()` callback, the kernel checks them in the main loop.

That means:

1. kernel loop runs
2. it walks through loaded modules
3. if a module has `idle()`, it may call it

So 10+ modules means 10+ possible background callbacks every cycle.

That is okay only if each idle callback is short.

If modules do too much work in `idle()`:

- system becomes slower
- boot feels heavier
- the kernel spends more time doing module chores

So a good idle callback should be:

- tiny
- quick
- non-blocking

---

## What Happens When A New Module Cannot Fit?

If a new module cannot fit, the kernel should refuse to load it.

That means:

- old modules stay loaded
- new module does not get a memory slot
- kernel logs an error

Think of it like a bookshelf.

If there is no space for one more book:

- you do not throw away all books
- you just cannot place the new book

---

## Does The Kernel Reuse Space?

Not yet.

Right now the region allocator is very simple.

It only moves forward.

So:

- load module A
- load module B
- load module C

The next free address keeps moving upward.

There is no smart system yet for:

- unload a module
- reclaim the hole
- move another module into that hole

So if unload is added later, memory management will need to become smarter.

---

## Simple 3-Module Example

Imagine:

- `sample_sys` uses 64 KB
- `disk_driver` uses 128 KB
- `net_driver` uses 256 KB

Then the module region may look like:

```text
0xFFFF000010000000  sample_sys
0xFFFF000010010000  disk_driver
0xFFFF000010030000  net_driver
0xFFFF000010070000  next free space
```

The exact numbers depend on alignment and section sizes, but the idea is:

- one after another
- always inside the module region

---

## Super Short Version

Where are modules loaded?

- into one reserved kernel virtual-memory region
- from `0xFFFF000010000000` to `0xFFFF000011000000`

What happens with 10+ modules?

- they keep getting placed one after another
- they all share the same kernel address space
- they must fit inside 16 MB total
- only 16 modules are supported right now
- too many or too-large modules will fail to load
