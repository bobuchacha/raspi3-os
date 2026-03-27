# ARM64 Page Tables For Kids

This note explains what `PGD`, `PUD`, `PMD`, and `PTE` mean in your ARM64 memory management code like you are a child.

It matches the live code in:

- `kernel/include/arch/cortex-a53/mmu/pte.h`
- `kernel/arch/cortex-a53/mmu.c`

## The Big Idea

Imagine memory is a giant city.

- A **virtual address** is a house address written on paper.
- A **physical address** is the real house in the real city.
- The **MMU** is the mailman.
- **Page tables** are the mailman's address books.

So when a program says:

> "I want address `0x401234`"

the MMU says:

> "Let me check my address books and find the real memory page for that address."

## Why There Are So Many Levels

The city is too big for one giant notebook.

So the MMU uses **4 smaller notebooks**, one inside another:

1. `PGD`
2. `PUD`
3. `PMD`
4. `PTE`

Then finally it reaches the real 4 KB memory page.

Think of it like this:

```text
Virtual address
    |
    v
+--------+
|  PGD   |  "Which big area of the city?"
+--------+
    |
    v
+--------+
|  PUD   |  "Which neighborhood?"
+--------+
    |
    v
+--------+
|  PMD   |  "Which street?"
+--------+
    |
    v
+--------+
|  PTE   |  "Which house on that street?"
+--------+
    |
    v
Real 4 KB physical page
```

## What The Names Mean

These names are the page-table levels:

- `PGD` = Page Global Directory
- `PUD` = Page Upper Directory
- `PMD` = Page Middle Directory
- `PTE` = Page Table Entry

The last one, `PTE`, is the one that finally points to a real page of memory.

## Your ARM64 Numbers

In your code, the important constants are:

- `MM_PAGE_SHIFT = 12`
- `MM_PTRS_PER_TABLE = 512`
- `MM_PGD_SHIFT = 39`
- `MM_PUD_SHIFT = 30`
- `MM_PMD_SHIFT = 21`
- `MM_PAGE_MASK = 0xFFFFFFFFFFFFF000`

That means:

- one page is `2^12 = 4096` bytes = 4 KB
- each table has `512` entries
- each level uses `9` bits to choose one entry, because `512 = 2^9`

So a virtual address gets split up like this:

```text
| PGD index | PUD index | PMD index | PTE index | page offset |
|  9 bits   |  9 bits   |  9 bits   |  9 bits   |   12 bits   |
```

The last `12` bits are the position *inside* the 4 KB page.

## What Each Level Covers

Because each level picks one of 512 entries, each level covers a different chunk size.

### 1. Page Offset

The last `12` bits choose a byte inside one 4 KB page.

```text
0x.... .... .... .... .... .... .... ABCD
                                     ^^^^
                                somewhere inside the page
```

### 2. PTE Level

The `PTE` chooses which 4 KB page we want.

One `PTE` entry maps:

- `4 KB`

### 3. PMD Level

The `PMD` chooses which group of pages.

One `PMD` entry covers:

- `2 MB`

because:

$$512 \times 4\text{ KB} = 2\text{ MB}$$

### 4. PUD Level

One `PUD` entry covers:

- `1 GB`

because:

$$512 \times 2\text{ MB} = 1\text{ GB}$$

### 5. PGD Level

One `PGD` entry covers:

- `512 GB`

because:

$$512 \times 1\text{ GB} = 512\text{ GB}$$

## The Simple Story

Imagine you want to find one toy in a huge toy warehouse.

You do not search the whole warehouse at once.

You go step by step:

1. Pick the big building
2. Pick the floor
3. Pick the room
4. Pick the shelf
5. Pick the exact toy on that shelf

That is what `PGD -> PUD -> PMD -> PTE -> offset` is doing.

## How Your Code Builds The Path

Your mapping code is in `kernel/arch/cortex-a53/mmu.c`.

The important helper is `_map_table()`.

It does this:

1. Look at one table level
2. Use the right shift to find the index
3. If the next table does not exist, allocate one page for it
4. Return the next table address

Then `process_map_page_internal()` does this:

1. Make `PGD` if the task does not have one yet
2. Walk/create `PUD`
3. Walk/create `PMD`
4. Walk/create `PTE`
5. Write the final page mapping

So the code is doing:

```text
task virtual address
    -> PGD
    -> PUD
    -> PMD
    -> PTE
    -> physical page
```

## A Visual Example

Suppose a user program wants virtual address:

```text
0x0000000000401234
```

The MMU will think like this:

```text
Virtual address = 0x0000000000401234

1. PGD index = bits for the top directory
2. PUD index = bits for the next directory
3. PMD index = bits for the next directory
4. PTE index = bits for the page entry
5. offset    = 0x234 inside the final 4 KB page
```

If the final physical page is, for example:

```text
0x000000000008A000
```

then the real byte is:

```text
0x000000000008A000 + 0x234 = 0x000000000008A234
```

So:

```text
virtual  0x401234
   becomes
physical 0x8A234
```

## Why Tables Are Stored In Pages Too

A funny but important thing:

- page tables themselves live in memory pages too

That is why your code sometimes allocates new pages just to hold new `PUD`, `PMD`, or `PTE` tables.

So memory is used for two things:

1. real program data/code pages
2. pages that store the page-table books

In your code, those table pages get tracked in `task->mm.kernel_pages`.

## What `_map_table_entry()` Does

After all the directory levels exist, `_map_table_entry()` finally writes the last answer.

That means:

> "This virtual page should point to this physical page with these permissions."

Permissions can mean things like:

- code page
- read-only page
- writable data page

Your code uses flags like:

- `PE_USER_CODE`
- `PE_USER_DATA`
- `PE_USER_RO`

## What `MM_PAGE_MASK` Does

`MM_PAGE_MASK` is used to remove the tiny offset at the end and keep only the page-aligned address.

So if you have:

```text
0x401234
```

the page-aligned base is:

```text
0x401000
```

That means:

- `0x401000` is the page start
- `0x234` is the offset inside that page

## What Happens On A Page Fault

Your `mem_handle_data_abort()` can allocate a page when a task touches an unmapped address.

Child version:

> "The child opened a door to a room that does not exist yet."

So the kernel says:

> "Okay, I will build that room now and add it to the address books."

That function:

1. allocates a new physical page
2. rounds the fault address down to a page boundary
3. maps that page into the current task

## One Tiny Example With Streets

Think of the whole thing like a mail address:

```text
Country -> City -> Street -> House -> Room
```

ARM64 page tables are like:

```text
PGD -> PUD -> PMD -> PTE -> byte inside page
```

So the MMU is just following the address one step at a time.

## The Most Important Things To Remember

If you only remember 5 things, remember these:

1. A page is 4 KB in your kernel.
2. The last 12 bits are the offset inside a page.
3. `PGD`, `PUD`, `PMD`, and `PTE` are nested lookup levels.
4. Each level uses part of the virtual address to choose the next table.
5. The final `PTE` points to the real physical page.

## One-Line Summary

`PGD`, `PUD`, `PMD`, and `PTE` are just a chain of smaller address books the ARM64 MMU uses to turn a pretend virtual address into the real physical memory page.