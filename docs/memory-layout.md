# Memory Layout

## Virtual Address Policy

- User address space stays in the lower canonical region.
- Kernel address space stays in the higher canonical region.
- Service calls run with the kernel already mapped, so the transition path does not need a syscall remap trampoline.

## Current Baseline Split

```text
0x0000_0000_0000_0000 - 0x0000_FFFF_FFFF_FFFF    user space
0xFFFF_0000_0000_0000 - 0xFFFF_FFFF_FFFF_FFFF    kernel space
```

This matches a simple 48-bit higher-half layout and leaves room to evolve page-table policy later.

## Early Rules

- The kernel image should link against the higher-half base.
- Physical memory ownership and memory maps come from the boot handoff block.
- Board-specific memory carve-outs stay under `kernel/platform/board/<board>/` instead of leaking into common MM code.
