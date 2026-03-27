# Paired Application And DLL Template

This example shows the full path for a small application opening a DLL and calling an exported API.

Files:

- `app/main.c`: example program that opens `app/__DLL_NAME__.h`
- `dll/include/app/template-dll.h`: public DLL header template
- `dll/main.c`: DLL implementation and exported entrypoint

Use placeholder replacement for:

- `__APP_NAME__`
- `__DLL_NAME__`
- `__DLL_NAME_UPPER__`

The `make new-app-pair APP_NAME=... DLL_NAME=...` target performs that replacement automatically.
