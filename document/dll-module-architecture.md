# DLL And Module Architecture

This note answers three design questions together because they touch the same boundary: execution context.

## 1. Can kernel modules call DLLs?

Not directly in the current architecture, and keeping that separation is the correct default.

Why:

- Kernel modules run at EL1 in the kernel address space.
- DLLs are user-space images loaded into an EL0 task address space.
- The DLL loader maps pages into a specific task and returns a user virtual entry address.
- A kernel module cannot safely treat that user virtual address as a callable kernel function pointer.

Current code confirms that split:

- `applications/common/dll.ld` links DLLs in a user shared-library range starting at `0x200000`.
- `kernel/user-exe.c` loads a DLL into shared physical pages and maps those pages into a task at user virtual addresses.
- `applications/include/app/demo-shared.h` shows the ABI: a user program calls `user_kernel_open_shared_library(...)`, receives an entry pointer, and then calls into the DLL from EL0.
- `applications/include/app/kernel_module.h` shows the kernel-module ABI is a separate EL1-facing API table.

So there are really two different goals:

1. Reuse code across user programs through DLLs.
2. Reuse code across kernel modules through kernel-side relocatable libraries.

Those should not be merged into one binary artifact unless the kernel grows a full cross-address-space call bridge. That bridge would be RPC, not a normal function call.

### Recommended rule

- User DLLs are callable by user programs only.
- Kernel modules may import kernel exports or future kernel-side shared libraries only.
- If the same logic is needed in both places, share source code, not a single loaded binary image.

## 2. How should relocatable DLLs work?

The current DLL pipeline is intentionally fixed-base and simple.

Current state:

- DLLs are linked with `applications/common/dll.ld` at `0x200000`.
- `tools/pack_user_exe.py` accepts `ET_EXEC` and preserves only `PT_LOAD` segments.
- `kernel/user-exe.c` validates the packed header and maps pages at the linked virtual addresses.
- No relocation records survive packing, so the loader has nothing to apply.

That means true dynamic rebasing is impossible today by design.

### Recommended redesign

Make user DLLs use the same broad model already used by kernel modules:

1. Build DLLs as PIC shared ELF
2. Preserve relocations and imports in the packed image
3. Let the loader choose a runtime base
4. Apply relocations after mapping
5. Resolve imports against an explicit export table

### Concrete format direction

Do not extend the current `ROSXEXE` format for relocatable DLLs. Keep that format for simple executables.

Instead, add a dedicated flat DLL format, parallel to the flat module format:

- header
- section table
- import table
- relocation table
- export table
- payload sections

Conceptually:

```c
struct FlatDllHeader {
    char magic[8];
    uint16_t abi_version;
    uint16_t machine;
    uint32_t flags;
    uint32_t header_size;
    uint32_t section_count;
    uint32_t import_count;
    uint32_t reloc_count;
    uint32_t export_count;
    uint32_t entry_section;
    uint32_t entry_offset;
    uint64_t image_size;
    uint64_t preferred_base; /* optional hint, not a requirement */
};
```

Reuse the same relocation kinds already handled by the kernel-module packer where possible:

- absolute 64-bit
- relative 64-bit
- section-relative relocations
- import patch relocations

### The key semantic decision: what is shared?

This matters more than the relocation code.

For user DLLs, the clean model is:

- `.text` and `.rodata`: globally shared across tasks
- `.data` and `.bss`: per-task private copies
- explicit shared state: opt-in, via a separate API or named shared-memory object

Why this is better than the current model:

- Today, writable DLL pages are physically shared across all tasks, so global writable state is implicitly shared.
- That is simple, but it is not normal DLL behavior and it makes isolation harder.
- A relocatable loader is the right time to fix that semantic mismatch.

### Loader outline

For a relocatable DLL loader:

1. Load and validate the flat DLL metadata.
2. Allocate global backing pages for `.text` and `.rodata`.
3. Copy read-only sections.
4. For each task opening the DLL:
   - allocate task-local `.data` and `.bss`
   - map shared `.text` and `.rodata`
   - map private writable sections
   - apply relocations against the chosen task-local base layout
   - resolve imports
5. Return the entry function address inside that task's mapping.

That preserves code sharing while making writable state sane.

## 3. Do modules and DLLs need JSON manifests?

No. JSON is a tooling choice, not a runtime requirement.

Current state:

- Kernel modules use `module.json` as packer input.
- The JSON tells the packer the module name, lifecycle symbols, and exported functions.
- DLLs currently do not use a separate manifest file; their ABI is implicit in the exported `_shared_library_entry` convention and the public C header.

### Better long-term direction

Move metadata into an ELF section emitted by source code or linker script, then let the packer read that section.

That gives these benefits:

- metadata stays next to the code it describes
- symbol names can be validated directly against the same object file
- scaffolding becomes easier
- the build no longer depends on an extra JSON file staying in sync

### Recommended metadata model

Use C macros to emit a packed metadata record into a dedicated section.

Example direction for kernel modules:

```c
typedef struct RosModuleExportDecl {
    const char *name;
    const void *symbol;
} RosModuleExportDecl;

typedef struct RosModuleMetadata {
    const char *name;
    const void *init;
    const void *shutdown;
    const void *idle;
    uint32_t export_count;
    const RosModuleExportDecl *exports;
} RosModuleMetadata;

#define ROS_MODULE_METADATA_SECTION __attribute__((section(".ros.module.meta"), used))
```

And for DLLs:

```c
typedef struct RosDllMetadata {
    const char *name;
    const void *entry;
    uint32_t flags;
} RosDllMetadata;

#define ROS_DLL_METADATA_SECTION __attribute__((section(".ros.dll.meta"), used))
```

The packers would then:

1. read the ELF metadata section
2. resolve the referenced symbols
3. emit the packed flat image format

### Transition strategy

Support both forms for a while:

- `module.json` or embedded `.ros.module.meta`
- future `dll.json` or embedded `.ros.dll.meta`

Then remove JSON when the embedded path is stable.

That avoids a flag day.

## 4. Recommended implementation order

Do the redesign in this order.

### Step 1: Keep the execution-context split

- Do not make EL1 kernel modules call EL0 DLL entry pointers.
- Document that user DLLs are for user programs.

### Step 2: Introduce embedded metadata

- Add source-embedded metadata sections for kernel modules first.
- Make `tools/pack_kernel_module.py` accept either JSON or embedded metadata.

### Step 3: Add a dedicated relocatable DLL format

- Keep `ROSXEXE` for executables.
- Add a new flat DLL packer/loader pair.
- Build DLLs from PIC shared ELF, not fixed-base `ET_EXEC`.

### Step 4: Fix writable-section semantics

- Share `.text` and `.rodata` globally.
- Make `.data` and `.bss` per-task.
- Keep explicit per-task local storage only for DLL-managed custom state that is separate from normal global data.

### Step 5: If kernel-side library reuse is still needed

- Introduce kernel shared libraries as a separate EL1 format.
- Reuse the same flat relocation concepts, but not the user DLL loader.

## 5. Bottom line

- Kernel modules should not directly call user DLLs.
- Relocatable DLLs are a good idea, but they need a new flat DLL format and loader, not a small tweak to the current fixed-base path.
- JSON is optional. Embedded metadata sections are the better long-term interface.
- The module system already provides the reference model for relocations. Use that model for DLL packing and relocation, but keep the user-space and kernel-space loaders separate.