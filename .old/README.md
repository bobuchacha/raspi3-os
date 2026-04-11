# raspi3-os

Hobby operating system kernel for Raspberry Pi 3, with the active codebase now split cleanly between `kernel/` and `applications/`.

## Active Source Layout

Only the top-level `kernel/`, `applications/`, and build files participate in the active build. Folders whose names start with `_` are archived experiments and are intentionally ignored.

```text
kernel/
|---- include/
|     |---- platform/           board selectors and CPU wrapper headers
|     |---- arch/               architecture and CPU implementation headers
|     |---- device/             board and peripheral headers
|     |---- ...                 kernel public headers
|---- arch/                     architecture and CPU implementation code
|---- device/                   reusable peripheral and board device drivers
|---- platform/
|     |---- board/              board bring-up and board composition code
|---- filesystem/
|---- hal/
|---- mm/
|---- scheduler/
|---- ...                       generic kernel subsystems

applications/
|---- include/                  application SDK, libc subset, and logger
|---- common/                   shared app linker scripts and startup
|---- lib/                      shared application runtime support
|---- programs/                 one program per folder
|---- dlls/                     one shared library per folder
```

## Target Selection

```sh
make BOARD=raspi3b CPU=cortex-a53 ARCH=aarch64
make run QEMU_MACHINE=raspi3b
```

For the QEMU `virt` target with the ramfb window enabled:

```sh
make run-virt-gfx
```

Once the userspace shell is up, run `guidemo` to render mirrored keyboard text and a software-injected pointer path inside the framebuffer GUI. Typed shell input is mirrored into the GUI on both `raspi3b` and `virt`, but real host mouse/touch events are not wired on `virt` yet because the guest still lacks a USB/virtio HID driver.

To port the kernel to another board or CPU, add a new board wrapper under `kernel/include/platform/board/`, add CPU wrappers under `kernel/include/platform/cpu/`, and add a board bring-up file under `kernel/platform/board/`.

## Applications

Applications now build against their own freestanding SDK under `applications/include/`.

- `stdio.h`, `stdlib.h`, and `string.h` provide a small custom libc surface.
- `logger.h` provides TRACE/DEBUG/INFO/WARN/ERROR logging.
- `app/kernel.h` and `app/kernel.hpp` wrap syscalls for C and C++ applications.
- `app/*.h` is the place to publish shared ABI headers that programs and DLLs consume.

Every directory directly under `applications/programs/` is linked into an intermediate ELF, then packed into a final `.exe` image. Every directory directly under `applications/dlls/` is linked as a PIC shared ELF and then packed into a flat relocatable `.dll` image. The build mounts `fat32.img`, copies program images into `/bin`, and copies shared libraries into `/lib` inside the FAT filesystem so they are available through VFS at boot.

See `applications/README.md` for the application-side layout, scaffolding, and SDK details.

## Build Output

All active build artifacts now land under `output/`:

- `output/kernel8.elf`
- `output/kernel8.img`
- `output/applications/programs/*.exe`
- `output/applications/dll/*.dll`

The compatibility Make targets `make user` and `make user-vfs` still work, but the preferred names are `make applications` and `make applications-vfs`.

## Scaffolding

Copy-ready templates live under `applications/_templates/`.

- `make new-app NAME=my_app [APP_LANG=c|cpp]`
- `make new-dll NAME=my_dll`
- `make new-app-pair APP_NAME=my_app DLL_NAME=my_dll`

The current default first application is `/bin/init.exe`.
