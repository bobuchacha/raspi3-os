# Applications

Userspace is organized around small C++ executable fronts, DLL-backed runtime
libraries, and one built-in bootstrap layer.

## Tree

```text
applications/
|-- apps/                 executable fronts that define `main()`
|-- libs/                 DLL fronts and shared reusable user libraries
|-- runtime/              built-in startup, syscall, heap, loader, and C++ runtime glue
|-- include/
|   |-- app/app.h         umbrella application header
|   `-- ...               DLL and runtime headers
`-- assets/               staged user-visible assets copied into the VFS image
```

## Entry Model

- Every executable now enters through `applications/runtime/ros_support.c:_start`
  and then runs the application-owned `main()`.
- `applications/runtime/cpp_runtime.cpp` provides constructor/destructor array
  handling plus the minimal `new`/`delete` support needed by C++ application
  fronts and DLL fronts.
- DLL entrypoints remain explicit loader symbols such as `samplemath_entry` and
  use `extern "C"` on exported/public symbols so the packer sees stable names.

## Runtime Split

Code stays built in only when it is part of process bootstrap or the loader ABI:

- syscall wrappers
- heap access
- import/export attachment
- executable `_start`
- shared C++ constructor/destructor runtime

Everything else should prefer a DLL surface:

- `window.dll`
- `widgets.dll`
- `gdi.dll`
- `crt.dll`
- `samplemath.dll`
- other reusable user libraries

## Include Convention

Use `app/app.h` as the single top-level include for application fronts.

Feature gates keep the umbrella header from importing every DLL into every EXE:

- `ROS_APP_WITH_CRT`
- `ROS_APP_USE_WINDOW`
- `ROS_APP_USE_GDI`
- `ROS_APP_USE_WIDGETS`
- `ROS_APP_USE_PNG`
- `ROS_APP_USE_EXPLORER_SHELL`

Example:

```cpp
#define ROS_APP_USE_WINDOW 1
#define ROS_APP_USE_GDI 1
#include "app/app.h"

int main(void) {
    return 0;
}
```

## Naming

- Exported DLL names should use CamelCase.
- Non-exported helpers should stay file-local and avoid module prefixes unless
  the prefix adds real clarity.
