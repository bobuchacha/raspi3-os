# How The Loader Determines Load Address

This document explains how `my-loader` currently decides where a module is loaded in memory, and how build-time RVAs from `ldr_build.py` are converted into runtime virtual addresses.

## 1. Terms
- `base`: Runtime module base returned by VM reserve/commit APIs.
- `rva`: Relative Virtual Address inside one module image.
- `va`: Absolute runtime address computed as `base + rva`.
- `image_size`: Total virtual span of all loadable sections in one image.

Formula used throughout loader runtime:

```text
va = base + rva
```

## 2. Runtime Address Selection (`ldr_vm_map_image`)
Source: `src/ldr_vm.c`

### 2.1 Input used to choose address
The loader uses these fields from parsed module metadata:
- `module->preferred_base`
- `module->image_size`
- `module->kind` (`EXE`, `DLL`, `SYS`)

### 2.2 User vs kernel mapping policy
- For `.sys` (`LDR_IMAGE_SYS`): loader sets `LDR_VM_KERN`
- For `.exe` / `.dll`: loader sets `LDR_VM_USER`

### 2.3 Reserve then commit flow
The loader does the following in order:
1. `vm_reserve(preferred_base, image_size, flags, &base)`
2. `vm_commit(base, image_size, flags)`
3. On success, stores `module->base = base`

If reserve fails, load fails with `LDR_E_VM`.
If commit fails, loader releases the region and returns `LDR_E_VM`.

### 2.4 Important current behavior
Current code does **not** perform fallback base retry logic in loader core.
- If `preferred_base` cannot be reserved by kernel VM, loader returns failure.
- Address randomization / alternative-base policy is expected to be handled in kernel VM callback implementation, or added in a future loader revision.

## 3. Section Placement (`ldr_sections_load`)
Source: `src/ldr_sections.c`

For each section:
1. Compute destination: `dst = base + section.rva`
2. Validate bounds: `section.rva + section.virt_size <= image_size`
3. Copy file bytes from artifact to `dst`
4. Zero-fill `virt_size - file_size` (BSS tail)
5. Apply section protections using `vm_protect`

So section load addresses are fully determined by:
- the selected `base`
- per-section `rva`

## 4. Relocation Effect On Addresses (`ldr_reloc_apply`)
Source: `src/ldr_reloc.c`

For each relocation (`LDR_RELOC_ABS64`):
1. Patch site is `patch_addr = base + patch_rva`
2. Value written is `base + addend`

That means relocation values are directly anchored to runtime `base`.

## 5. Import Address Table (IAT) Writes (`ldr_imports_bind`)
Source: `src/ldr_imports.c`

For each import:
1. Resolve dependency module by name
2. Resolve symbol to absolute address in exporter module
3. Write into importer IAT slot at `base + iat_rva`

Import slot location is therefore also determined by selected `base` and packed `iat_rva`.

## 6. Build-Time RVA Generation (`tools/my-loader/ldr_build.py`)
Source: `tools/my-loader/ldr_build.py`

The builder controls `rva` values stored in artifact metadata.

### 6.1 Linked ELF path (preferred)
If linker exists (`aarch64-none-elf-ld`):
- Builder reads ELF section metadata.
- Uses ELF section addresses as RVAs.
- Computes `entry_rva = entry_addr - min_rva`.

### 6.2 Relocatable object fallback (no linker)
If linker is unavailable and exactly one object file exists:
- Builder synthesizes RVAs.
- Starts at `0x1000`.
- Places each section with alignment (`align_up`).
- Computes synthetic `entry_rva` from entry symbol + section synthetic RVA.

### 6.3 Preferred base in current artifacts
Builder currently writes `preferred_base = 0` in packed header.
Effect:
- Kernel VM callback is free to choose any valid address.
- Actual runtime base is entirely VM policy-dependent.

## 7. End-To-End Address Determination
For each module:
1. Builder writes section RVAs and `entry_rva` into artifact.
2. Loader asks VM for `base` using `preferred_base` and `image_size`.
3. Loader maps each section at `base + rva`.
4. Loader applies relocations and imports relative to same base.

So final address identity is:

```text
final runtime address = selected base by VM + build-time RVA
```

## 8. Practical Example
Assume:
- VM returns `base = 0x40000000`
- `.text` section `rva = 0x2000`
- entry `entry_rva = 0x2050`

Then:
- `.text` runtime address = `0x40000000 + 0x2000 = 0x40002000`
- entry runtime PC = `0x40000000 + 0x2050 = 0x40002050`

## 9. Current Limitations (Important)
1. No internal fallback retry list for base conflicts in loader code.
2. No ASLR/random-base policy in loader code.
3. No explicit per-kind preferred base policy (`exe` vs `dll` vs `sys`) yet.
4. Relocations currently support only `LDR_RELOC_ABS64`.

## 10. Recommended Next Improvements
1. Add loader-side base fallback policy:
   - Try `preferred_base`
   - Then try kernel-suggested free ranges
   - Then fail with detailed reason
2. Add per-artifact preferred-base fields in target recipes.
3. Add optional randomized base selection mode in VM callbacks.
4. Add relocation types beyond ABS64 for richer code-gen patterns.

## 11. Key Files
- `src/ldr_vm.c`
- `src/ldr_sections.c`
- `src/ldr_reloc.c`
- `src/ldr_imports.c`
- `tools/my-loader/ldr_build.py`
