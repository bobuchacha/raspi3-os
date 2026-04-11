ARMGNU ?= /Applications/ArmGNUToolchain/12.3.Rel1/aarch64-none-elf/bin/aarch64-none-elf
QEMU ?= qemu-system-aarch64
PYTHON ?= python3

BOARD ?= virt
CPU ?= cortex-a53
ARCH ?= aarch64
QEMU_MACHINE ?= $(BOARD)
QEMU_CPUS ?= 1
QEMU_USB ?= 1
QEMU_GUI ?= 0
QEMU_SERIAL_ARGS ?= -serial stdio -monitor none

comma := ,

KERNEL_DIR := kernel
BOOTLOADER_DIR := $(KERNEL_DIR)/bootloader
KERNEL_INCLUDE_DIR := $(KERNEL_DIR)/include
BOOTLOADER_INCLUDE_DIR := $(BOOTLOADER_DIR)/include

OUTPUT_ROOT := output/boards/$(BOARD)
OBJ_DIR := $(OUTPUT_ROOT)/objs
KERNEL_ELF := $(OUTPUT_ROOT)/kernel8.elf
KERNEL_IMG := $(OUTPUT_ROOT)/kernel8.img
MAIN_KERNEL_ELF := $(OUTPUT_ROOT)/kernel.elf
MAIN_KERNEL_IMAGE := $(OUTPUT_ROOT)/kernel

MAIN_KERNEL_NAME := kernel
FAT32_IMAGE := fat32.img
FAT32_SIZE_MB ?= 64
FAT32_MOUNT_DIR := $(OUTPUT_ROOT)/fat32mnt
FAT32_MOUNT_DIR_ABS := $(abspath $(FAT32_MOUNT_DIR))
FAT32_ATTACH := hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount -readwrite $(FAT32_IMAGE)
APP_ROOT := applications
APP_INCLUDE_DIR := $(APP_ROOT)/include
APP_RUNTIME_DIR := $(APP_ROOT)/runtime
APP_ASSET_DIR := $(APP_ROOT)/assets
APP_FONT_ASSET_DIR := $(APP_ASSET_DIR)/fonts
APP_CURSOR_ASSET_DIR := $(APP_ASSET_DIR)/cursors
APP_WALLPAPER_ASSET_DIR := $(APP_ASSET_DIR)/wallpapers
APP_CORE_DIR := $(APP_ROOT)/apps/core
APP_DLLSMOKE_DIR := $(APP_ROOT)/apps/dllsmoke
APP_GUESAMPLE_DIR := $(APP_ROOT)/apps/guisample
APP_GWES_DIR := $(APP_ROOT)/apps/gwes
APP_GDI_DIR := $(APP_ROOT)/libs/gdi
APP_KEVENTMON_DIR := $(APP_ROOT)/apps/keventmon
APP_MULTIWIN_DIR := $(APP_ROOT)/apps/multiwin
APP_SHELL_DIR := $(APP_ROOT)/apps/shell
APP_SYSTEMTEST_DIR := $(APP_ROOT)/apps/systemtest
APP_WIDGETDEMO_DIR := $(APP_ROOT)/apps/widgetdemo
APP_SAMPLEMATH_DIR := $(APP_ROOT)/libs/samplemath
APP_WIDGETS_DIR := $(APP_ROOT)/libs/widgets
APP_WINDOWKIT_SOURCE := $(APP_ROOT)/libs/window/window.c
APP_WIDGETS_SOURCE := $(APP_WIDGETS_DIR)/widgets.c
APP_RUNTIME_WINDOW_SOURCE := $(APP_RUNTIME_DIR)/window_support.c
APP_RUNTIME_GDI_SOURCE := $(APP_RUNTIME_DIR)/gdi_support.c
APP_RUNTIME_GDI_FONT_SOURCE := $(APP_RUNTIME_DIR)/gdi_font_support.c
APP_RUNTIME_WIDGET_SOURCE := $(APP_RUNTIME_DIR)/widget_support.c
APP_SYSTEM_UI_FONT_ASSET := $(APP_FONT_ASSET_DIR)/system_ui.font
USER_OUTPUT_ROOT := $(OUTPUT_ROOT)/userspace
USER_CORE_ARTIFACT := $(USER_OUTPUT_ROOT)/core.exe
USER_DLLSMOKE_ARTIFACT := $(USER_OUTPUT_ROOT)/dllsmoke.exe
USER_GUESAMPLE_ARTIFACT := $(USER_OUTPUT_ROOT)/guisample.exe
USER_GWES_ARTIFACT := $(USER_OUTPUT_ROOT)/gwes.exe
USER_GDI_ARTIFACT := $(USER_OUTPUT_ROOT)/gdi.dll
USER_KEVENTMON_ARTIFACT := $(USER_OUTPUT_ROOT)/kevent.exe
USER_MULTIWIN_ARTIFACT := $(USER_OUTPUT_ROOT)/multiwin.exe
USER_SAMPLEMATH_ARTIFACT := $(USER_OUTPUT_ROOT)/samplemath.dll
USER_SHELL_ARTIFACT := $(USER_OUTPUT_ROOT)/shell.exe
USER_SYSTEMTEST_ARTIFACT := $(USER_OUTPUT_ROOT)/systemtest.exe
USER_WIDGETDEMO_ARTIFACT := $(USER_OUTPUT_ROOT)/widgetdemo.exe
USER_WIDGETS_ARTIFACT := $(USER_OUTPUT_ROOT)/widgets.dll
USER_WINDOWKIT_ARTIFACT := $(USER_OUTPUT_ROOT)/window.dll
USER_BUILDER := $(PYTHON) tools/my-loader/ldr_build.py
USER_BUILDER_COMMON_FLAGS := --project-root . --include-dir $(KERNEL_INCLUDE_DIR) --include-dir $(APP_INCLUDE_DIR) --define LDR_MVP=1 --cflag=-O2 --cflag=-g --cflag=-ffunction-sections --cflag=-fdata-sections --ldflag=--gc-sections --output-dir $(USER_OUTPUT_ROOT)
USER_GWES_BUILDER_FLAGS := $(USER_BUILDER_COMMON_FLAGS) --include-dir $(APP_GWES_DIR) --include-dir $(APP_GWES_DIR)/include
USER_RUNTIME_SOURCES := $(sort \
	$(wildcard $(APP_RUNTIME_DIR)/*.c) \
	$(wildcard $(APP_RUNTIME_DIR)/*.cpp) \
	$(wildcard $(APP_RUNTIME_DIR)/*.cc) \
	$(wildcard $(APP_RUNTIME_DIR)/*.cxx))
USER_RUNTIME_COMMON_SOURCES := $(filter-out $(APP_RUNTIME_WINDOW_SOURCE) $(APP_RUNTIME_GDI_SOURCE) $(APP_RUNTIME_GDI_FONT_SOURCE) $(APP_RUNTIME_WIDGET_SOURCE),$(USER_RUNTIME_SOURCES))
USER_CORE_SOURCES := $(sort \
	$(wildcard $(APP_CORE_DIR)/*.c) \
	$(wildcard $(APP_CORE_DIR)/*.cpp) \
	$(wildcard $(APP_CORE_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_DLLSMOKE_SOURCES := $(sort \
	$(wildcard $(APP_DLLSMOKE_DIR)/*.c) \
	$(wildcard $(APP_DLLSMOKE_DIR)/*.cpp) \
	$(wildcard $(APP_DLLSMOKE_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_GUESAMPLE_SOURCES := $(sort \
	$(wildcard $(APP_GUESAMPLE_DIR)/*.c) \
	$(wildcard $(APP_GUESAMPLE_DIR)/*.cpp) \
	$(wildcard $(APP_GUESAMPLE_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_GWES_SOURCES := $(sort \
	$(wildcard $(APP_GWES_DIR)/*.c) \
	$(wildcard $(APP_GWES_DIR)/*.cpp) \
	$(wildcard $(APP_GWES_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_GDI_SOURCES := $(APP_GDI_DIR)/gdi.c
USER_KEVENTMON_SOURCES := $(sort \
	$(wildcard $(APP_KEVENTMON_DIR)/*.c) \
	$(wildcard $(APP_KEVENTMON_DIR)/*.cpp) \
	$(wildcard $(APP_KEVENTMON_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_MULTIWIN_SOURCES := $(sort \
	$(wildcard $(APP_MULTIWIN_DIR)/*.c) \
	$(wildcard $(APP_MULTIWIN_DIR)/*.cpp) \
	$(wildcard $(APP_MULTIWIN_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_SAMPLEMATH_SOURCES := $(sort \
	$(wildcard $(APP_SAMPLEMATH_DIR)/*.c) \
	$(wildcard $(APP_SAMPLEMATH_DIR)/*.cpp) \
	$(wildcard $(APP_SAMPLEMATH_DIR)/*.S))
USER_WINDOWKIT_SOURCES := $(APP_WINDOWKIT_SOURCE)
USER_SHELL_SOURCES := $(sort \
	$(wildcard $(APP_SHELL_DIR)/*.c) \
	$(wildcard $(APP_SHELL_DIR)/*.cpp) \
	$(wildcard $(APP_SHELL_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_SYSTEMTEST_SOURCES := $(sort \
	$(wildcard $(APP_SYSTEMTEST_DIR)/*.c) \
	$(wildcard $(APP_SYSTEMTEST_DIR)/*.cpp) \
	$(wildcard $(APP_SYSTEMTEST_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_WIDGETDEMO_SOURCES := $(sort \
	$(wildcard $(APP_WIDGETDEMO_DIR)/*.c) \
	$(wildcard $(APP_WIDGETDEMO_DIR)/*.cpp) \
	$(wildcard $(APP_WIDGETDEMO_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_WIDGETS_SOURCES := $(APP_WIDGETS_SOURCE)
USER_APP_ARTIFACTS := $(USER_CORE_ARTIFACT) $(USER_DLLSMOKE_ARTIFACT) $(USER_GUESAMPLE_ARTIFACT) $(USER_GWES_ARTIFACT) $(USER_KEVENTMON_ARTIFACT) $(USER_MULTIWIN_ARTIFACT) $(USER_SHELL_ARTIFACT) $(USER_SYSTEMTEST_ARTIFACT)
USER_APP_ARTIFACTS += $(USER_WIDGETDEMO_ARTIFACT)
USER_DLL_ARTIFACTS := $(USER_SAMPLEMATH_ARTIFACT) $(USER_WINDOWKIT_ARTIFACT) $(USER_GDI_ARTIFACT) $(USER_WIDGETS_ARTIFACT)
USER_ARTIFACTS := $(USER_APP_ARTIFACTS) $(USER_DLL_ARTIFACTS)

MAIN_KERNEL_PACKER := $(PYTHON) tools/my-loader/ldr_build.py --pack-kernel
MAIN_KERNEL_LINKER_SCRIPT := $(if $(filter virt,$(BOARD)),$(KERNEL_DIR)/platform/board/virt/link-virt.ld,$(KERNEL_DIR)/platform/board/raspi3/link.ld)
BOOTLOADER_LINKER_SCRIPT := $(if $(filter virt,$(BOARD)),$(BOOTLOADER_DIR)/linker/link-virt.ld,$(BOOTLOADER_DIR)/linker/link.ld)
BOARD_CPPFLAGS := $(if $(filter virt,$(BOARD)),-DBOARD_VIRT=1,-DBOARD_RASPI3=1)
ARCH_CPPFLAGS := $(if $(filter aarch64,$(ARCH)),-DARCH_AARCH64=1,)
MAIN_KERNEL_LOAD_PHYS_BASE := $(if $(filter virt,$(BOARD)),0x40100000ULL,0x00100000ULL)
BOOT_TARGET_LOAD_PHYS_BASE := $(MAIN_KERNEL_LOAD_PHYS_BASE)

COMMON_CPPFLAGS := $(BOARD_CPPFLAGS) $(ARCH_CPPFLAGS) -DBOOT_TARGET_BOARD_NAME=\"$(BOARD)\" -DBOOT_TARGET_KERNEL_PATH=\"/$(MAIN_KERNEL_NAME)\" -DBOOT_TARGET_LOAD_PHYS_BASE=$(BOOT_TARGET_LOAD_PHYS_BASE)
COMMON_CFLAGS := -g -Werror -nostdlib -nostartfiles -ffreestanding -fno-omit-frame-pointer -mgeneral-regs-only
COMMON_CXXFLAGS := -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -fno-unwind-tables -fno-asynchronous-unwind-tables
KERNEL_DEBUG_ENABLE ?= 1
KERNEL_DEBUG_ZONE_MASK ?= 0xFFFF
KERNEL_DEBUG_CPPFLAGS := -DKERNEL_DEBUG_ENABLE=$(KERNEL_DEBUG_ENABLE) -DKERNEL_DEBUG_ZONE_MASK=$(KERNEL_DEBUG_ZONE_MASK)
KERNEL_CFLAGS := $(COMMON_CFLAGS) $(BOARD_CPPFLAGS) $(ARCH_CPPFLAGS) $(KERNEL_DEBUG_CPPFLAGS) -I$(KERNEL_INCLUDE_DIR) -I$(KERNEL_DIR) -I$(APP_INCLUDE_DIR)
KERNEL_CXXFLAGS := $(KERNEL_CFLAGS) $(COMMON_CXXFLAGS)
KERNEL_ASFLAGS := -g $(BOARD_CPPFLAGS) $(ARCH_CPPFLAGS) -I$(KERNEL_INCLUDE_DIR) -I$(KERNEL_DIR) -I$(APP_INCLUDE_DIR)
BOOTLOADER_CFLAGS := $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -I$(BOOTLOADER_INCLUDE_DIR) -I$(BOOTLOADER_DIR) -I$(KERNEL_INCLUDE_DIR) -I$(KERNEL_DIR)
BOOTLOADER_CXXFLAGS := $(BOOTLOADER_CFLAGS) $(COMMON_CXXFLAGS)
BOOTLOADER_ASFLAGS := -g $(COMMON_CPPFLAGS) -I$(BOOTLOADER_INCLUDE_DIR) -I$(BOOTLOADER_DIR) -I$(KERNEL_INCLUDE_DIR) -I$(KERNEL_DIR)

KERNEL_COMMON_SOURCE_FILES := $(shell find $(KERNEL_DIR) \
	-path $(BOOTLOADER_DIR) -prune -o \
	-path $(KERNEL_DIR)/arch -prune -o \
	-path $(KERNEL_DIR)/mm/arch -prune -o \
	-type d \( -name '_*' -o -name '__pycache__' \) -prune -o \
	-type f \( -name '*.c' -o -name '*.cpp' -o -name '*.S' \) -print | LC_ALL=C sort)
KERNEL_ARCH_SOURCE_FILES := $(shell if [ -d $(KERNEL_DIR)/arch/$(ARCH) ]; then find $(KERNEL_DIR)/arch/$(ARCH) \
	-type d \( -name '_*' -o -name '__pycache__' \) -prune -o \
	-type f \( -name '*.c' -o -name '*.cpp' -o -name '*.S' \) -print; fi | LC_ALL=C sort)
KERNEL_MM_ARCH_SOURCE_FILES := $(shell if [ -d $(KERNEL_DIR)/mm/arch/$(ARCH) ]; then find $(KERNEL_DIR)/mm/arch/$(ARCH) \
	-type d \( -name '_*' -o -name '__pycache__' \) -prune -o \
	-type f \( -name '*.c' -o -name '*.cpp' -o -name '*.S' \) -print; fi | LC_ALL=C sort)
KERNEL_SOURCE_FILES := $(sort $(KERNEL_COMMON_SOURCE_FILES) $(KERNEL_ARCH_SOURCE_FILES) $(KERNEL_MM_ARCH_SOURCE_FILES))
BOOTLOADER_COMMON_SOURCE_FILES := $(shell find $(BOOTLOADER_DIR) \
	-path $(BOOTLOADER_DIR)/arch -prune -o \
	-type d \( -name '_*' -o -name '__pycache__' \) -prune -o \
	-type f \( -name '*.c' -o -name '*.cpp' -o -name '*.S' \) -print | LC_ALL=C sort)
BOOTLOADER_ARCH_SOURCE_FILES := $(shell if [ -d $(BOOTLOADER_DIR)/arch/$(ARCH) ]; then find $(BOOTLOADER_DIR)/arch/$(ARCH) \
	-type d \( -name '_*' -o -name '__pycache__' \) -prune -o \
	-type f \( -name '*.c' -o -name '*.cpp' -o -name '*.S' \) -print; fi | LC_ALL=C sort)
BOOTLOADER_SOURCE_FILES := $(sort $(BOOTLOADER_COMMON_SOURCE_FILES) $(BOOTLOADER_ARCH_SOURCE_FILES))

KERNEL_C_FILES := $(filter %.c,$(KERNEL_SOURCE_FILES))
KERNEL_CPP_FILES := $(filter %.cpp,$(KERNEL_SOURCE_FILES))
KERNEL_ASM_FILES := $(filter %.S,$(KERNEL_SOURCE_FILES))
BOOTLOADER_C_FILES := $(filter %.c,$(BOOTLOADER_SOURCE_FILES))
BOOTLOADER_CPP_FILES := $(filter %.cpp,$(BOOTLOADER_SOURCE_FILES))
BOOTLOADER_ASM_FILES := $(filter %.S,$(BOOTLOADER_SOURCE_FILES))

KERNEL_OBJ_FILES := $(patsubst $(KERNEL_DIR)/%.c,$(OBJ_DIR)/kernel/%_c.o,$(KERNEL_C_FILES))
KERNEL_OBJ_FILES += $(patsubst $(KERNEL_DIR)/%.cpp,$(OBJ_DIR)/kernel/%_cpp.o,$(KERNEL_CPP_FILES))
KERNEL_OBJ_FILES += $(patsubst $(KERNEL_DIR)/%.S,$(OBJ_DIR)/kernel/%_s.o,$(KERNEL_ASM_FILES))

BOOTLOADER_OBJ_FILES := $(patsubst $(BOOTLOADER_DIR)/%.c,$(OBJ_DIR)/bootloader/%_c.o,$(BOOTLOADER_C_FILES))
BOOTLOADER_OBJ_FILES += $(patsubst $(BOOTLOADER_DIR)/%.cpp,$(OBJ_DIR)/bootloader/%_cpp.o,$(BOOTLOADER_CPP_FILES))
BOOTLOADER_OBJ_FILES += $(patsubst $(BOOTLOADER_DIR)/%.S,$(OBJ_DIR)/bootloader/%_s.o,$(BOOTLOADER_ASM_FILES))

DEP_FILES := $(KERNEL_OBJ_FILES:.o=.d) $(BOOTLOADER_OBJ_FILES:.o=.d)
COMMON_RUNTIME_OBJ_FILES := $(OBJ_DIR)/kernel/lib/memory_c.o

QEMU_DISPLAY_ARGS := $(if $(filter 1,$(QEMU_GUI)),-display default,-display none)
ifneq ($(filter virt,$(QEMU_MACHINE)),)
QEMU_USB_ARGS := $(if $(filter 1,$(QEMU_USB)),-device virtio-keyboard-device$(comma)bus=virtio-mmio-bus.1$(comma)display=ramfb0 -device virtio-tablet-device$(comma)bus=virtio-mmio-bus.2$(comma)display=ramfb0,)
else
QEMU_USB_ARGS := $(if $(filter 1,$(QEMU_USB)),-device usb-kbd -device usb-mouse -device usb-tablet,)
endif
QEMU_VIRT_MACHINE := virt,gic-version=2,virtualization=on
QEMU_MACHINE_ARG := $(if $(filter virt,$(QEMU_MACHINE)),$(QEMU_VIRT_MACHINE),$(QEMU_MACHINE))
QEMU_CPU_ARGS := $(if $(filter virt,$(QEMU_MACHINE)),-cpu $(CPU),)
QEMU_BOOT_ARGS := -kernel $(KERNEL_IMG)
QEMU_PLATFORM_ARGS := $(if $(filter virt,$(QEMU_MACHINE)),-global virtio-mmio.force-legacy=false -device ramfb$(comma)id=ramfb0 -m 256M,)
QEMU_STORAGE_ARGS := $(if $(filter virt,$(QEMU_MACHINE)),-drive file=$(FAT32_IMAGE)$(comma)if=none$(comma)format=raw$(comma)id=virtblk0 -device virtio-blk-device$(comma)drive=virtblk0$(comma)bus=virtio-mmio-bus.0,-drive file=$(FAT32_IMAGE),if=sd,format=raw)

.DEFAULT_GOAL := all

.PHONY: all clean applications applications-vfs fat32-sync kernel8.img run run-gfx run-headless run-virt run-virt-gfx run-virt-headless dump

all: kernel8.img

applications: $(USER_ARTIFACTS)

$(USER_CORE_ARTIFACT): tools/my-loader/ldr_build.py $(USER_CORE_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name core --kind exe $(foreach src,$(USER_CORE_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_DLLSMOKE_ARTIFACT): tools/my-loader/ldr_build.py $(USER_DLLSMOKE_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name dllsmoke --kind exe $(foreach src,$(USER_DLLSMOKE_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_GUESAMPLE_ARTIFACT): tools/my-loader/ldr_build.py $(USER_GUESAMPLE_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name guisample --kind exe $(foreach src,$(USER_GUESAMPLE_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_GWES_ARTIFACT): tools/my-loader/ldr_build.py $(USER_GWES_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name gwes --kind exe $(foreach src,$(USER_GWES_SOURCES),--source $(src)) --entry-symbol _start $(USER_GWES_BUILDER_FLAGS)

$(USER_KEVENTMON_ARTIFACT): tools/my-loader/ldr_build.py $(USER_KEVENTMON_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name kevent --kind exe $(foreach src,$(USER_KEVENTMON_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_MULTIWIN_ARTIFACT): tools/my-loader/ldr_build.py $(USER_MULTIWIN_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name multiwin --kind exe $(foreach src,$(USER_MULTIWIN_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_SAMPLEMATH_ARTIFACT): tools/my-loader/ldr_build.py $(USER_SAMPLEMATH_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name samplemath --kind dll $(foreach src,$(USER_SAMPLEMATH_SOURCES),--source $(src)) --entry-symbol samplemath_entry $(USER_BUILDER_COMMON_FLAGS)

$(USER_WINDOWKIT_ARTIFACT): tools/my-loader/ldr_build.py $(USER_WINDOWKIT_SOURCES) $(APP_RUNTIME_WINDOW_SOURCE)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name window --kind dll $(foreach src,$(USER_WINDOWKIT_SOURCES),--source $(src)) --entry-symbol window_entry $(USER_BUILDER_COMMON_FLAGS)

$(USER_GDI_ARTIFACT): tools/my-loader/ldr_build.py $(USER_GDI_SOURCES) $(APP_RUNTIME_GDI_SOURCE) $(APP_RUNTIME_GDI_FONT_SOURCE)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name gdi --kind dll $(foreach src,$(USER_GDI_SOURCES),--source $(src)) --entry-symbol gdi_entry $(USER_BUILDER_COMMON_FLAGS)

$(USER_SHELL_ARTIFACT): tools/my-loader/ldr_build.py $(USER_SHELL_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name shell --kind exe $(foreach src,$(USER_SHELL_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_SYSTEMTEST_ARTIFACT): tools/my-loader/ldr_build.py $(USER_SYSTEMTEST_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name systemtest --kind exe $(foreach src,$(USER_SYSTEMTEST_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_WIDGETDEMO_ARTIFACT): tools/my-loader/ldr_build.py $(USER_WIDGETDEMO_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name widgetdemo --kind exe $(foreach src,$(USER_WIDGETDEMO_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_WIDGETS_ARTIFACT): tools/my-loader/ldr_build.py $(USER_WIDGETS_SOURCES) $(APP_RUNTIME_WIDGET_SOURCE)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name widgets --kind dll $(foreach src,$(USER_WIDGETS_SOURCES),--source $(src)) --entry-symbol widgets_entry $(USER_BUILDER_COMMON_FLAGS)

applications-vfs: fat32-sync

clean:
	@rm -rf output
	@rm -f $(FAT32_IMAGE)

$(OBJ_DIR)/kernel/%_c.o: $(KERNEL_DIR)/%.c
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(KERNEL_CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/kernel/%_cpp.o: $(KERNEL_DIR)/%.cpp
	@mkdir -p $(@D)
	@$(ARMGNU)-g++ $(KERNEL_CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/kernel/%_s.o: $(KERNEL_DIR)/%.S
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(KERNEL_ASFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/bootloader/%_c.o: $(BOOTLOADER_DIR)/%.c
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(BOOTLOADER_CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/bootloader/%_cpp.o: $(BOOTLOADER_DIR)/%.cpp
	@mkdir -p $(@D)
	@$(ARMGNU)-g++ $(BOOTLOADER_CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/bootloader/%_s.o: $(BOOTLOADER_DIR)/%.S
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(BOOTLOADER_ASFLAGS) -MMD -MP -c $< -o $@

$(MAIN_KERNEL_ELF): $(MAIN_KERNEL_LINKER_SCRIPT) $(KERNEL_OBJ_FILES)
	@mkdir -p $(OUTPUT_ROOT)
	@$(ARMGNU)-ld -nostdlib -z max-page-size=0x1000 -T $(MAIN_KERNEL_LINKER_SCRIPT) -o $@ $(KERNEL_OBJ_FILES) -g

$(MAIN_KERNEL_IMAGE): $(MAIN_KERNEL_ELF) tools/my-loader/ldr_build.py
	@mkdir -p $(OUTPUT_ROOT)
	@$(MAIN_KERNEL_PACKER) --input $< --output $@

$(KERNEL_ELF): $(BOOTLOADER_LINKER_SCRIPT) $(BOOTLOADER_OBJ_FILES) $(COMMON_RUNTIME_OBJ_FILES)
	@mkdir -p $(OUTPUT_ROOT)
	@$(ARMGNU)-ld -nostdlib -z max-page-size=0x1000 -T $(BOOTLOADER_LINKER_SCRIPT) -o $@ $(BOOTLOADER_OBJ_FILES) $(COMMON_RUNTIME_OBJ_FILES) -g

$(KERNEL_IMG): $(KERNEL_ELF)
	@$(ARMGNU)-objcopy $< -O binary $@

$(FAT32_IMAGE):
	@rm -f $@
	@dd if=/dev/zero of=$@ bs=1048576 count=$(FAT32_SIZE_MB) >/dev/null 2>&1

fat32-sync: $(FAT32_IMAGE) $(MAIN_KERNEL_IMAGE) $(USER_ARTIFACTS)
# 	@pkill qemu
	@set -e; \
	if command -v lsof >/dev/null 2>&1; then \
		STALE_HOLDERS=$$(lsof $(FAT32_IMAGE) 2>/dev/null | awk 'NR > 1 { print $$1 " pid=" $$2 }' || true); \
		if [ -n "$$STALE_HOLDERS" ]; then \
			echo "$(FAT32_IMAGE) is still in use; stop the stale emulator/process before retrying" >&2; \
			echo "Killing $$STALE_HOLDERS" >&2; \
			echo "$$STALE_HOLDERS" | awk '{ sub(/^pid=/, "", $$2); print $$2 }' | xargs kill -9 2>/dev/null || true; \
			REMAINING_HOLDERS=$$(lsof $(FAT32_IMAGE) 2>/dev/null | awk 'NR > 1 { print $$1 " pid=" $$2 }' || true); \
			if [ -n "$$REMAINING_HOLDERS" ]; then \
				echo "Failed to kill some processes holding $(FAT32_IMAGE); please check the output above and stop them before retrying" >&2; \
				echo "Stale holders:" >&2; \
				echo "$$REMAINING_HOLDERS" >&2; \
				exit 1; \
			fi; \
		fi; \
	fi; \
	if mount | grep -q " on $(FAT32_MOUNT_DIR_ABS) "; then umount -f $(FAT32_MOUNT_DIR_ABS) >/dev/null 2>&1 || true; fi; \
	STALE_DEVS=$$(hdiutil info | awk -v target="$$PWD/$(FAT32_IMAGE)" '/image-path/ && $$NF == target { found=1; next } found && /^\/dev\// { print $$1; found=0 }'); \
	for STALE_DEV in $$STALE_DEVS; do hdiutil detach -force "$$STALE_DEV" >/dev/null 2>&1 || true; done; \
	mkdir -p $(FAT32_MOUNT_DIR_ABS); \
	DEV=$$($(FAT32_ATTACH) | awk 'NR==1 { print $$1 }'); \
	if [ -z "$$DEV" ]; then echo "Failed to attach $(FAT32_IMAGE)"; exit 1; fi; \
	trap 'umount -f $(FAT32_MOUNT_DIR_ABS) >/dev/null 2>&1 || true; hdiutil detach -force "'"'$$DEV'"'" >/dev/null 2>&1 || true' EXIT; \
	newfs_msdos -F 32 -O ROS -S 512 -c 1 -n 2 -v ROSROOT "$$DEV" >/dev/null; \
	mount -t msdos "$$DEV" $(FAT32_MOUNT_DIR_ABS); \
	cp "$(MAIN_KERNEL_IMAGE)" "$(FAT32_MOUNT_DIR_ABS)/$(MAIN_KERNEL_NAME)"; \
	mkdir -p "$(FAT32_MOUNT_DIR_ABS)/bin" "$(FAT32_MOUNT_DIR_ABS)/fonts" "$(FAT32_MOUNT_DIR_ABS)/cursors" "$(FAT32_MOUNT_DIR_ABS)/wallpapers" "$(FAT32_MOUNT_DIR_ABS)/lib"; \
	cp "$(USER_CORE_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/core.exe"; \
	cp "$(USER_DLLSMOKE_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/dllsmoke.exe"; \
	cp "$(USER_GUESAMPLE_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/guisample.exe"; \
	cp "$(USER_GWES_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/gwes.exe"; \
	cp "$(USER_KEVENTMON_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/kevent.exe"; \
	cp "$(USER_MULTIWIN_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/multiwin.exe"; \
	cp "$(USER_SYSTEMTEST_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/systemtest.exe"; \
	cp "$(USER_WIDGETDEMO_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/widgetdemo.exe"; \
	cp "$(USER_SAMPLEMATH_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/lib/samplemath.dll"; \
	cp "$(USER_WINDOWKIT_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/lib/window.dll"; \
	cp "$(USER_GDI_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/lib/gdi.dll"; \
	cp "$(USER_WIDGETS_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/lib/widgets.dll"; \
	for font_asset in $(APP_FONT_ASSET_DIR)/system_ui.rtf $(APP_FONT_ASSET_DIR)/system_ui.font $(APP_FONT_ASSET_DIR)/tahoma-*.rtf; do if [ -f "$$font_asset" ]; then cp "$$font_asset" "$(FAT32_MOUNT_DIR_ABS)/fonts/"; fi; done; \
	for cursor_asset in $(APP_CURSOR_ASSET_DIR)/*.cur32; do if [ -f "$$cursor_asset" ]; then cp "$$cursor_asset" "$(FAT32_MOUNT_DIR_ABS)/cursors/"; fi; done; \
	for wallpaper_asset in $(APP_WALLPAPER_ASSET_DIR)/*.jpg; do if [ -f "$$wallpaper_asset" ]; then cp "$$wallpaper_asset" "$(FAT32_MOUNT_DIR_ABS)/wallpapers/"; fi; done; \
	cp "$(USER_SHELL_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/shell.exe"; \
	printf 'lfn sample\n' > "$(FAT32_MOUNT_DIR_ABS)/Long File Name.txt"; \
	printf 'initial writable sample\n' > "$(FAT32_MOUNT_DIR_ABS)/Writable Sample.txt"; \
	umount -f $(FAT32_MOUNT_DIR_ABS); \
	hdiutil detach -force "$$DEV" >/dev/null; \
	trap - EXIT

kernel8.img: fat32-sync $(KERNEL_IMG)
	@true

run: kernel8.img
	@$(QEMU) -M $(QEMU_MACHINE_ARG) $(QEMU_CPU_ARGS) -smp $(QEMU_CPUS) $(QEMU_BOOT_ARGS) $(QEMU_SERIAL_ARGS) $(QEMU_DISPLAY_ARGS) $(QEMU_USB_ARGS) $(QEMU_PLATFORM_ARGS) $(QEMU_STORAGE_ARGS)

run-gfx: QEMU_GUI=1
run-gfx: kernel8.img
	@$(QEMU) -M $(QEMU_MACHINE_ARG) $(QEMU_CPU_ARGS) -smp $(QEMU_CPUS) $(QEMU_BOOT_ARGS) $(QEMU_SERIAL_ARGS) -display default $(QEMU_USB_ARGS) $(QEMU_PLATFORM_ARGS) $(QEMU_STORAGE_ARGS)

run-headless: kernel8.img
	@$(QEMU) -M $(QEMU_MACHINE_ARG) $(QEMU_CPU_ARGS) -smp $(QEMU_CPUS) $(QEMU_BOOT_ARGS) $(QEMU_SERIAL_ARGS) -display none $(QEMU_USB_ARGS) $(QEMU_PLATFORM_ARGS) $(QEMU_STORAGE_ARGS)

run-virt:
	@$(MAKE) BOARD=virt CPU=$(CPU) QEMU_MACHINE=virt QEMU_CPUS=1 run

run-virt-gfx:
	@$(MAKE) BOARD=virt CPU=$(CPU) QEMU_MACHINE=virt QEMU_CPUS=1 run-gfx

run-virt-headless:
	@$(MAKE) BOARD=virt CPU=$(CPU) QEMU_MACHINE=virt QEMU_CPUS=1 run-headless

dump: $(KERNEL_ELF) $(MAIN_KERNEL_ELF)
	@$(ARMGNU)-objdump --all-headers $(KERNEL_ELF)
	@$(ARMGNU)-objdump --all-headers $(MAIN_KERNEL_ELF)
