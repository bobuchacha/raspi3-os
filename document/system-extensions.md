# System Extensions Design

This document is now a compatibility note.

The old design treated `/system/*.sys` as repacked user executables with an `extension.json` descriptor stub and a fake standalone `main()` path. That model is deprecated.

The current redesign is documented in:

- `document/module-architect.md`

New direction:

- `.sys` is a kernel module bundle, not a user executable
- each module uses `module.json`, not `extension.json`
- each module exposes one manifest-defined entry symbol
- modules are built as PIC shared ELF images and then wrapped by `tools/pack_kernel_module.py`
- any userspace-facing ABI should be registered explicitly through the kernel, not by directly invoking arbitrary module symbols
