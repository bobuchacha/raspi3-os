# Application Templates

These templates are intentionally stored under `_templates` so the build ignores them.

Use them as copy sources when creating a new application program or DLL:

1. Copy one program template into `applications/programs/<name>/main.c` or `applications/programs/<name>/main.cpp`.
2. Copy the DLL template header into `applications/include/app/<name>.h`.
3. Copy the DLL template implementation into `applications/dlls/<name>/main.c`.
4. Adjust the ABI version, exported function names, and VFS path.

Placeholder tokens are available to speed up copy-and-rename work:

- `__APP_NAME__` for folder names, log strings, and paired example messages.
- `__DLL_NAME__` for header names, symbol prefixes, and DLL paths.
- `__DLL_NAME_UPPER__` for macro prefixes and header guards.

The paired example under `applications/_templates/paired/` shows a program opening a DLL and calling its exported API.

You can also generate files automatically:

- `make new-app NAME=my_app [APP_LANG=c|cpp]`
- `make new-dll NAME=my_dll`
- `make new-sys NAME=my_sys`
- `make new-app-pair APP_NAME=my_app DLL_NAME=my_dll`

The build only picks up direct children of `applications/programs/`, `applications/dlls/`, and `applications/system/`, so files kept here are safe examples and never become part of the image.
