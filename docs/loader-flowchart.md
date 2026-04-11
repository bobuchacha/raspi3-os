# Loader flowchart — EXE and DLL load sequence

This file contains a Mermaid flowchart that documents the detailed control flow used by the kernel to load EXE images and DLL/driver modules.

```mermaid
flowchart TD
  A[Start: Loader::spawn_user_process(path)] --> B[ProcessManager::create_user_process(name) -> new AddressSpace]
  B --> C[allocate stack backing (L2 block)]
  C --> D[DllLoader::load_user_executable(path)]
  D --> E[read_file_into_staging(path) -> g_loader_file_buffer]
  E --> F[parse_image_view() -> validate magic, version, machine, image_type==EXE]
  F --> G[allocate image_backing (L2 block, UserModuleSlotSize/L2 alignment)]
  G --> H[copy_image_to_backing(view, image_backing)]
  H --> I[apply_image_relocations(view, image_backing, base=UserExecutableBase)]
  I --> J[resolve_image_imports(process, image_backing) -- for each import: ensure_library_loaded()]
  J --> K[create image VmMapping: virtual=UserImageBase -> physical=kernel_to_physical(image_backing)]
  K --> L[mm::MemoryManager::map(&process->process_address_space, image_mapping)]
  L --> M[mm::MemoryManager::map(&process->process_address_space, stack_mapping at UserStackBase)]
  M --> N[ThreadManager::create_user_thread(process, entry_point, UserStackTop) -> user thread]
  N --> Z[Process starts running at entry]

  %% DLL/driver load branch (called during resolve_image_imports or user request)
  J --> O{ensure_library_loaded(process, module_path)}
  O -->|module present| P[find_module_by_path() -> KernelObjectManager::reference_object() -> return module_handle]
  O -->|not present| Q[load_new_module(process, module_path)]
  Q --> R[normalize_module_path + load_image_file into staging buffer]
  R --> S[parse_image_view() -> validate image_type==DLL|DRIVER and image_size <= UserModuleSlotSize]
  S --> T[preferred_base = header->image_base or defaults]
  T --> U[choose_module_base(process, preferred_base) -> find free slot in UserModuleRegionBase..]
  U --> V[alloc backing (Heap::alloc(UserModuleSlotSize, L2))]
  V --> W[copy_image_to_backing(view, backing) -> apply_image_relocations(view, backing, actual_base)]
  W --> X[resolve_image_imports(process, backing)  (recursive)]
  X --> Y[create_loaded_module(process, path, backing, image_base) -> KernelObjectManager::register_object()]
  Y --> AA[create_module_mapping(process, module) -> mm::MemoryManager::map(module->image_base)]
  AA --> P

  %% Error paths (illustrative)
  E -->|file too large| ERR1[FAIL: file exceeds staging buffer]
  F -->|invalid header| ERR2[FAIL: invalid image magic/version/machine]
  S -->|image too large| ERR3[FAIL: image exceeds slot size]
  W -->|unsupported reloc| ERR4[FAIL: unsupported relocation type]

```

Notes:
- The loader enforces L2 alignment and L2-sized mappings for images and stacks. See `kernel/loader/dll_loader.cpp` and `kernel/loader/loader.cpp` for constants and implementation.
- IAT (import address table) entries are written by `resolve_image_imports()` once dependency exports are available.
- `ensure_library_loaded()` both finds existing modules and invokes `load_new_module()` which performs normalization, loading, relocation, and mapping.
