# User-Exe Loader For Kids

This note explains your `user-exe` loader in a very easy way.

It matches the live code in:

- `kernel/include/user-exe.h`
- `kernel/user-exe.c`
- `kernel/service-call.c`
- `kernel/process.c`

## The Big Idea

Imagine you have a toy box on disk.

Inside that toy box is:

- a small instruction card
- several bags of toy pieces

Your kernel loader is the helper that:

1. opens the toy box
2. reads the instruction card
3. prepares empty rooms in memory
4. puts each bag of toys into the correct room
5. tells the CPU where to start running

That is what `exec_user_program()` does.

## What Is A User Executable Here?

Your kernel does not load a normal file in a random way.

It loads a custom packed format called:

```text
ROSXEXE
```

That magic name is defined in `kernel/include/user-exe.h`.

So your executable file starts with a header that says:

- "I am a real ROSXEXE file"
- "Here is my version"
- "Here is my entry point"
- "Here are my segments"

## What Is In The Header?

The header is `UserExeHeader`.

Think of it like a lunchbox label.

It tells the loader:

- where the program should start
- how many pieces the program has
- where each piece belongs in memory

Those pieces are called **segments**.

Each segment says things like:

- where its bytes are in the file
- where it should live in virtual memory
- how many bytes to copy from the file
- how much memory it needs in total
- whether it is code, read-only data, or writable data

## What Is A Segment?

A segment is just one chunk of the program.

For example:

- code segment
- read-only data segment
- writable data segment
- zero-filled extra memory

Think of a segment like a bag of Lego pieces that belongs in one room.

## The Loader Story

The real loader is:

```text
exec_user_program(path)
```

It lives in `kernel/user-exe.c`.

Here is the story in simple steps.

## Step 1: Open The Program File

The loader gets a path like:

```text
/bin/init.exe
```

Then it opens the file using the VFS.

Child version:

> "Go find the toy box on the shelf and open it."

If the file cannot be opened, loading stops.

## Step 2: Read The Header

The loader reads the fixed header from the start of the file.

That header is like the instruction card taped to the front of the toy box.

It says:

- this is a real program
- how many segments there are
- where the program starts running

If the header is wrong, the loader stops.

## Step 3: Check The Header

The loader checks things like:

- is the magic really `ROSXEXE`?
- is the version correct?
- is the header size correct?
- does the segment table look sane?
- are the segment sizes valid?

Child version:

> "Before building anything, make sure the instruction card is not fake or broken."

## Step 4: Clean The Old User Space

Before loading the new program, the kernel cleans the current task's old user memory.

That work is done by `process_reset_user_space()` in `kernel/process.c`.

It removes:

- old code pages
- old data pages
- old heap pages
- old user stack pages
- old page-table pages for that task

Child version:

> "The old child moved out of the house. Clean all the rooms first."

## Step 5: Make A Fresh Stack Page

Every user program needs a stack.

So the loader:

1. asks for one fresh physical page
2. maps it to the user stack virtual address

Child version:

> "Put one empty mattress in the stack room so the child has a place to stand."

## Step 6: Load Every Segment

Now the loader goes through all segments one by one.

This happens in `exec_load_segment()`.

For each segment it does four important things.

### 6a. Understand The Segment

The segment says:

- where it belongs in memory
- how big it is in the file
- how big it should be in memory
- what permissions it needs

Permissions become:

- `PE_USER_CODE` for executable code
- `PE_USER_DATA` for writable data
- `PE_USER_RO` for read-only data

So the loader first decides what kind of room this segment needs.

### 6b. Create Enough Pages

The loader makes sure all pages touched by that segment exist.

That work is done by `exec_map_segment_range()`.

It walks page by page:

1. round the segment start down to a page boundary
2. check if that page is already mapped
3. if not, allocate a physical page
4. map it into the task at the correct virtual address

Child version:

> "Before carrying toys into the room, make sure the room exists."

## Visual: Building The Rooms First

```text
segment says it needs memory from 0x4000 to 0x6ABC

loader creates pages:

0x4000 - 0x4FFF
0x5000 - 0x5FFF
0x6000 - 0x6FFF
```

Even if only part of the last page is used, the whole page must exist.

### 6c. Seek To The Right File Position

The segment also tells the loader where its bytes are inside the executable file.

So the loader moves the file cursor to that place.

Child version:

> "Open the toy box and find the correct bag of pieces."

### 6d. Copy Bytes Into Memory

Then the loader copies the segment's file bytes into the mapped pages.

It does this carefully:

- page by page
- chunk by chunk
- using the page offset inside each page

So if a segment starts in the middle of a page, the loader still puts the bytes in the correct place.

## What If Memory Size Is Bigger Than File Size?

That is normal.

Example:

- file size = 3000 bytes
- memory size = 5000 bytes

That means:

- copy the first 3000 bytes from the file
- leave the extra 2000 bytes as zero

Child version:

> "The toy box only brought 3000 real pieces, but the room should hold 5000 spaces. Leave the rest empty and clean."

## Step 7: Set The Program Counter And Stack Pointer

Once all segments are loaded, the kernel prepares the saved user registers.

It writes:

- `pc = header.entry_point`
- `sp = VA_USER_STACK`
- `pstate = EL0`

This means:

- `pc` tells the CPU where to begin running the program
- `sp` tells the CPU where the top of the user stack is

Child version:

> "Put the child at the front door and point to the first instruction to follow."

## Step 8: Turn On The New Page Tables

After the new memory map is ready, the kernel installs the task's page tables.

That means the CPU can now see:

- the new stack page
- the new code pages
- the new data pages

Now the user program can run.

## The Whole Story In One Picture

```text
disk file: /bin/app.exe

+------------------------------------------+
| header | segment table | segment bytes   |
+------------------------------------------+

            |
            v

1. open file
2. read header
3. validate header
4. clear old user memory
5. map stack page
6. for each segment:
   - create pages
   - seek to file bytes
   - copy bytes into memory
7. set entry PC and stack SP
8. install page tables

            |
            v

user program starts running
```

## What `spawn` Means

Your kernel also has:

```text
spawn_user_program(path, name)
```

This is different from `exec_user_program()`.

### `exec`

`exec` means:

> "Take the current task and replace its user program with a new one."

So it does **not** create a new task.

It reuses the current one.

### `spawn`

`spawn` means:

> "Make a new child task, give it a program path and a name, then let that child exec the program."

So:

1. create child task
2. store child path and name
3. child starts in kernel code
4. child calls `exec_user_program()`
5. child becomes a user process

## Visual: `spawn` vs `exec`

```text
exec:
current task
   -> throw away old user image
   -> load new user image
   -> same task, new program

spawn:
parent task
   -> create child task
   -> child calls exec
   -> child becomes new user process
```

## What About Shared Libraries?

Your loader also supports shared libraries.

That is a second story in the same file:

- load one shared library once into global physical pages
- map those pages into tasks that need them
- optionally give each task its own DLL-local writable block

But the main executable loader is still the simple flow above:

- open
- check
- map
- copy
- start

## A Super Simple Child Story

Here is the loader as a bedtime story:

> The kernel opens the program box.
> It reads the instruction card.
> It cleans the old room.
> It builds new rooms in memory.
> It carries each bag of program bytes into the right room.
> It puts the child at the start line.
> Then it says, "go run."

## The 5 Most Important Things To Remember

If you only remember 5 things, remember these:

1. `ROSXEXE` files start with a header that tells the loader what to do.
2. Each segment says where its bytes should go in virtual memory.
3. The loader creates pages first, then copies bytes into them.
4. `exec` replaces the current task's user image.
5. `spawn` creates a child task that later `exec`s a program.

## One-Line Summary

Your user-exe loader is the kernel helper that opens a packed program file, reads its instructions, builds the right memory pages, copies each segment into place, and then points the CPU at the program's entry point.