# Kernel And User Allocation Map Investigation

Date: 2026-04-11

## Scope

This document answers the following questions for the current kernel/userspace GUI stack:

- Where EXE headers, DLL headers, EXE backing, DLL backing, image-load staging buffers, file-read buffers, GWES GUI surfaces, GUI procedures, DLL stack state, DLL module tables, DLL image cache state, and handles are stored.
- Whether each item lives in kernel heap, user heap, static storage, or page-backed memory.
- Which address space sees each allocation.
- Which paths are the most likely sources of heap allocation failures when a GUI app scales up.

## Executive Summary

- EXE and DLL headers are not allocated separately. The header bytes live at offset 0 of the same backing block that stores the full loaded image.
- EXE backing, DLL backing, loader stack backing, loader module records, cache records, and cached image payloads all ultimately come from the kernel heap, either directly through `Heap::alloc` or through `KernelResourceManager::allocate`, which itself wraps `Heap::alloc`.
- GUI shared-surface metadata is mostly static kernel storage, but each actual surface backing is a kernel-heap allocation that is then mapped into GWES and the owning client process.
- Normal userspace file reads do not require a kernel-side staging buffer. The loader is different: it allocates a kernel-heap staging buffer to read EXE and DLL files before parsing and caching them.
- Window procedures are not stored in the kernel. They are process-local function pointers stored in `window.dll` and `widgets.dll` registries in EL0 memory.
- There is no dedicated per-DLL stack pool in the kernel. DLL code runs on the current thread stack. The only loader-owned EL0 stack block in this path is the process startup stack.
- The `Process::handles` field exists in the kernel process struct, but there is no active kernel handle-table implementation attached to it in the current tree. Active GUI handle registries are userspace-owned.

## User Virtual Address Layout

The current EL0 layout is built on `mm::backend::L2BlockSize`, which is 2 MiB in the active AArch64 mapper.

| Region | Virtual address | Source of backing |
| --- | --- | --- |
| Main EXE image | `0x00200000` | Kernel heap image backing, mapped into the process |
| Initial user stack | `0x00400000` | Kernel heap stack backing, mapped into the process |
| DLL/module region start | `0x00600000` | Kernel heap module backing, mapped per module |
| DLL/module region limit | `0x08000000` | End of module mapping search range |
| GUI shared-surface view base | `0x08000000` | Kernel heap surface backing, mapped into GWES and owner |
| GUI shared-surface view limit | `0x18000000` | 256 MiB GUI shared-surface view window |
| Shared input view | `0x18000000` | One physical page allocated from `PhysicalMemory::alloc_page()` |
| User heap base | `0x20000000` | Kernel heap-backed arenas mapped into the process |

Important detail: each user process has its own `AddressSpace`, and the page tables for that address space are themselves heap-backed according to the `AddressSpace` comment in `kernel/include/address-space.h`.

## Storage Matrix

| Item | Where it actually lives | Allocation mechanism | Visible in which address space | Notes |
| --- | --- | --- | --- | --- |
| EXE header | Start of `process->loader_image_backing` | `KernelResourceManager::allocate(LoaderImageBacking)` -> kernel `Heap::alloc` | Kernel VA and mapped into the process at `0x00200000` | Not separate from EXE backing |
| EXE backing | `process->loader_image_backing` | Same as above | Kernel VA and process EL0 | Freed on process teardown |
| DLL header | Start of `LoadedModule::backing` | `KernelResourceManager::allocate(LoaderImageBacking)` -> kernel `Heap::alloc` | Kernel VA and mapped into owner process at `module->image_base` | Not separate from DLL backing |
| DLL backing | `LoadedModule::backing` | Same as above | Kernel VA and owner process EL0 | One backing per loaded module |
| Image-load staging bytes | Temporary `file_bytes` or cached `CachedImageObject::file_bytes` | `Heap::alloc(node.size_bytes)` | Kernel only | Used before image copy/relocation |
| Loader image cache record | `CachedImageObject` list nodes | `KernelResourceManager::allocate(LoaderImageCacheRecord)` | Kernel only | Global cache across loads |
| Loader image cache payload | `CachedImageObject::file_bytes` | `Heap::alloc`, then `KernelResourceManager::track_external` | Kernel only | Retains full file copies for TTL |
| DLL module table | Global `g_module_head` intrusive list of `LoadedModule` | `KernelResourceManager::allocate(LoaderModuleRecord)` plus `Heap::alloc` for path strings | Kernel only, though handle returned to user is `image_base` | Global registry filtered by `owner_process` |
| Loader startup stack backing | `process->loader_stack_backing` | `KernelResourceManager::allocate(LoaderStackBacking)` -> kernel `Heap::alloc` | Kernel VA and process EL0 at `0x00400000` | Per-process, not per-DLL |
| GUI surface metadata | `shared_surfaces[2048]` and `shared_surface_slots[4096]` | Static kernel BSS/data | Kernel only | Permanent metadata footprint |
| GUI surface backing | `SharedWindowSurfaceRecord::backing` | `Heap::alloc(allocation_bytes, mm::PageSize)` | Kernel VA and mapped into GWES plus owner process | One backing per live window/control surface |
| Shared GUI input page | `g_gui_shared_input_region_ptr` | `PhysicalMemory::alloc_page()` then mapped | Kernel VA and mapped at `0x18000000` in consumers | Not from kernel heap |
| GUI procedures (`WNDPROC`, widget user proc) | `window.dll` and `widgets.dll` class/instance registries | `SYS_MALLOC` through userspace runtime, or static user globals | Userspace only | Kernel never stores the callback pointers |
| GUI handles (`HWND`) | Numeric IDs in GWES window records and userspace mirror tables | Static GWES arrays plus userspace `SYS_MALLOC` tables | Mostly userspace; kernel only sees numeric values in requests and surface records | No active kernel handle-table implementation |

## Detailed Findings

### 1. EXE header and EXE backing

`Loader::spawn_user_process()` allocates the initial user stack and then calls `DllLoader::load_user_executable()`. The executable image backing is allocated by `allocate_loader_backing()`, which uses `KernelResourceManager::allocate(KernelResourceKind::LoaderImageBacking, ...)`. `KernelResourceManager::allocate()` itself calls `Heap::alloc()`.

The EXE header is the first bytes of that backing block. The kernel does not allocate a distinct EXE-header object. After relocation/import resolution, the same block is mapped into the process at `UserImageBase = 0x00200000`.

Conclusion: EXE header and EXE body live in one kernel-heap block and are also visible in the process address space after mapping.

### 2. DLL header and DLL backing

`DllLoader::load_new_module()` allocates a per-module backing block with the same `allocate_loader_backing()` helper. `create_loaded_module()` stores the backing pointer in `LoadedModule::backing`, and `map_module()` maps that backing into the owner process at `module->image_base` inside the module window starting at `0x00600000`.

Again, the DLL header is simply the first bytes of the loaded backing image. There is no separate kernel allocation for the header.

Conclusion: DLL header and DLL image body are one kernel-heap allocation per live module, then mapped into the owner process.

### 3. Loader image staging and file-read behavior

There are two materially different file-read paths.

#### Loader path for EXE and DLL files

`read_file_into_staging()` resolves the VFS node, allocates a kernel buffer with `Heap::alloc(node.size_bytes, ...)`, and reads the file into that buffer. `parse_image_view()` then parses directly from those bytes.

If the image is cached, those staging bytes are retained in the global image cache as `CachedImageObject::file_bytes`. If not cached, the staging buffer is freed after the parsed view is released.

Conclusion: loader reads are kernel-heap staged.

#### Normal userspace `readFile()` path

`service_read_file()` resolves the VFS node in the kernel and reads directly into the caller's userspace buffer. It does not allocate a second kernel staging buffer for ordinary file reads.

Conclusion: normal file reads consume the caller's user buffer, not a dedicated kernel-heap staging buffer.

### 4. DLL image cache

The loader maintains a global cache of immutable file bytes.

- Cache metadata: `CachedImageObject` records live in the kernel heap and are linked by `g_cached_image_head`/`g_cached_image_tail`.
- Cache payload: the full file bytes are heap allocations tracked as external resources.
- Cache lifetime: TTL-based, default 5 minutes, zero-reference only.

Important scaling implication: repeated launches can keep full EXE/DLL file copies resident in kernel memory even after one process has already copied them into executable backings.

### 5. DLL module table

The live module table is the global `LoadedModule` intrusive list.

- The `LoadedModule` records are kernel-heap allocations tracked as `LoaderModuleRecord` resources.
- Each record also owns two heap strings: normalized `path` and `module_name`.
- The object is also registered in the global kernel object registry (`KernelObjectManager`), which itself stores object pointers in a heap-backed `HeapList<ObjectHeader*>`.

Conclusion: the DLL module table is a kernel data structure, heap-backed, global across processes, but logically partitioned by `owner_process`.

### 6. DLL stack pool

There is no dedicated per-DLL stack pool in the current loader.

What does exist:

- `process->loader_stack_backing`: one process startup EL0 stack block allocated by `Loader::allocate_user_block()` and mapped at `0x00400000`.
- Thread kernel stacks: separate kernel-side allocations in `thread.cpp`, not DLL-specific.

What does not exist:

- No allocation in `dll_loader.cpp` creates a per-DLL stack.
- DLL code runs on the current thread's existing stack.

Conclusion: if the question is specifically about "DLL stack pool", the active answer is that there is no standalone DLL stack pool in the kernel loader.

### 7. GWES GUI surfaces

Kernel-side GUI surfaces split into metadata and backing.

#### Metadata

- `shared_surfaces[ROS_KERNEL_GUI_WINDOW_SURFACE_MAX_SLOTS]` is a static kernel array.
- `shared_surface_slots[4096]` is also static kernel storage used for virtual-slot bookkeeping.

These are not heap allocations.

#### Surface backing

`GuiService::create_window_surface()` computes `payload_bytes = width * height * 4`, rounds that up to page size, then allocates `record->backing = Heap::alloc(allocation_bytes, mm::PageSize)`.

That backing is then mapped into:

- GWES, the server process that created it.
- The owner client process that paints into it.

The user virtual address used for the mapping is inside the fixed GUI view region starting at `0x08000000`, allocated in 64 KiB slot units even though the actual physical backing is only page-rounded.

Conclusion: each live window or child control surface costs one kernel-heap backing allocation plus one static metadata slot.

### 8. GUI procedures

GUI procedures are not a kernel resource.

The current split is:

- `window.dll` keeps `WindowClassRegistration` and `WindowHandleRegistration` lists in process-local memory. These store `WNDPROC` function pointers.
- `widgets.dll` keeps `WidgetClassDefinition` and `WidgetInstance` lists in process-local memory. These store caller `user_proc` pointers.
- GWES stores retained window geometry/surface state, but not process callback pointers.

Both `window.dll` and `widgets.dll` allocate their registries with `SYS_MALLOC`, which means the memory lives in the calling process's user heap, not in kernel heap.

Conclusion: the kernel only transports messages and surface handles; it does not own or store GUI callback pointers.

### 9. Handles

There are three separate handle stories here.

#### Window handles (`HWND`)

- GWES keeps stable numeric IDs in `g_render_state.window_records[kMaxWindowCount]`.
- `window.dll` mirrors `HWND -> WNDPROC/class` in a per-process linked list allocated from the user heap.
- `widgets.dll` mirrors `HWND -> WidgetInstance` in a per-process linked list allocated from the user heap.
- The kernel GUI service only stores `hwnd` numerically inside `SharedWindowSurfaceRecord` and syscall payload structs.

#### Kernel object handles

Kernel objects are referenced through `ObjectHeader` and the object manager registry, not through a separate general-purpose handle table in the active code shown here.

#### `Process::handles`

`Process` still has a `HandleTables* handles` field, but this field is only nulled during teardown. There is no active definition or allocation path for `HandleTables` in the current live tree that would make this an operating handle table.

Conclusion: the active GUI handle system is userspace-owned. The kernel `handles` field is presently vestigial or not yet wired up.

### 10. User heap backing for `malloc`

Standard userspace `malloc` does not use an independent EL0 allocator implementation. In `ros_support.c`, `malloc()` simply requests memory from `SYS_MALLOC`, and `SYS_MALLOC` is implemented by `service_malloc()`, which calls `UserHeap::alloc_raw()`.

`UserHeap::alloc_raw()` does three things:

- Maintains region metadata in the kernel heap.
- Allocates each arena backing block from the kernel heap with `Heap::alloc(region_bytes, mm::PageSize)`.
- Maps that arena into the process above `UserHeapBase = 0x20000000`.

Conclusion: a user-mode `malloc` failure often means the kernel-side user-heap manager could not allocate more kernel heap backing for that process.

## What Is Most Likely Causing Scaling Failures

### Primary suspect: per-window and per-control surface backing

If your app is scaling by creating more windows, child controls, or larger drawable areas, the most likely kernel pressure point is GUI surface backing.

Why:

- Every live surface allocates `width * height * 4`, rounded to page size.
- The backing is kernel-resident for the full surface lifetime.
- GWES maps the same backing into itself and into the owner process, so the physical memory cost is paid once, but it is still paid in kernel memory.
- The widget model is surface-heavy: child controls own their own surfaces as well, not just top-level windows.

Practical consequence: scaling a complex GUI by adding controls can grow kernel heap usage linearly with total painted pixel area, not just with top-level window count.

### Secondary suspect: userspace `malloc` growth backed by kernel heap

If the failure happens during file loading, font loading, image decoding, or dynamic UI data growth, the next strongest suspect is user-heap pressure.

Why:

- `malloc` and `realloc` ultimately route through `SYS_MALLOC`.
- `UserHeap` arenas are backed by kernel `Heap::alloc`.
- `realloc` in `ros_support.c` is allocate-copy-free, so temporary peak usage is old block plus new block, not in-place growth.

This matters in GWES itself because:

- `gwes_read_file_all()` uses `malloc` and capacity doubling with `realloc`.
- Font and cursor caches keep raw file buffers alive.
- The desktop backing surface is one large persistent `malloc`.
- `gdi_font_support.c` is allocation-heavy.

### Third suspect: loader cache and module residency

If the issue happens during process launch, repeated DLL loads, or launch storms, the loader is a strong suspect.

Why:

- Each process keeps one EXE backing block and one startup stack backing block.
- Each loaded DLL keeps one module backing block and one module-record object.
- The image cache may keep a second full file copy for each EXE/DLL for up to 5 minutes.

This is especially relevant when scaling through repeated launches rather than through one already-running app's window count.

## Hard Limits That Are Not Heap Failures

These are separate ceilings and should not be confused with heap exhaustion:

- GWES retained window array: `MAX_WINDOWS = 1024`.
- Kernel shared-surface record table: `ROS_KERNEL_GUI_WINDOW_SURFACE_MAX_SLOTS = 2048`.
- Shared-input consumer slots: `8`.

If one of these hits first, the failure mode will be `StatusNoSpace` or server-side rejection, not necessarily a raw heap allocation failure.

## Bottom Line

For the current codebase, the best short answer is:

- EXE/DLL headers and their loaded bytes live inside kernel-heap backing blocks that are then mapped into the target process.
- Loader cache records and payloads are kernel-only and heap-backed.
- GWES shared window surfaces are kernel-heap backed and are the most obvious kernel-memory growth vector for a scaling GUI app.
- GUI procedures and most GUI handle registries are userspace structures, not kernel structures.
- User `malloc` failures still often originate in kernel heap pressure, because the user heap is itself backed by kernel `Heap::alloc`.

## Recommended Next Measurements

If you want to turn this from investigation into a deterministic root-cause capture, the next instrumentation should be:

1. Add `KernelResourceManager` tracking for GUI surface backing, not just loader backing.
2. Add per-process `UserHeap` stats: arena count, live bytes, peak bytes, and allocation metadata count.
3. Log `allocation_bytes`, `slot_count`, and total live surface bytes when `GuiService::create_window_surface()` fails.
4. Dump loader resource stats and image-cache stats on every `StatusNoMemory` during process spawn or DLL load.
