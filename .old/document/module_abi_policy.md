# Module ABI Policy

This document defines the active ABI policy for user-space `.dll` and `.sys` artifacts packed by `tools/my-loader/ldr_build.py` into the LRD0 format.

## Goals

- Make every public module symbol explicit.
- Keep the current LRD0 import table and IAT binding model.
- Avoid symbol-table guessing for ABI decisions.
- Add no new syscall model for DLL calls.
- Avoid adding extra per-module kernel-heap metadata beyond the loader structures that already exist today.

## ABI Policy

The ABI contract is now:

- Only explicitly declared exports are public.
- DLL imports remain explicit through packed LRD0 import records and import address table slots.
- The builder must not synthesize DLL or SYS exports from arbitrary ELF globals.
- Runtime support sources under `applications/runtime/` are implementation details and are never part of a module ABI.

## DLL Exports

DLL exports are declared in source with `ROS_DLL_EXPORT(symbol)` or `ROS_DLL_EXPORT_AS("public_name", symbol)` from `applications/include/ros_user_runtime.h`.

These macros emit one `.ldrmeta.exports` record into the object file. The builder reads that section and uses it as the packed LRD0 export table.

Example:

```c
#include "ros_user_runtime.h"

ROS_DLL_EXPORT(Init);
ROS_DLL_EXPORT(Deinit);
ROS_DLL_EXPORT(sample_lib_calculate_total);
ROS_DLL_EXPORT(sample_lib_invocation_count);
ROS_DLL_EXPORT(sample_lib_profile);
```

## SYS Exports

SYS exports are declared in source with `ROS_SYS_EXPORT(symbol)` or `ROS_SYS_EXPORT_AS("public_name", symbol)`.

Normal `.sys` builds must expose only:

- lifecycle hooks used by the runtime, such as `Init`, `Deinit`, and `DriverLoop`
- explicitly intended driver APIs

No ELF-global fallback is allowed for normal SYS builds.

Example:

```c
#include "ros_user_runtime.h"

ROS_SYS_EXPORT(Init);
ROS_SYS_EXPORT(Deinit);
ROS_SYS_EXPORT(sample_driver_dispatch);
ROS_SYS_EXPORT(sample_driver_last_value);
ROS_SYS_EXPORT(DriverLoop);
```

## Imports

Imports keep the current LRD0 model.

An import record contains:

- the provider module name
- the imported symbol name
- the importing module's IAT slot RVA

At load time the loader:

1. finds the dependency module
2. finds the export inside that dependency
3. writes the resolved absolute target address into the importer's IAT slot

The binding step happens in `kernel/loader/ldr_imports.c`.

This means imports are loader patch points, not a separate invocation mechanism.

## Clean DLL Invocation

The recommended user-mode DLL call pattern is:

1. `ros_shlib_open(path)`
2. resolve typed exports once with a small cache slot
3. call through the typed function pointer
4. `ros_shlib_close(path, base)`

Use the helpers in `applications/include/ros_user_runtime.h`:

- `ROS_DLL_CACHE(cache_name)`
- `ros_shlib_resolve_cached(path, export_name, &cache_name)`
- `ROS_DLL_RESOLVE(path, export_name, function_type, cache_name)`

Example:

```c
typedef void (*hello_one_print_fn)(unsigned long base);

ROS_DLL_CACHE(g_hello_one_print_cache);

unsigned long base = ros_shlib_open("/lib/hello_one.dll");
hello_one_print_fn print_fn = ROS_DLL_RESOLVE("/lib/hello_one.dll", "hello_one_print", hello_one_print_fn, g_hello_one_print_cache);
if (print_fn) {
    print_fn(base);
}
ros_shlib_close("/lib/hello_one.dll", base);
```

The runtime wrapper already auto-calls `Init` on first load and `Deinit` on final close.

## DLL Memory Layout

If a DLL is loaded at `0x200000`, that address is the start of the reserved image range, not the start of an on-image metadata block.

The active runtime layout is:

- `base + section.rva` for each packed section
- `.text` normally starts at `base + 0x0`
- `.rodata`, `.data`, and `.bss` follow at their packed RVAs
- IAT slots live inside the mapped writable sections at the RVAs chosen by the builder

The packed metadata tables are not mapped into user memory as a separate runtime header. The loader parses them and then copies section payloads only.

## Allocation Bookkeeping

The allocation bookkeeping is kernel-side.

Relevant state includes:

- per-task module reservations in `kernel/user-runtime.c`
- the loaded module graph and parsed import/export arrays in the loader runtime
- per-task DLL-local blocks in the `0x800000` to `0xC00000` range

This ABI step does not add any new per-module kernel-heap metadata beyond the loader structures already required to track live modules. The explicit source annotations are a build-time declaration mechanism, not a new runtime metadata store.

## Validation Rules

When changing a DLL or SYS module ABI:

1. rebuild with `make -B applications`
2. inspect `output/my-loader/*.manifest.json`
3. verify the visible exports match the annotated symbols only
4. boot and validate load, call, and unload paths

## Migration Rules

- New DLLs must declare exports with `ROS_DLL_EXPORT` or `ROS_DLL_EXPORT_AS`.
- New SYS modules must declare exports with `ROS_SYS_EXPORT` or `ROS_SYS_EXPORT_AS`.
- Do not rely on non-static global symbols becoming public automatically.
- Treat `applications/runtime/` as private implementation support.
