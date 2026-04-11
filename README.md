# raspi3-os

Clean-room rewrite of a small 64-bit operating system for multiple embedded boards and emulated targets.

## Goals

- Keep the kernel permanently mapped in the higher half of the virtual address space.
- Keep user mappings in the lower half so service calls do not need a separate syscall remap window.
- Separate ISA-level code from board and CPU-family bring-up.
- Keep the second-stage bootloader independent from the kernel binary while storing its sources inside the kernel tree.
- Make public kernel interfaces easy to include and private headers easy to contain.

## Repository Layout

```text
docs/               design notes for boot flow and memory layout
kernel/             higher-half kernel sources and second-stage bootloader
tools/              image packing, flashing, emulation, and inspection helpers
.old/               archived code kept only as reference during the rewrite
```

## Kernel Layout

```text
kernel/
|-- bootloader/             second-stage bootloader sources and handoff contracts
|-- include/
|   |-- types.h             shared scalar and address-space types
|   |-- arch.h              architecture interface
|   |-- device.h            device subsystem interface
|   |-- filesystem.h        filesystem driver interface
|   |-- vfs.h               virtual filesystem interface
|   |-- scheduler.h         scheduler interface
|   |-- loader.h            executable and module loader interface
|   |-- mm.h                memory-management interface and VA split
|   |-- heap.h              heap allocator interface
|   |-- platform.h          board and CPU composition interface
|   `-- internal/           subsystem-private headers
|-- arch/                   ISA-specific entry, traps, and low-level helpers
|-- device/                 device core and reusable drivers
|-- filesystem/             on-disk filesystem drivers
|-- heap/                   kernel heap allocator implementations
|-- loader/                 executable and module loading
|-- mm/                     physical and virtual memory management
|-- platform/
|   |-- board/              board-family bring-up
|   `-- cpu/                CPU-family quirks, timers, and errata hooks
|-- scheduler/              thread and run-queue management
|-- service/                service call entry and dispatch
`-- vfs/                    namespace and file-handle layer
```

## Boot Flow

The expected boot chain is:

1. Board firmware or emulator entry.
2. Custom second-stage bootloader in `kernel/bootloader/`.
3. Bootloader loads the kernel image, prepares a handoff block, and jumps into the higher-half kernel entry.
4. Kernel initializes architecture, memory management, platform, devices, VFS, loader, and scheduler.
5. User code enters the kernel through service calls.

See [docs/boot-flow.md](docs/boot-flow.md) and [docs/memory-layout.md](docs/memory-layout.md) for the current baseline rules.
