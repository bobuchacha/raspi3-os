# Bootloader

This directory holds the custom second-stage bootloader that now lives inside the kernel tree while remaining a separate binary image.

## Responsibilities

- Perform the smallest possible board-specific bring-up needed to load the kernel.
- Validate the kernel image format and any optional initrd payloads.
- Publish a stable boot handoff block for the kernel.
- Transfer control to the higher-half kernel entry point.

## Layout

```text
kernel/bootloader/
|-- include/          bootloader-public contracts and boot handoff definitions
|-- arch/             ISA-level boot entry code
|-- fs/               boot-time filesystem readers
|-- image/            kernel image parsing and relocation helpers
|-- linker/           linker scripts for bootloader artifacts
`-- platform/
    |-- board/        board-family specific boot code
    `-- cpu/          CPU-family specific cache, timer, and errata hooks
```
