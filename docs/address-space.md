# Address space reference — raspi3-os

This document describes the virtual address layout, sizes, and loader constraints used by this kernel and userspace runtime. It is a concise, code-referenced summary intended to help developers reason about where things live at runtime and the invariants the loader/MMU depend on.

Summary (high level)
- Kernel high-half base: 0xFFFF000000000000 (KernelVaBase in `kernel/include/mm.h`).
- User canonical region: 0x0000000000000000 — 0x0000FFFFFFFFFFFF (UserVaBase..UserVaLimit).
- MMU L2 block granularity: 2 MiB (mm::backend::L2BlockSize, AARCH64 L2 block).

Key constants
- Page size: 0x1000 (4096) — `KERNEL_PAGE_SIZE` (`kernel/include/mm.h`).
- L2 block size: 1 << 21 = 0x00200000 (2,097,152 bytes, 2 MiB) — `mm::backend::L2BlockSize` (see `kernel/include/internal/mm/arch/aarch64/mmu_defs.h`).
- Kernel virtual base: 0xFFFF000000000000 (`KERNEL_VA_BASE`).
- User VA range: 0x0000000000000000 — 0x0000FFFFFFFFFFFF (`KERNEL_USER_VA_BASE`..`KERNEL_USER_VA_LIMIT`).

Derived addresses used by the loader and services
- User executable image base (fixed):
  - Symbol: `UserImageBase` (in `kernel/loader/loader.cpp`) = `mm::backend::L2BlockSize`
  - Value: 1 * L2 = 0x00200000 (2 MiB)
- User stack region (one L2 block directly above image):
  - `UserStackBase` = `UserImageBase` + L2 = 0x00400000 (4 MiB)
  - `UserStackTop`  = `UserStackBase` + L2 = 0x00600000 (6 MiB)
  - The loader maps one full L2 block for the initial user stack.
- User module (DLL/driver) region:
  - `UserModuleRegionBase` (in `kernel/loader/dll_loader.cpp`) = 3 * L2 = 0x00600000 (6 MiB) — note: equals `UserStackTop`.
  - Slot size: `UserModuleSlotSize` = L2 = 0x00200000 (2 MiB).
  - Slot count: `UserModuleSlotCount` = 32.
  - Region end: 0x00600000 + (32 * 0x00200000) = 0x04600000 (≈ 70 MiB).
  - Default DLL preferred base: equals `UserModuleRegionBase` (0x00600000).
  - Default driver preferred base: `UserModuleRegionBase + 8 * UserModuleSlotSize` = 0x01600000 (≈ 22 MiB).
  - Notes: each module occupies one whole L2 slot. The loader enforces `header->image_size <= UserModuleSlotSize`.

- GUI shared-surface view region (fixed per-slot L2 mappings):
  - `GuiSurfaceViewBase` (in `kernel/gui/gui_service.cpp`) = 64 * L2 = 0x08000000 (128 MiB)
  - `ROS_KERNEL_GUI_WINDOW_SURFACE_MAX_SLOTS` = 16 (slot count)
  - Region size = 16 * L2 = 0x02000000 (32 MiB)
  - Region covers: 0x08000000 .. 0x0A000000 (exclusive)
  - Per-slot address = GuiSurfaceViewBase + slot_index * L2.

- Shared memory view region (fixed per-slot L2 mappings):
  - `SharedMemoryViewBase` (in `kernel/shared_memory.cpp`) = 80 * L2 = 0x0A000000 (160 MiB)
  - Slot count: SharedMemoryViewSlotCount = 32
  - Region size = 32 * L2 = 0x04000000 (64 MiB)
  - Region covers: 0x0A000000 .. 0x0E000000 (exclusive)
  - Per-slot address = SharedMemoryViewBase + slot_index * L2.

MMU and mapping policy (brief)
- The implementation maps only whole L2 blocks into EL0 address spaces. See `kernel/mm/arch/aarch64/mm.cpp` and the L2 assumptions in the loader.
- The bootstrap and kernel address spaces keep a low-identity map (low 1 GiB) for early userspace switching. Kernel code also maintains a higher-half mapping at `KernelVaBase`.
- Mappings are applied using `mm::MemoryManager::map()` which requires `virtual_base`, `physical_base`, and `length` be multiples of `L2BlockSize`.

Loader constraints and implementation details (important to document)
- Staging buffer: `g_loader_file_buffer` is aligned to L2BlockSize and is `2 MiB` (`LoaderFileBufferSize`), used as a temporary file read buffer before copying into heap-backed image slots. Files larger than this buffer are rejected during staging.
- Module backing allocations: every module (EXE/DLL/DRIVER) gets a heap-backed block allocated with `Heap::alloc(UserModuleSlotSize, UserModuleSlotSize)` so the backing is L2-aligned and L2-sized.
- Mapping flags are derived from section flags (DLL_SEC_WRITE / DLL_SEC_EXEC) and include `PageUser` plus present/exec/write as needed.
- Preferred base semantics: loader honors `header->image_base` when aligned to a slot and unused. Otherwise the loader searches the module region for a free slot.
- Import resolution: the loader resolves imports by looking up dependent modules via `ensure_library_loaded()`; it writes resolved absolute addresses into the importing module's IAT entries (`import_symbol->iat_rva`).
- Relocations supported: `DLL_RELOC_ABS64` and `DLL_RELOC_ABS32` only. Unsupported relocation types cause load failure.
- Module lifecycle: loaded modules are registered with the kernel object manager. Modules maintain `pending_process_attach` and `pending_process_detach` booleans to support DLL entry notifications; `DllLoader::free_library()` can return an `action_handle` (image_base) for deferred detach callbacks.

Image/container format
- See `kernel/include/dll_image.h` for the on-disk/packed runtime format. Important fields:
  - `magic`, `version`, `machine` (must be `DLL_MACHINE_AARCH64`), `image_type` (EXE/DLL/DRIVER).
  - `image_base`, `entry_point_rva`, `image_size`, `header_size`.
  - Section table, import/export tables, relocation table, and string table offsets/sizes.

Practical implications & gotchas to document further
- The MMU/loader only maps whole L2 blocks. Small images will still consume 2 MiB of physical backing and a whole L2 VA slot: this has memory cost.
- The staging buffer limit (`LoaderFileBufferSize`) and `UserModuleSlotSize` set strict maximum binary sizes — document expected developer workflows (e.g., when to split large DLLs).
- The global loaded-module list (`g_module_head`/`tail`) stores `LoadedModule` objects for all processes; each `LoadedModule` has an `owner_process` to restrict visibility. Verify concurrency assumptions if parallel loads are introduced.
- Driver preferred base is offset inside the module region (8-slot offset) — this is an implicit layout choice that should be documented for driver authors that rely on fixed addresses.
- Resolve-imports writes absolute addresses into the module's IAT; third-party build tools must ensure exported symbol names match exactly (case-insensitive compare is used by the loader).

References (code)
- Loader and DLL loader: [kernel/loader/loader.cpp](kernel/loader/loader.cpp#L1) and [kernel/loader/dll_loader.cpp](kernel/loader/dll_loader.cpp#L1).
- Image format: [kernel/include/dll_image.h](kernel/include/dll_image.h#L1).
- MMU backend and L2 constant: [kernel/include/mm/arch/aarch64/mm_backend.h](kernel/include/mm/arch/aarch64/mm_backend.h#L1) and [kernel/include/internal/mm/arch/aarch64/mmu_defs.h](kernel/include/internal/mm/arch/aarch64/mmu_defs.h#L1).
- GUI and shared memory fixed views: [kernel/gui/gui_service.cpp](kernel/gui/gui_service.cpp#L1) and [kernel/shared_memory.cpp](kernel/shared_memory.cpp#L1).

Next steps / suggested additions to repository docs
- Add a short diagram (this repo already contains `docs/loader-flowchart.md`) illustrating the EXE/DLL load sequence and error branches.
- Add a short cookbook on how to build DLLs with the right preferred base, relocation model, and export table expectations.
- Document memory cost (physical and virtual) of loading one DLL (2 MiB aligned), and guidance for bundling multiple logical modules inside a single slot if needed.

If you want, I can convert this into a README-style page inside `docs/` with cross-links and a few examples showing address math for common cases.
