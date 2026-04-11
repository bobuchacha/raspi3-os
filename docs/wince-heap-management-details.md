# Windows CE 7 Heap Management Analysis

## Scope

This report describes how heap allocation is implemented in the `WINCE700` source tree, with emphasis on the code you would need to understand before redesigning heap management.

The analysis covers two different mechanisms:

- The NK kernel object heap in `WINCE700/private/winceos/COREOS/nk/kernel/heap.c`.
- The generic Win32 heap manager (`RHeap`) in `WINCE700/private/winceos/COREOS/core/lmem/rheap.cpp`, which backs `HeapAlloc`, `HeapFree`, `HeapReAlloc`, `LocalAlloc`, `LocalFree`, and related APIs.

## Files Examined

- `WINCE700/private/winceos/COREOS/core/lmem/heap.h`
- `WINCE700/private/winceos/COREOS/core/lmem/heap16.h`
- `WINCE700/private/winceos/COREOS/core/lmem/heap32.h`
- `WINCE700/private/winceos/COREOS/core/lmem/heap.c`
- `WINCE700/private/winceos/COREOS/core/lmem/rheap.cpp`
- `WINCE700/private/winceos/COREOS/core/lmem/dllheapfunc.cpp`
- `WINCE700/private/winceos/COREOS/core/lmem/th32heap.c`
- `WINCE700/private/winceos/COREOS/core/dll/coredll.cpp`
- `WINCE700/private/winceos/COREOS/core/inc/dllheap.h`
- `WINCE700/private/winceos/COREOS/nk/inc/heap.h`
- `WINCE700/private/winceos/COREOS/nk/kernel/heap.c`
- `WINCE700/private/winceos/COREOS/nk/kernel/x86/splheap.c`

## Executive Summary

Windows CE 7 does not use one heap design everywhere.

- NK has a dedicated fixed-size kernel heap for kernel objects. It is a page-fed pool allocator with one free list per object class. It never returns pages once they enter a pool. This is optimized for speed and determinism, not for reclaiming memory.
- User-visible heaps use `RHeap`, a variable-size allocator built around 60 KB regions inside 64 KB VM blocks. The default granularity is 16 bytes on most architectures, but x86 builds with extended heap sentinels switch to 32-byte blocks.
- `RHeap` has three allocation strategies:
  - Fixed-size O(1) free-list regions for 1-, 2-, and 4-block allocations when `HEAP_SUPPORT_FIX_BLKS` is enabled.
  - Bitmap-tracked variable-size regions for everything else below the large-allocation threshold.
  - A separate `VirtualAlloc` path for allocations larger than 16 KB on growable heaps.
- Growable local heaps are implemented as embedded heaps: the heap header and region headers live inside the same reserved 64 KB chunks as user data.
- Compaction is non-moving. It only decommits trailing pages in regions and destroys completely empty secondary regions. Allocations never move unless `HeapReAlloc` falls back to allocate-copy-free.
- Debugging and corruption detection are built into the allocator through sentinels, tail signatures, free-pattern fills, optional no-access protection of freed pages, Toolhelp enumeration support, and CeLog hooks.

If you want to redesign CE7 heap management, the main design axes are:

- Allocation latency versus fragmentation.
- Stable pointers versus movable compaction.
- Region-local metadata simplicity versus richer size-class segregation.
- Single-heap locking versus scalable concurrency.
- Deep corruption diagnostics versus metadata/padding overhead.

## 1. High-Level Split

### 1.1 NK fixed kernel object heap

`nk/inc/heap.h` defines 8 arenas (`NUMARENAS`) and maps kernel object types onto them, such as `MODULELIST`, `APISET`, `MAPVIEW`, `MUTEX`, `MODULE`, `PROCESS`, `THREAD`, and a final 1024-byte max bucket (`HEAP_MAXALLOC`) (`nk/inc/heap.h:18-125`).

`nk/kernel/heap.c` describes the design explicitly: objects are smaller than one page, each arena has a fixed object size, each page is threaded into an object free list, and pages are never freed back out of the pool (`nk/kernel/heap.c:27-42`).

### 1.2 Generic heap manager used by Win32 heap APIs

The exported APIs in `core/lmem/heap.c` are thin validation wrappers over `RHeap`:

- `LocalAlloc` and `LocalFree` route to `RHeapAlloc` and `RHeapFree` on `g_hProcessHeap` (`core/lmem/heap.c:42-56`, `core/lmem/heap.c:116-126`).
- `HeapCreate`, `HeapAlloc`, `HeapReAlloc`, `HeapFree`, `HeapSize`, `HeapDestroy`, `HeapCompact`, and `HeapValidate` route into `DoRHeapCreate`, `RHeapAlloc`, `RHeapReAlloc`, `RHeapFree`, `RHeapSize`, `RHeapDestroy`, `RHeapCompact`, and `RHeapValidate` (`core/lmem/heap.c:136-332`).

The real implementation is almost entirely in `core/lmem/rheap.cpp`.

## 2. RHeap Geometry and Metadata

### 2.1 Region size and large-allocation threshold

Important constants are in `core/lmem/heap.h`:

- `CE_FIXED_HEAP_MAXSIZE = 15 * VM_PAGE_SIZE`, so each normal region is 60 KB, leaving the final 4 KB page of the surrounding 64 KB block uncommitted as a guard page (`core/lmem/heap.h:24`).
- `CE_VALLOC_MINSIZE = 4 * VM_PAGE_SIZE`, so allocations larger than 16 KB use the dedicated `VirtualAlloc` path on growable heaps (`core/lmem/heap.h:25`).
- `HEAP_MAX_ALLOC = 1 GB` caps request size (`core/lmem/heap.h:163`).

### 2.2 Block size

`RHeap` works in blocks, not bytes.

- Most architectures use 16-byte blocks (`core/lmem/heap16.h:23-25`).
- x86 builds with extended sentinel frames use 32-byte blocks (`core/lmem/heap32.h:23-25`).

This matters because every region bitmap, fixed-class decision, allocation rounding, and free-space scan is expressed in block counts.

### 2.3 Region and heap metadata

`RHRGN` contains:

- Heap owner pointer.
- Links to next region.
- Local and remote base addresses.
- Free-block count, total-block count, first-likely-free index, committed-block boundary.
- Either `maxBlkFree` for bitmap-managed regions or `prgnPrev` for free-list regions.
- Flexible `allocMap[]` storage used either as bitmaps or as subregion/free-list metadata (`core/lmem/heap.h:195-220`).

`RHEAP` contains:

- Signature and process ID.
- Maximum size.
- Heap options.
- Linked-list node for the global heap list.
- Large-allocation list (`pvaList`).
- Tail pointer for the default region list.
- A critical section for serialization.
- Allocator/deallocator callbacks.
- Optional fixed-block region lists (`pRgnList`).
- A cached recent region pointer (`prgnfree`).
- Embedded first region (`rgn`) as the last member (`core/lmem/heap.h:260-276`).

### 2.4 Two region types

`rheap.cpp` is explicit that there are two region styles (`core/lmem/rheap.cpp:35-115`):

- Non-fix regions: arbitrary-sized allocations, tracked by a 2-bit-per-block bitmap.
- Fix allocation regions: each region serves a single size class and is subdivided into subregions with explicit free lists.

### 2.5 Bitmap encoding

For variable-size regions, every block uses 2 bits:

- `00` free
- `01` continuation
- `10` head bit
- `11` start block

See `RHF_FREEBLOCK`, `RHF_CONTBLOCK`, `RHF_HEADBIT`, `RHF_STARTBLOCK` in `core/lmem/heap.h:136-141`.

The allocator uses precomputed masks in `alcValHead`, `alcValTail`, and `alcMasks` to mark and scan allocations quickly (`core/lmem/rheap.cpp:151-222`).

## 3. Heap Creation and Initialization

### 3.1 Process startup

`coredll.cpp` calls `LMemInit()` during `DLL_PROCESS_ATTACH`; process startup fails if heap initialization fails (`core/dll/coredll.cpp:257-266`).

### 3.2 Default process heap

`LMemInit()` initializes the global heap list and creates `g_hProcessHeap` through `DoRHeapCreate()` (`core/lmem/rheap.cpp:3398-3437`).

There is an important mode split:

- In kernel mode, the default process heap is created with `HEAP_IS_PROC_HEAP | HEAP_SUPPORT_FIX_BLKS` (`core/lmem/rheap.cpp:3423-3424`).
- In user mode, the default process heap is created with only `HEAP_IS_PROC_HEAP` (`core/lmem/rheap.cpp:3423-3424`).

That means the kernel default process heap benefits from fixed-size fast paths, but ordinary user-mode default heaps do not.

### 3.3 DoRHeapCreate policy

`DoRHeapCreate()` chooses the heap layout (`core/lmem/rheap.cpp:2065-2220`):

- If no custom allocator is supplied and `dwMaximumSize == 0`, the heap becomes growable and `HEAPRGN_IS_EMBEDDED` is set.
- Embedded growable heaps reserve a full 60 KB region chunk and place the heap header inside it.
- Non-embedded heaps allocate the `RHEAP` structure separately from the managed data region.
- If `HEAP_SUPPORT_FIX_BLKS` is active, `pRgnList` is placed immediately after the base heap header.
- For embedded local heaps that are not shared or remote, `HEAP_FREE_CHECKING_ENABLED` is turned on (`core/lmem/rheap.cpp:2148-2152`).

### 3.4 Region initialization

`InitRegion()` sets up the first region or any newly grown region (`core/lmem/rheap.cpp:1557-1643`):

- Embedded heaps already have the reserved address from the caller.
- Non-embedded heaps reserve and optionally commit data separately through `php->pfnAlloc`.
- `idxBlkCommit` tracks how much is committed.
- `numBlkFree` counts free blocks including uncommitted tail space.
- For free-list regions, subregion headers and block-linked free lists are built in `allocMap`.
- For non-free-list regions, `maxBlkFree` is initialized to the region size.

### 3.5 Initial fixed-block region set

When `HEAP_SUPPORT_FIX_BLKS` is enabled on an embedded heap, `DoRHeapCreate()` immediately creates one empty region each for the `1`, `2`, `3`, and `4` block lists through `RHeapGrowNewRegion()` (`core/lmem/rheap.cpp:2180-2191`).

Important nuance:

- Only `1`, `2`, and `4` block regions use the O(1) explicit free-list implementation.
- `3` block regions are still treated as fixed-size regions, but they fall back to the generic bitmap allocator because there is no efficient subregion free-list layout for 3-block slots (`core/lmem/rheap.cpp:1573-1579`, `core/lmem/rheap.cpp:2516-2518`).

## 4. Small Allocation Fast Path: Fixed-Block Regions

### 4.1 Size classes

With fixed-block support enabled, requests of `1`, `2`, `3`, or `4` blocks are steered to dedicated region lists (`core/lmem/rheap.cpp:2503-2514`).

On x86, where the block size is 32 bytes:

- 1 block = 32 bytes
- 2 blocks = 64 bytes
- 3 blocks = 96 bytes
- 4 blocks = 128 bytes

On 16-byte builds, the classes are 16, 32, 48, and 64 bytes.

### 4.2 Subregion organization

For free-list-backed fixed regions, each region is split into subregions of 256 logical blocks, with:

- A `SUBRGN` header.
- A byte-per-slot free list.
- Reserved tail markers `0xFE` and `0xFF` for end-of-list and allocated-state markers (`core/lmem/rheap.cpp:35-115`).

Header sizing and unused-block counts are precomputed in `g_dwRgnHdrSize`, `g_dwRgnTotalBlks`, `g_NumSubRgns`, and `g_NumUnusedBlks` (`core/lmem/rheap.cpp:236-305`).

### 4.3 Allocation

For 1-, 2-, and 4-block classes:

- `RHeapAlloc()` picks the region list for the class (`core/lmem/rheap.cpp:2503-2514`).
- It uses the list head as the active allocation region (`core/lmem/rheap.cpp:2516-2528`).
- `FindBlocksFx()` calculates the slot index from the first subregion that still has a free entry, commits pages if necessary, checks free sentinels, and then `TakeBlocksFx()` pops the free-list head (`core/lmem/rheap.cpp:1469-1499`, `core/lmem/rheap.cpp:921-949`).
- If the region becomes full and there is another region behind it, the full region is moved to the end of the list via `MoveRgnToEnd()` (`core/lmem/rheap.cpp:2326-2348`, `core/lmem/rheap.cpp:2524-2527`).

This is very close to O(1) allocation for hot small-object classes.

### 4.4 Free

`RHeapFreeWithCaller()` routes fixed-class frees to `FreeBlocksFx()` (`core/lmem/rheap.cpp:2633-2644`).

`FreeBlocksFx()`:

- Verifies the slot is currently allocated.
- Validates allocation sentinels.
- Writes free sentinels and fills freed payload with `0xCC`.
- Pushes the slot back onto the subregion free list.
- If the subregion was previously full, it is returned to the head-visible free chain.
- Clears `g_fFullyCompacted` so future `CompactAllHeaps()` runs will do real work (`core/lmem/rheap.cpp:1022-1059`).

If a once-full region becomes reusable and has all pages committed, `RHeapFreeWithCaller()` may move it back to the front with `MoveRgnToBegin()` (`core/lmem/rheap.cpp:2330-2403`, `core/lmem/rheap.cpp:2638-2643`).

## 5. General Allocation Path: Bitmap Regions

### 5.1 Search strategy

For everything not served by a free-list fixed region, `RHeapAlloc()` searches regions using a cached starting point:

- `php->prgnfree` for the default variable-size region chain.
- `pRgnList[i].prgnFree` for fixed-size-but-bitmap-managed lists such as 3-block regions (`core/lmem/rheap.cpp:2507-2514`, `core/lmem/rheap.cpp:2568-2573`).

The scan is region-first, not globally size-segregated.

### 5.2 Intra-region free-space scan

Inside a bitmap region:

- `CountFreeBlocks()` measures consecutive free blocks starting at an index (`core/lmem/rheap.cpp:1170-1239`).
- `SkipAllocBlocks()` skips over a current allocation (`core/lmem/rheap.cpp:1335-1377`).
- `DoFindBlockInRegion()` repeatedly alternates these operations, keeping track of `maxBlkFree` while searching (`core/lmem/rheap.cpp:1379-1448`).
- `FindBlockInRegion()` first searches from `idxBlkFree` to the commit boundary, then optionally wraps to index 0 if needed (`core/lmem/rheap.cpp:1501-1554`).

This is essentially a first-fit search with remembered start position and opportunistic `maxBlkFree` updates.

### 5.3 Allocation marking

Once a fit is found:

- `CommitBlocks()` commits additional pages if the allocation extends past the committed boundary (`core/lmem/rheap.cpp:1117-1137`).
- `TakeBlocks()` writes start/continuation bits into the bitmap and updates bookkeeping (`core/lmem/rheap.cpp:951-995`).

The allocator does not maintain explicit free lists for arbitrary-sized free runs.

### 5.4 Growth

If no region can satisfy the request and the heap is growable:

- `RHeapAlloc()` calls `RHeapGrowNewRegion()` (`core/lmem/rheap.cpp:2554-2559`).
- `RHeapGrowNewRegion()` reserves a new 60 KB region chunk, initializes it, links it into the proper list, marks region-header space as permanently allocated, and optionally takes the initial request from the new region (`core/lmem/rheap.cpp:1645-1769`).

For the default variable-size region chain, new regions are appended to the tail. For fixed-size class lists, new regions are inserted at the head (`core/lmem/rheap.cpp:1691-1709`).

## 6. Large Allocation Path: VirtualAlloc Items

### 6.1 Threshold

On growable heaps, requests larger than `CE_VALLOC_MINSIZE` (16 KB) bypass region allocation entirely (`core/lmem/heap.h:25`, `core/lmem/heap.h:131`, `core/lmem/rheap.cpp:2477-2480`).

### 6.2 Metadata and reserve/commit behavior

`DoRHeapVMAlloc()`:

- Allocates a small `RHVAITEM` descriptor from the process heap.
- Reserves a block-aligned virtual region sized to `cbSize + HEAP_SENTINELS`, rounded to 64 KB.
- Commits only the pages currently needed.
- Stores the exact request size in `pitem->cbSize`.
- Adds the item to the heap’s `pvaList` (`core/lmem/rheap.cpp:1911-1962`).

This path dramatically reduces region fragmentation for large requests.

### 6.3 Free and realloc

- `DoRHeapVMFree()` removes the descriptor from `pvaList`, validates sentinels, releases the VM reservation, and frees the descriptor (`core/lmem/rheap.cpp:1964-2000`).
- `RHeapReAlloc()` can shrink by decommitting committed tail pages, or grow in place as long as the new committed size still fits inside the original reserved 64 KB-aligned reservation (`core/lmem/rheap.cpp:2872-2905`).
- If the reserved space is too small, it falls back to allocate-copy-free unless `HEAP_REALLOC_IN_PLACE_ONLY` was requested (`core/lmem/rheap.cpp:2927-2947`).

## 7. Free, Size, and Pointer Validation

### 7.1 Pointer-to-region lookup

Pointer validation is split:

- `FindRegionByPtr()` locates the containing region.
- Embedded heaps use 64 KB alignment to infer the region base directly.
- Non-embedded heaps scan region links starting from `prgnfree`.
- `FindVaItemByPtr()` separately checks the list of large `VirtualAlloc` items (`core/lmem/rheap.cpp:1773-1849`).

### 7.2 Size queries

`RHeapSize()`:

- Finds the containing region or VA item.
- Computes block count from bitmap or fixed-class state.
- Returns the user-requested byte count when sentinels are enabled, not the rounded block size (`core/lmem/rheap.cpp:2713-2773`).

That exact-size behavior matters for compatibility.

### 7.3 Validation

`RHeapValidate()` can validate:

- A single pointer through `DoRHeapValidatePtr()`.
- Or the whole heap by validating signatures, heap structure, each region, each subregion free list, and finally every sentinel via full heap enumeration (`core/lmem/rheap.cpp:1852-1908`, `core/lmem/rheap.cpp:3141-3208`).

## 8. Reallocation Semantics

`RHeapReAlloc()` has strong pointer-stability semantics but limited in-place growth capability (`core/lmem/rheap.cpp:2776-2959`):

- Fixed-class allocations cannot grow or shrink in place.
- Bitmap-region allocations can grow in place only if immediately adjacent free/uncommitted blocks exist and can be committed.
- Shrinking a bitmap allocation frees the tail blocks but does not move the head.
- VA allocations can grow in place only inside the already-reserved span.
- If in-place growth fails and `HEAP_REALLOC_IN_PLACE_ONLY` is not set, the allocator performs allocate-copy-free.

This is why CE7 compaction is non-moving: object addresses stay stable except when the caller explicitly asks for reallocation and accepts relocation.

## 9. Compaction and Page Reclaim

### 9.1 Region compaction

`DoRHeapCompactRegion()` only decommits tail pages that are definitely unused (`core/lmem/rheap.cpp:2406-2464`):

- For free-list fixed regions, it scans backward for the last allocated slot.
- For bitmap regions, it scans backward for the last nonzero bitmap dword.
- It then decommits everything above that point and recomputes max free size by enumerating region items.

### 9.2 Heap compaction

`RHeapCompact()` compacts:

- The base variable-size region chain.
- Every fixed-class region chain if present (`core/lmem/rheap.cpp:3098-3137`).

`DoRHeapCompactRgnList()` also destroys secondary regions that are completely free, after accounting for reserved header blocks and fixed-region unusable blocks (`core/lmem/rheap.cpp:3035-3095`).

### 9.3 Process-wide opportunistic compaction

`CompactAllHeaps()` iterates the global heap list and runs `RHeapCompact()` on each heap, but only if some free operation previously marked `g_fFullyCompacted = FALSE` (`core/lmem/rheap.cpp:3296-3313`).

This is a coarse process-wide reclamation mechanism, not a background compactor.

## 10. Corruption Detection and Debug Instrumentation

### 10.1 Sentinel layout

The sentinel header stores:

- Signature.
- Requested size.
- Allocation caller frames and free caller frame (`core/lmem/heap.h:81-119`).

The returned pointer points after the sentinel header.

### 10.2 Allocation and free patterns

On allocation:

- `RHeapSetAllocSentinels()` writes the header and fills tail padding with a byte ramp beginning at `0xA5` (`core/lmem/rheap.cpp:473-514`).

On free:

- `RHeapSetFreeSentinels()` records the free caller and fills payload with `0xCC` (`core/lmem/rheap.cpp:581-603`).

Checks:

- `RHeapCheckAllocSentinels()` verifies header signature, requested size, and tail pattern and raises `STATUS_HEAP_CORRUPTION` on failure (`core/lmem/rheap.cpp:516-579`).
- `RHeapCheckFreeSentinels()` scans freed memory for `0xCCCCCCCC` or embedded free sentinels and also raises on corruption (`core/lmem/rheap.cpp:605-649`).

### 10.3 x86 call-stack capture

On x86, the allocator tries to walk past `coredll` or `k.coredll` frames and record user frames into the sentinel header (`core/lmem/rheap.cpp:365-450`).

This is why x86 builds use the larger sentinel/block configuration (`core/lmem/heap.h:46-69`, `core/lmem/heap32.h:20-69`).

### 10.4 Optional no-access freed-page protection

`MarkPagesNoAccess()` can mark freed pages as `PAGE_HEAP_NOACCESS`, but only:

- When heap checking is enabled.
- And not on the first page of a region (`core/lmem/rheap.cpp:709-719`).

On x86 NK, the split-heap shim code maps `PAGE_HEAP_NOACCESS` to a page-table flag and also clears `HEAP_FREE_CHECKING_ENABLED` for heaps created by the generic kernel heap wrapper (`nk/kernel/x86/splheap.c:319-336`).

That means the debug page-noaccess behavior is supported, but intentionally reduced for the generic kernel heap path.

### 10.5 Double-free detection

If `RHeapFreeWithCaller()` cannot find the pointer in regions or VA items, it treats that as an invalid free and, unless DLL custom heap behavior is active, raises heap corruption as a likely double-free (`core/lmem/rheap.cpp:2678-2692`).

## 11. Remote Heap Support

CE7 also supports remote heaps through `CeRemoteHeapCreate()` and `CeRemoteHeapTranslatePointer()` (`core/lmem/rheap.cpp:3315-3392`).

Two modes exist:

- Same-process remote heap: still uses `VirtualAlloc`.
- Cross-process remote heap: uses a file-mapping-backed transport where the heap keeps both local and remote base addresses (`core/lmem/rheap.cpp:799-833`, `core/lmem/rheap.cpp:3321-3337`).

This is why both `RHRGN` and `RHVAITEM` carry:

- `hMapfile`
- `pRemoteBase`
- `pLocalBase`
- per-reservation data (`core/lmem/heap.h:195-220`, `core/lmem/heap.h:274-282`)

If your redesign does not need remote heap semantics, this is one of the few clearly separable feature layers.

## 12. Tooling and Observability

### 12.1 Toolhelp snapshot integration

`GetHeapSnapshot()` uses `EnumerateHeapItems()` to expose heap state to Toolhelp. It reports:

- Free blocks.
- Regular allocations.
- Big `VirtualAlloc` allocations.
- Allocation caller PC in `dwResvd` when sentinels are enabled (`core/lmem/th32heap.c:20-113`).

### 12.2 DLL heap hooks

`dllheapfunc.cpp` does not introduce a different allocator core. Instead, it wraps the normal heap APIs and stuffs caller addresses into sentinels so per-DLL heaps and CRT replacements can piggyback on the same machinery (`core/lmem/dllheapfunc.cpp:23-220`, `core/inc/dllheap.h:17-60`).

This is more of a diagnostics/plumbing feature than a separate heap design.

## 13. NK Kernel Heap Details

### 13.1 Arena model

The NK heap defines fixed object arenas in `nk/inc/heap.h` and instantiates one `heapptr_t` per arena in `nk/kernel/heap.c` (`nk/inc/heap.h:18-125`, `nk/kernel/heap.c:53-58`).

Each arena stores:

- Fixed block size.
- Free-list head.
- Current used count.
- Lifetime max count.

### 13.2 Backing memory

`HeapInit()` reserves up to 64 MB of kernel heap VA on ARM/x86 and advances a bump pointer page-by-page as new pages are needed (`nk/kernel/heap.c:111-140`).

### 13.3 Page acquisition and spare-byte distribution

`GetKHeap()`:

- First tries to satisfy the request out of a current page remainder tracked by `pFree` and `dwLen`.
- Otherwise grabs a physical page, maps it into kernel VA, returns the requested object-sized prefix, and hands the leftover bytes to `FreeSpareBytes()` (`nk/kernel/heap.c:145-223`).

`FreeSpareBytes()` distributes the unusable tail among arena free lists, round-robin by arena, and records residual waste in `KINX_HEAP_WASTE` (`nk/kernel/heap.c:70-104`).

### 13.4 Allocation and free

- `AllocMem()` pops from the arena free list or obtains a new object-sized slice from `GetKHeap()` (`nk/kernel/heap.c:226-249`).
- `FreeMem()` pushes the object back to the arena free list and in debug builds paints it with `0xAB` (`nk/kernel/heap.c:253-282`).

This design is extremely fast and simple, but it never coalesces and never returns memory to the VM subsystem.

### 13.5 Generic kernel heap

NK also exposes `NKCreateHeap`, `NKHeapAlloc`, `NKHeapFree`, and `NKHeapCompact`, but those are only wrappers that switch into `g_pprcNK` and call the normal `RHeap` exports from `kcoredll` (`nk/kernel/heap.c:371-420` and following).

So the kernel has both:

- A dedicated object-pool allocator for core kernel objects.
- A variable-size generic heap built on `RHeap` for non-time-critical allocations.

## 14. What This Design Optimizes For

The CE7 heap is trying to satisfy several constraints simultaneously:

- Stable addresses. Normal compaction never moves live allocations.
- Very cheap small-object alloc/free when fixed-block support is enabled.
- Simple metadata that fits naturally into 64 KB VM granularity.
- Cheap tail-page decommit without complicated relocation.
- Built-in corruption diagnostics for OEM and Microsoft debug workflows.
- Remote-process heap transport for a subset of scenarios.

This is a practical embedded-OS design, not a general-purpose modern scalable allocator.

## 15. Main Redesign Pain Points

If you want to redesign heap management, these are the important limitations in the current design.

### 15.1 Single lock per heap

All normal region operations serialize on `php->cs` (`core/lmem/heap.h:264`, `core/lmem/rheap.cpp:2501`, `2626`, `2731`, `2798`, `3108`, `3173`).

Implication:

- Contention scales poorly with many threads.
- Small alloc/free hot paths still take the same heap-wide lock.

### 15.2 Limited small-size segregation

Only 1-, 2-, and 4-block classes get true O(1) free-list treatment. Everything else falls back to region scanning and bitmap bookkeeping.

Implication:

- Many common sizes outside the selected classes still fragment general regions.
- The design is tuned for a narrow set of dominant small sizes.

### 15.3 Region-local fragmentation

Bitmap regions never move live allocations, so internal holes remain until reused. `RHeapCompact()` only decommits free pages at the region tail.

Implication:

- You can reclaim committed tail pages.
- You cannot recover committed interior holes without relocating live objects.

### 15.4 Static geometry

The design hard-codes:

- 60 KB regions.
- 16 KB large-object threshold.
- A tiny set of fixed classes.
- One region list per class.

Implication:

- It is simple and predictable.
- It is not workload-adaptive.

### 15.5 Diagnostics overhead

Sentinels, tail signatures, caller tracking, free-pattern fills, and optional page-noaccess checking add memory and CPU cost.

Implication:

- Great for fault diagnosis.
- Expensive if you want maximum throughput or tight footprint.

## 16. Compatibility Constraints You Should Preserve Deliberately

If you redesign this allocator and still need CE7-compatible behavior, preserve or consciously break the following:

- Size `0` allocations are normalized to `1` byte, not rejected (`core/lmem/heap.c:25-35`).
- `HeapSize` reports requested size, not rounded allocation size, when sentinels are enabled (`core/lmem/rheap.cpp:2743-2749`).
- `HeapReAlloc` may move allocations only when in-place growth fails and the caller did not request in-place-only (`core/lmem/rheap.cpp:2936-2947`).
- Growable heaps depend on 64 KB region-aligned pointer arithmetic in embedded mode (`core/lmem/rheap.cpp:1782-1804`).
- Toolhelp snapshot and heap validation depend on full enumeration of all regions and VA items (`core/lmem/th32heap.c:65-113`, `core/lmem/rheap.cpp:2295-2330`).
- Remote heap translation depends on having both local and remote base addresses for items and regions (`core/lmem/rheap.cpp:3344-3392`).

## 17. Redesign Directions Suggested by This Code

If the goal is a materially better CE heap, the most promising changes are:

- Replace heap-global locking with per-size-class or per-region locks, or add thread-local caches for small allocations.
- Broaden segregated size classes well beyond 1, 2, and 4 blocks.
- Keep the dedicated large-allocation path, but make the threshold workload-tunable instead of fixed at 16 KB.
- Replace region scanning with explicit free-run trees or segregated free lists for variable-size regions.
- Decide explicitly whether you still want strict pointer stability. If yes, compaction remains limited. If no, you can reclaim fragmentation much more aggressively.
- Separate debug heap features from release geometry so sentinel size does not indirectly alter base block size and size classes.
- Consider dropping embedded region metadata if you want more flexible region sizing, but only after accounting for the compatibility cost of losing 64 KB base inference.
- If remote heaps are not required, isolate or remove the local/remote dual-address model to simplify metadata.

## 18. Bottom Line

CE7 heap management is a hybrid:

- NK object heap: fixed-pool, page-fed, no reclamation, optimized for kernel object speed.
- `RHeap`: region-based, mostly non-moving, partly size-segregated, partly bitmap-scanned, with a separate `VirtualAlloc` path for large requests and heavy built-in diagnostics.

The most important design fact for a redesign is this:

- CE7 does not solve fragmentation globally.
- It avoids the worst small-object cost through a few fixed classes.
- It avoids the worst large-object fragmentation by splitting them out into VA allocations.
- Everything in the middle is managed by a serialized region-and-bitmap allocator with non-moving compaction.

If you redesign it, the biggest leverage points are concurrency, richer size segregation, and a more capable free-space data structure for medium allocations.
