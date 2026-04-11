
# AGENTS.md

## AArch64 Microkernel – Kernel extension Loader (Flat extension Format)

## 1. Project Overview

This project implements a custom AArch64 microkernel-style operating system with support for dynamically loaded kernel drivers (extension).

System characteristics:

- Architecture: ARM64 / AArch64
- Kernel runs at EL1
- User processes run at EL0
- Drivers/extension run at EL1
- Single kernel virtual address space
- Drivers loaded after kernel boot
- Custom flat extension format (not ELF for now)
- No dynamic linker
- No extension unload yet
- No per-driver address spaces yet

extension are loaded into a reserved kernel virtual address region and linked using a kernel export symbol table.

---

## 2. Exception Levels

| Level | Purpose |
| ------ | --------- |
| EL0 | User applications |
| EL1 | Kernel + Drivers |
| EL2 | Hypervisor (not used yet) |
| EL3 | Secure monitor (firmware) |

Drivers run inside kernel space at EL1.

---

## 3. Kernel Virtual Memory Layout

Keep the same address layout as is unless necessary to change. Reserve a memory region to load extensions and its data.

Rules:

1. Kernel and extension share EL1 address space
2. extension only mapped inside extension region
3. MMIO mapped separately
4. extension text RX, data RW
5. Never identity-map extension

---

## 4. Kernel extension Design

extension:

- Run in EL1
- Share kernel address space
- Use kernel exported functions
- Can register interrupts
- Can map device memory
- Must not access non-exported kernel symbols

extension are not user processes.

---

## 5. Flat extension Format (v1)

File Layout:
[extension_header]
[section_table]
[import_table]
[relocation_table]
[text]
[rodata]
[data]

### extension Header

```c
#define MOD_MAGIC 'ROSMOD\0'

struct mod_header {
    char magic[8];
    uint16_t abi_version;
    uint16_t machine;
    uint32_t flags;

    uint32_t header_size;
    uint32_t section_count;
    uint32_t import_count;
    uint32_t reloc_count;

    uint32_t entry_section;
    uint32_t entry_offset;

    uint32_t image_size;
    uint32_t bss_size;

    uint64_t align;
};
```

### Section Entry

```c
enum mod_section_type {
    MOD_SEC_TEXT,
    MOD_SEC_RODATA,
    MOD_SEC_DATA
};

struct mod_section {
    uint32_t type;
    uint32_t flags;
    uint64_t file_offset;
    uint64_t file_size;
    uint64_t mem_size;
    uint64_t align;
};
```

### Import Entry

```c
struct mod_import {
    uint32_t name_offset;
    uint32_t type;
    uint64_t patch_offset;
};
```

### Relocation Entry

```c
enum mod_reloc_type {
    MOD_RELOC_ABS64,
    MOD_RELOC_REL64,
    MOD_RELOC_SECTION
};

struct mod_reloc {
    uint32_t type;
    uint32_t section;
    uint64_t offset;
    uint64_t addend;
};
```

---

## 6. Kernel Export Table

```c
struct kernel_export {
    const char *name;
    void *addr;
};
```

Example exports:

- klog
- kmalloc
- kfree
- map_mmio
- register_irq
- enable_irq
- sleep_ms
- event_wait
- event_signal

---

## 7. extension Loader Flow

Loader steps:

1. Read extension file
2. Validate header
3. Validate tables
4. Build memory layout
5. Allocate extension memory
6. Copy sections
7. Zero BSS
8. Apply relocations
9. Resolve imports
10. Set memory permissions
11. Flush instruction cache
12. Register extension
13. Call extension entry
14. Store extension state

Abort if any step fails.

---

## 8. extension Entry Interface

```c
int extension_init(const struct kernel_api *api);
void extension_exit(void);
```

Kernel API example:

```c
struct kernel_api {
    void (*klog)(const char*);
    void* (*kmalloc)(size_t);
    void (*kfree)(void*);
    void* (*map_mmio)(uint64_t phys, size_t size);
    int (*register_irq)(int irq, void (*handler)(void));
};
```

---

## 9. extension Subsystem Files

kernel/extension/

- extension_loader.c
- extension_validate.c
- extension_vm.c
- extension_reloc.c
- extension_import.c
- extension_registry.c
- extension_format.h

---

## 10. Implementation TODO - if not done.

Phase 1 – Memory

- Reserve extension VA region
- extension_vm_alloc()
- extension_vm_protect()

Phase 2 – Format

- Header structs
- Section structs
- Import structs
- Relocation structs

Phase 3 – Validator

- Validate magic
- Validate ABI
- Validate machine
- Validate offsets

Phase 4 – Loader

- Allocate memory
- Copy sections
- Zero BSS
- Entry address

Phase 5 – Relocations

- ABS64
- REL64
- SECTION

Phase 6 – Imports

- Export table
- Symbol lookup
- Patch addresses

Phase 7 – Execution

- Set RX/RW
- Flush I-cache
- Call extension_init()

Phase 8 – Registry

- extension list
- extension info

Phase 9 – Tools

- extension packer
- extension dump
- Hello extension

---

## 11. Security Rules

Reject extension if:

- Invalid header
- Unknown ABI
- Unknown relocation
- Import not found
- Sections overlap
- Entry outside extension memory
- Image too large

Never trust extension input.

---

## 12. Coding Rules

- No magic numbers
- Validate all inputs
- Log loader steps
- Separate validation/loading/relocation/import
- Use clear structs
- Document extension format
- Keep ABI versioned
- Clearly comment functions, what it does
- Use inline comment for procedure calls
- Use single line comment for code blocks

---

## 13. Future Features (Not Now)

Do NOT implement yet:

- ELF loader
- Dynamic linker
- extension unload
- Symbol versioning
- Driver isolation
- User-space drivers
- IPC framework
- SMP extension locking
- Live patching

---

## 14. Milestones

1. Load extension and call entry
2. Import kernel symbols
3. Relocations working
4. Memory protections
5. UART driver extension
6. Interrupt driver extension
7. Storage driver extension
8. Freeze ABI v1

---

## 15. Architecture Rule Summary

EL0 → User Applications  
EL1 → Kernel + Drivers  

Kernel Address Space:
Kernel
Heap
Stacks
extension
MMIO

extension are part of the kernel, not user programs.

## 16. Kernel Behavior

- Automatic load and init all extensions.
- Add extension's loop to kernel loop.
