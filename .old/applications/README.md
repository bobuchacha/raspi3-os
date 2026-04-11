# Application Layout

The active userspace tree is now organized around three artifact classes only:

```text
applications/
|---- apps/
|     |---- init/
|           |---- main.c
|---- libs/
|     |---- sample_lib/
|           |---- user_lib.c
|---- drivers/
|     |---- sample_driver/
|           |---- kernel_driver.c
|---- include/
|     |---- user_runtime.h
|     |---- sample_abi.h
|---- _templates/
```

## Build Rules

- Every direct child folder under `applications/apps/` builds into one `.exe` artifact.
- Every direct child folder under `applications/libs/` builds into one `.dll` artifact.
- Every direct child folder under `applications/drivers/` builds into one `.sys` artifact.
- `make applications` packs all three classes through `tools/my-loader/ldr_build.py`.
- `make applications-vfs` stages only these packed artifacts plus `/roskrnl` into `fat32.img`.

Current sample outputs:

- `output/my-loader/init.exe`
- `output/my-loader/sample_lib.dll`
- `output/my-loader/sample_driver.sys`

These land in the FAT image as:

- `/bin/init.exe`
- `/lib/sample_lib.dll`
- `/system/sample_driver.sys`

## Runtime Model

- `init.exe` is the default PID 1 sample and exercises the loader bridge on boot.
- `sample_lib.dll` is a normal userspace shared library whose exports are resolved through `SYS_SHLIB_EXPORT`.
- `sample_driver.sys` is also loaded into user address space and behaves like a driver-shaped userspace service module.

The shared header `applications/include/user_runtime.h` provides the minimal syscall wrappers used by all three sample artifacts.

The shared runtime support sources under `applications/runtime/` are linked into every user artifact automatically. They provide:

- a minimal freestanding C runtime surface for `stdlib.h`, `stdio.h`, and `string.h`
- syscall bridge implementations for `applications/include/app/syscall.h`
- a small C++ runtime for `new`, `delete`, pure-virtual traps, and init/fini array helpers

## Scaffolding

- `make new-app NAME=my_app [APP_LANG=c|cpp]`
- `make new-lib NAME=my_lib [LIB_LANG=c|cpp]`
- `make new-dll NAME=my_lib [DLL_LANG=c|cpp]`
- `make new-sys NAME=my_driver [SYS_LANG=c|cpp]`

The scaffold targets now create folders under `applications/apps`, `applications/libs`, and `applications/drivers` respectively. `new-lib` and `new-dll` both create shared-library sources under `applications/libs` plus a generated public header under `applications/include/app/`.
