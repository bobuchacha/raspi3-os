# Application Framework

Everything needed to build an application program or DLL now lives under `applications/`. Application code should not need headers from `kernel/include/`.

## Layout

```text
applications/
|---- include/
|     |---- stdio.h             minimal stdio-style API for applications
|     |---- stdlib.h            allocation and process-exit helpers
|     |---- string.h            small string and memory utility layer
|     |---- logger.h            colored application logger for UART output
|     |---- app/
|           |---- syscall.h     application syscall ABI declarations
|           |---- kernel.h      C runtime wrappers around syscalls
|           |---- kernel.hpp    C++ runtime wrappers around syscalls
|           |---- demo-shared.h sample DLL API header
|---- common/
|     |---- link.ld             linker script for executables
|     |---- dll.ld              linker script for shared libraries
|     |---- startup.c           shared process entrypoint
|---- lib/
|     |---- printf.c            stdio-style formatter implementation
|     |---- string.c            memcpy/memset/strlen family
|     |---- stdlib.c            malloc/free/calloc/exit helpers
|     |---- logger.c            TRACE/DEBUG/INFO/WARN/ERROR logger
|     |---- syscall.S           syscall stubs
|     |---- cxx_runtime.cpp     freestanding C++ runtime support
|---- programs/
|     |---- init/
|     |     |---- main.c
|---- dlls/
|     |---- demo/
|           |---- main.c
```

## Create A Program

1. Create a new folder under `applications/programs/<name>/`.
2. Add a `main.c` or `main.cpp` file.
3. Include application headers only, for example `#include "app/kernel.h"`, `#include "stdio.h"`, and `#include "logger.h"`.
4. Build with `make applications` or `make all`.

Copy-ready templates live under `applications/_templates/` and are ignored by the build.
You can scaffold one automatically with `make new-app NAME=my_app [APP_LANG=c|cpp]`.

Minimal example:

```c
#include "app/kernel.h"
#include "logger.h"

long main(void)
{
    app_log_info("my_app", "hello from application");
    return 0;
}
```

The build system links every direct child folder of `applications/programs/` into `output/applications/elf/<name>.elf`, then packs it into `output/applications/programs/<name>.exe` and copies it into `/bin` inside `fat32.img`.

## Create A DLL

1. Create a new folder under `applications/dlls/<name>/`.
2. Add the implementation source file.
3. Export `_shared_library_entry` if you want the legacy table-based entrypoint, and publish any direct-call symbols as non-`static` functions so the packer can export them by name.
4. In consumer programs, include `app/dll_import.h`, then choose one of these models:
    - Use `DLL_IMPORT("/lib/name.dll", "symbol", ret_type, wrapper_name, (args...), (arg_values...))` to resolve a named export directly from a user DLL image.
    - Use `MODULE_IMPORT("module", "export", ret_type, wrapper_name, (args...), (arg_values...))` for kernel modules that expose the two-argument invoke ABI.
    - Use `ROS_DLL_ACCESSOR(...)` plus `DECLARE(...)` or `DECLARE_AS(...)` only when you intentionally want the legacy API-table path.
5. Build with `make applications` or `make all`.

Example:

```c
typedef struct AlphaSharedApi {
    unsigned long abi_version;
    void (*announce)(const char* caller);
} AlphaSharedApi;

typedef const AlphaSharedApi* (*AlphaSharedEntry)(void);

DLL_IMPORT("/lib/alpha.dll", "alpha_add_alpha_bias", long, alpha_add_alpha_bias, (long value), (value))
DLL_IMPORT_VOID("/lib/alpha.dll", "alpha_announce", alpha_announce, (const char* caller), (caller))

// For kernel modules that use the invoke ABI:
MODULE_IMPORT("fat32", "probe", long, fat32_probe, (void), (0, 0))
```

The short form you suggested is the right mental model, but standard C still needs the return type and argument names in the macro invocation.

`DLL_IMPORT(...)` and `DLL_IMPORT_VOID(...)` resolve direct exports from user DLL images. `MODULE_IMPORT(...)` covers the separate kernel-module invoke ABI, which accepts two integer arguments and returns a `long`.

Copy-ready DLL templates live under `applications/_templates/dll/`.
You can scaffold one automatically with `make new-dll NAME=my_dll`.

Each direct child folder of `applications/dlls/` is linked into `output/applications/dll-elf/<name>.elf` as a PIC shared ELF, then packed into `output/applications/dll/<name>.dll` as a flat relocatable DLL image and copied into `/lib` inside `fat32.img`.

Current DLL limitation:

- DLLs do not run executable-style `.init_array` startup. Prefer explicit initialization from `_shared_library_entry` or another exported function instead of relying on global constructors.

## Create A Paired Program And DLL

Use the paired template under `applications/_templates/paired/` when you want one example program that opens a DLL and calls it immediately.

You can scaffold the pair automatically with `make new-app-pair APP_NAME=my_app DLL_NAME=my_dll`.

That command creates:

- `applications/programs/my_app/main.c`
- `applications/include/app/my_dll.h`
- `applications/dlls/my_dll/main.c`

## Build Boundaries

- Application compilation searches `applications/include` only.
- Kernel headers live under `kernel/include/` and are not part of the application SDK.
- Shared ABI headers that application code needs must be published under `applications/include/app/`.
- Folders beginning with `_` under `applications/` are ignored, so `applications/_templates/` is safe for scaffolding.

## System Extensions (.sys)

System extensions are now treated as native kernel modules rather than repacked user executables.

Workflow:

- Place module sources in `applications/system/<name>/module.c`.
- Embed metadata with `app/kernel_module.h` in the module source.
- Optionally keep `applications/system/<name>/module.json` as a fallback manifest.
- Build with `make applications`.
- Scaffold a new module with `make new-sys NAME=my_sys`.

Build output:

- `output/applications/system-elf/<name>.elf`: PIC shared ELF image
- `output/applications/system/<name>.sys`: final bundle copied into `/system` on the FAT32 image during `make applications-vfs`

The build compiles system modules with `-fPIC`, links them with `ld -shared`, and bundles the result with `tools/pack_kernel_module.py`.

Use `app/kernel_module.h` for the module entry ABI. A module provides embedded lifecycle metadata and can publish up to four user-callable exports from the same metadata block. `module.json` remains accepted as a fallback when embedded metadata is absent.

Sample:

- `applications/system/sample_sys/module.c`
- `applications/system/sample_sys/module.json`
- `applications/include/app/sample_sys.h`
- `applications/programs/samplequery/main.c`
- Logger colors are ANSI escape codes written over UART. They render when the serial consumer understands ANSI, such as QEMU with `-serial stdio` in a normal terminal.
