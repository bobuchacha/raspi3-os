# Default macOS/Linux toolchain path (override in environment as needed)
ARMGNU ?= /Applications/ArmGNUToolchain/12.3.Rel1/aarch64-none-elf/bin/aarch64-none-elf
# ARMGNU ?= /Applications/ArmGNUToolchain/13.2.Rel1/aarch64-none-elf/bin/aarch64-none-elf
# ARMGNU ?= E:\Nextcloud\raspo3b-os/toolchain/Windows/arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-aarch64-none-elf/bin/aarch64-none-elf
#ARMGNU ?= C:\Users\bobuc\Nextcloud\raspo3b-os/toolchain/Windows/arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-aarch64-none-elf/bin/aarch64-none-elf
#ARMGNU ?= aarch64-none-elf
# QEMU default. On Windows, prefer explicit .exe when running native cmd shells.
QEMU ?= qemu-system-aarch64

# Detect Windows more robustly (OS, uname, MSYS/MINGW/CYGWIN, or WINDIR)
UNAME_S := $(shell uname -s 2>/dev/null || true)
IS_WINDOWS := 0
ifeq ($(OS),Windows_NT)
	IS_WINDOWS := 1
endif
ifneq ($(findstring MINGW,$(UNAME_S)),)
	IS_WINDOWS := 1
endif
ifneq ($(findstring MSYS,$(UNAME_S)),)
	IS_WINDOWS := 1
endif
ifneq ($(findstring CYGWIN,$(UNAME_S)),)
	IS_WINDOWS := 1
endif
ifneq ($(WINDIR),)
	IS_WINDOWS := 1
endif
$(info Detected OS: $(OS) uname: $(UNAME_S) IS_WINDOWS=$(IS_WINDOWS))
ifeq ($(IS_WINDOWS),1)
	# If using native Windows toolchain path, you can set ARMGNU externally,
	# otherwise provide a common example (adjust to your installation):
	ARMGNU ?= E:/Nextcloud/raspo3b-os/toolchain/Windows/arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-aarch64-none-elf/bin/aarch64-none-elf
	# Prefer bash-style shell if available (MSYS2/MinGW). If not present, many
	# Make targets require a POSIX shell—consider using WSL or MSYS2 on Windows.
	SHELL := bash
	# Use forward slashes in Windows QEMU path to avoid backslash escaping
	QEMU := d:/qemu/qemu-system-aarch64.exe
endif

QEMU_CPUS ?= 4
QEMU_USB ?= 1
QEMU_GUI ?= 0
comma := ,

ARCH ?= aarch64
CPU ?= cortex-a53
BOARD ?= raspi3b
QEMU_MACHINE ?= raspi3b
QEMU_DISPLAY_ARGS = $(if $(filter 1,$(QEMU_GUI)),-display default,$(if $(filter 1,$(QEMU_USB)),-display default,-display none))

ifneq ($(filter virt,$(QEMU_MACHINE)),)
QEMU_USB_ARGS = $(if $(filter 1,$(QEMU_USB)),-device qemu-xhci$(comma)id=xhci -device usb-kbd -device usb-mouse -device usb-tablet,)
else
QEMU_USB_ARGS = $(if $(filter 1,$(QEMU_USB)),-device usb-kbd -device usb-mouse -device usb-tablet,)
endif

PLATFORM_CPPFLAGS = -DROS_BOARD_HEADER=\"platform/board/$(BOARD).h\"
PLATFORM_CPPFLAGS += -DROS_CPU_MMU_HEADER=\"platform/cpu/$(CPU)/mmu.h\"
PLATFORM_CPPFLAGS += -DROS_DEBUG_UART_HEADER=\"platform/cpu/$(CPU)/debug_uart.h\"
PLATFORM_CPPFLAGS += -DROS_QEMU_USB_ENABLED=$(QEMU_USB)

BUILD_DIR = output
SRC_DIR = kernel
LOADER_DIR = kernel-loader
APP_ROOT = applications
APP_INCLUDE_DIR = $(APP_ROOT)/include
APP_APPS_DIR = $(APP_ROOT)/apps
APP_LIBS_DIR = $(APP_ROOT)/libs
APP_DRIVERS_DIR = $(APP_ROOT)/drivers
INCLUDE_DIR = $(SRC_DIR)/include
OBJS_DIR = $(BUILD_DIR)/objs
KERNEL_ELF = $(BUILD_DIR)/kernel8.elf
KERNEL_IMG = $(BUILD_DIR)/kernel8.img
MAIN_KERNEL_ELF = $(BUILD_DIR)/roskrnl.elf
MAIN_KERNEL_IMAGE = $(BUILD_DIR)/roskrnl
MAIN_KERNEL_LINKER_SCRIPT = $(SRC_DIR)/link.ld
LOADER_LINKER_SCRIPT = $(LOADER_DIR)/link.ld
ROSKRNL_NAME = roskrnl

COPS = -g -Werror -nostdlib -nostartfiles -ffreestanding -fno-omit-frame-pointer -I. -I$(INCLUDE_DIR) -I$(SRC_DIR) -mgeneral-regs-only $(PLATFORM_CPPFLAGS)
ASMOPS = -g -I. -I$(INCLUDE_DIR) $(PLATFORM_CPPFLAGS)
LOADER_COPS = -g -Werror -nostdlib -nostartfiles -ffreestanding -fno-omit-frame-pointer -I. -I$(LOADER_DIR)/include -I$(LOADER_DIR) -mgeneral-regs-only $(PLATFORM_CPPFLAGS)
LOADER_ASMOPS = -g -I. -I$(LOADER_DIR)/include -I$(LOADER_DIR) $(PLATFORM_CPPFLAGS)
USER_COPS = -g -Werror -nostdlib -nostartfiles -ffreestanding -fno-omit-frame-pointer -I$(APP_INCLUDE_DIR) -mgeneral-regs-only
USER_CXXOPS = $(USER_COPS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
USER_ASMOPS = -g -I$(APP_INCLUDE_DIR)

all: applications-vfs $(KERNEL_IMG)

.PHONY: all clean applications applications-vfs user user-vfs new-app new-dll new-sys new-app-pair fat32-mount fat32-umount fat32-list fat32-read fat32-hexdump run run-gfx run-headless debug asm dump diasm gdb kernel8.img

clean:
	@echo "Cleaning build system"
	@rm -rf $(BUILD_DIR)
	@rm -f kernel8.img >/dev/null 2>/dev/null || true
	@mkdir -p $(OBJS_DIR)

$(OBJS_DIR)/%_c.o: $(SRC_DIR)/%.c
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(COPS) -MMD -c $< -o $@
$(OBJS_DIR)/%_cpp.o: $(SRC_DIR)/%.cpp
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(COPS) -MMD -c $< -o $@

$(OBJS_DIR)/%_s.o: $(SRC_DIR)/%.S
	@echo "-> $@..."
	@$(ARMGNU)-gcc $(ASMOPS) -MMD -c $< -o $@

$(OBJS_DIR)/kernel-loader/%_c.o: $(LOADER_DIR)/%.c
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(LOADER_COPS) -MMD -c $< -o $@
$(OBJS_DIR)/kernel-loader/%_cpp.o: $(LOADER_DIR)/%.cpp
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-g++ $(LOADER_COPS) -MMD -c $< -o $@
$(OBJS_DIR)/kernel-loader/%_s.o: $(LOADER_DIR)/%.S
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(LOADER_ASMOPS) -MMD -c $< -o $@

FONT_PSF_OBJ = $(OBJS_DIR)/font_psf.o
FONT_PSF_SRC = build/screenfont/font.psf
FONT_SFN_OBJ = $(OBJS_DIR)/font_sfn.o
FONT_SFN_SRC = build/screenfont/font.sfn

$(FONT_PSF_OBJ): $(FONT_PSF_SRC)
	@$(ARMGNU)-ld -r -b binary -o $(FONT_PSF_OBJ) $(FONT_PSF_SRC)

$(FONT_SFN_OBJ): $(FONT_SFN_SRC)
	@$(ARMGNU)-ld -r -b binary -o $(FONT_SFN_OBJ) $(FONT_SFN_SRC)

#C_FILES = $(wildcard $(SRC_DIR)/*.c)
SOURCE_FIND = find $(SRC_DIR) \
	-type d \( -name '_*' -o -name examples -o -name tests -o -name documents -o -name tools -o -name __pycache__ \) -prune \
	-o -type f
# Run the find once and filter in Make to avoid multiple shell spawns
SOURCE_FILES := $(shell $(SOURCE_FIND) -print)
C_FILES := $(filter %.c,$(SOURCE_FILES))
CPP_FILES := $(filter %.cpp,$(SOURCE_FILES))
ASM_FILES := $(filter %.S,$(SOURCE_FILES))
OBJ_FILES = $(C_FILES:$(SRC_DIR)/%.c=$(OBJS_DIR)/%_c.o)
OBJ_FILES += $(CPP_FILES:$(SRC_DIR)/%.cpp=$(OBJS_DIR)/%_cpp.o)
OBJ_FILES += $(ASM_FILES:$(SRC_DIR)/%.S=$(OBJS_DIR)/%_s.o)
OBJ_FILES += $(FONT_PSF_OBJ)
DEP_FILES = $(OBJ_FILES:%.o=%.d)
-include $(DEP_FILES)

LOADER_SOURCE_FIND = find $(LOADER_DIR) -type d -name '_*' -prune -o -type f
# Cache loader file list then filter
LOADER_SOURCE_FILES := $(shell $(LOADER_SOURCE_FIND) -print)
LOADER_C_FILES := $(filter %.c,$(LOADER_SOURCE_FILES))
LOADER_CPP_FILES := $(filter %.cpp,$(LOADER_SOURCE_FILES))
LOADER_ASM_FILES := $(filter %.S,$(LOADER_SOURCE_FILES))
LOADER_OBJ_FILES = $(LOADER_C_FILES:$(LOADER_DIR)/%.c=$(OBJS_DIR)/kernel-loader/%_c.o)
LOADER_OBJ_FILES += $(LOADER_CPP_FILES:$(LOADER_DIR)/%.cpp=$(OBJS_DIR)/kernel-loader/%_cpp.o)
LOADER_OBJ_FILES += $(LOADER_ASM_FILES:$(LOADER_DIR)/%.S=$(OBJS_DIR)/kernel-loader/%_s.o)
LOADER_DEP_FILES = $(LOADER_OBJ_FILES:%.o=%.d)
-include $(LOADER_DEP_FILES)

BOOTLOADER_OBJ_FILES = $(LOADER_OBJ_FILES)
MAIN_KERNEL_OBJ_FILES = $(filter-out $(OBJS_DIR)/arch/cortex-a53/boot/% $(OBJS_DIR)/arch/cortex-a53/secondary_entry_s.o,$(OBJ_FILES))


#######################################################################################################
USER_BUILD_DIR = $(BUILD_DIR)/applications
USER_VFS_MOUNT_DIR = $(USER_BUILD_DIR)/fat32mnt
USER_VFS_MOUNT_DIR_ABS = $(abspath $(USER_VFS_MOUNT_DIR))
USER_VFS_BIN_DIR = $(USER_VFS_MOUNT_DIR)/bin
USER_VFS_LIB_DIR = $(USER_VFS_MOUNT_DIR)/lib
USER_VFS_DEV_FILE = $(USER_BUILD_DIR)/fat32.dev
USER_VFS_ATTACH = hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount -readwrite fat32.img
MAIN_KERNEL_PACKER = python3 tools/my-loader/ldr_build.py --pack-kernel
APP_TEMPLATE_DIR = $(APP_ROOT)/_templates
MY_LOADER_BUILDER_SCRIPT = $(MY_LOADER_TOOLS_ROOT)/ldr_build.py
MY_LOADER_BUILDER = python3 $(MY_LOADER_BUILDER_SCRIPT)
MY_LOADER_OUTPUT = $(BUILD_DIR)/my-loader
MY_LOADER_TOOLS_ROOT = tools/my-loader
MY_LOADER_COMMON_FLAGS = --project-root . --include-dir kernel/include --include-dir $(APP_INCLUDE_DIR) --define LDR_MVP=1 --cflag=-O2 --cflag=-g --cflag=-ffunction-sections --cflag=-fdata-sections --ldflag=--gc-sections --output-dir $(MY_LOADER_OUTPUT)
APP_DIRS := $(patsubst %/,%,$(sort $(wildcard $(APP_APPS_DIR)/*/)))
LIB_DIRS := $(patsubst %/,%,$(sort $(wildcard $(APP_LIBS_DIR)/*/)))
DRIVER_DIRS := $(patsubst %/,%,$(sort $(wildcard $(APP_DRIVERS_DIR)/*/)))
MY_LOADER_APP_TARGETS =
MY_LOADER_DLL_TARGETS =
MY_LOADER_DRIVER_TARGETS =

define BUILD_APP_ARTIFACT
APP_$(1)_SOURCES := $$(sort $$(wildcard $(APP_APPS_DIR)/$(1)/*.c) $$(wildcard $(APP_APPS_DIR)/$(1)/*.cpp) $$(wildcard $(APP_APPS_DIR)/$(1)/*.S))
MY_LOADER_APP_TARGETS += $(MY_LOADER_OUTPUT)/$(1).exe

$(MY_LOADER_OUTPUT)/$(1).exe: $(MY_LOADER_BUILDER_SCRIPT) $$(APP_$(1)_SOURCES)
	@mkdir -p $(MY_LOADER_OUTPUT)
	@$(MY_LOADER_BUILDER) --name $(1) --kind exe $$(foreach src,$$(APP_$(1)_SOURCES),--source $$(src)) --entry-symbol AppMain $(MY_LOADER_COMMON_FLAGS)
endef

define BUILD_LIB_ARTIFACT
LIB_$(1)_SOURCES := $$(sort $$(wildcard $(APP_LIBS_DIR)/$(1)/*.c) $$(wildcard $(APP_LIBS_DIR)/$(1)/*.cpp) $$(wildcard $(APP_LIBS_DIR)/$(1)/*.S))
MY_LOADER_DLL_TARGETS += $(MY_LOADER_OUTPUT)/$(1).dll

$(MY_LOADER_OUTPUT)/$(1).dll: $(MY_LOADER_BUILDER_SCRIPT) $$(LIB_$(1)_SOURCES)
	@mkdir -p $(MY_LOADER_OUTPUT)
	@$(MY_LOADER_BUILDER) --name $(1) --kind dll $$(foreach src,$$(LIB_$(1)_SOURCES),--source $$(src)) --entry-symbol Init $(MY_LOADER_COMMON_FLAGS)
endef

define BUILD_DRIVER_ARTIFACT
DRIVER_$(1)_SOURCES := $$(sort $$(wildcard $(APP_DRIVERS_DIR)/$(1)/*.c) $$(wildcard $(APP_DRIVERS_DIR)/$(1)/*.cpp) $$(wildcard $(APP_DRIVERS_DIR)/$(1)/*.S))
MY_LOADER_DRIVER_TARGETS += $(MY_LOADER_OUTPUT)/$(1).sys

$(MY_LOADER_OUTPUT)/$(1).sys: $(MY_LOADER_BUILDER_SCRIPT) $$(DRIVER_$(1)_SOURCES)
	@mkdir -p $(MY_LOADER_OUTPUT)
	@$(MY_LOADER_BUILDER) --name $(1) --kind sys $$(foreach src,$$(DRIVER_$(1)_SOURCES),--source $$(src)) --entry-symbol Init $(MY_LOADER_COMMON_FLAGS)
endef

$(foreach app,$(notdir $(APP_DIRS)),$(eval $(call BUILD_APP_ARTIFACT,$(app))))
$(foreach lib,$(notdir $(LIB_DIRS)),$(eval $(call BUILD_LIB_ARTIFACT,$(lib))))
$(foreach driver,$(notdir $(DRIVER_DIRS)),$(eval $(call BUILD_DRIVER_ARTIFACT,$(driver))))

MY_LOADER_STAGE_APP_TARGETS := $(if $(filter $(MY_LOADER_OUTPUT)/core.exe,$(MY_LOADER_APP_TARGETS)),$(filter $(MY_LOADER_OUTPUT)/core.exe,$(MY_LOADER_APP_TARGETS)),$(MY_LOADER_APP_TARGETS))
MY_LOADER_STAGE_DLL_TARGETS := $(if $(filter $(MY_LOADER_OUTPUT)/sample_lib.dll,$(MY_LOADER_DLL_TARGETS)),$(filter $(MY_LOADER_OUTPUT)/sample_lib.dll,$(MY_LOADER_DLL_TARGETS)),$(MY_LOADER_DLL_TARGETS))
MY_LOADER_STAGE_DRIVER_TARGETS := $(if $(filter $(MY_LOADER_OUTPUT)/sample_driver.sys,$(MY_LOADER_DRIVER_TARGETS)),$(filter $(MY_LOADER_OUTPUT)/sample_driver.sys,$(MY_LOADER_DRIVER_TARGETS)),$(MY_LOADER_DRIVER_TARGETS))

my-loader-artifacts: $(MY_LOADER_APP_TARGETS) $(MY_LOADER_DLL_TARGETS) $(MY_LOADER_DRIVER_TARGETS)

applications: my-loader-artifacts

user: applications

new-app:
	@set -e; \
	if [ -z "$(NAME)" ]; then \
		echo "Usage: make new-app NAME=<app_name> [APP_LANG=c|cpp]"; \
		exit 1; \
	fi; \
	if ! printf '%s' "$(NAME)" | grep -Eq '^[A-Za-z_][A-Za-z0-9_]*$$'; then \
		echo "NAME must be a C identifier: letters, digits, and underscores only, not starting with a digit"; \
		exit 1; \
	fi; \
	LANG_CHOICE="$(APP_LANG)"; \
	if [ -z "$$LANG_CHOICE" ]; then LANG_CHOICE=c; fi; \
	case "$$LANG_CHOICE" in \
		c) TEMPLATE="$(APP_TEMPLATE_DIR)/program-c/main.c"; DEST="$(APP_APPS_DIR)/$(NAME)/main.c" ;; \
		cpp|c++) TEMPLATE="$(APP_TEMPLATE_DIR)/program-cpp/main.cpp"; DEST="$(APP_APPS_DIR)/$(NAME)/main.cpp" ;; \
		*) echo "APP_LANG must be c or cpp"; exit 1 ;; \
	esac; \
	if [ -e "$(APP_APPS_DIR)/$(NAME)" ]; then \
		echo "Application already exists: $(APP_APPS_DIR)/$(NAME)"; \
		exit 1; \
	fi; \
	mkdir -p "$(APP_APPS_DIR)/$(NAME)"; \
	sed -e 's/__APP_NAME__/$(NAME)/g' "$$TEMPLATE" > "$$DEST"; \
	echo "Created $$DEST"

new-dll:
	@set -e; \
	if [ -z "$(NAME)" ]; then \
		echo "Usage: make new-dll NAME=<dll_name>"; \
		exit 1; \
	fi; \
	if ! printf '%s' "$(NAME)" | grep -Eq '^[A-Za-z_][A-Za-z0-9_]*$$'; then \
		echo "NAME must be a C identifier: letters, digits, and underscores only, not starting with a digit"; \
		exit 1; \
	fi; \
	if [ -e "$(APP_LIBS_DIR)/$(NAME)" ]; then \
		echo "Library already exists: $(APP_LIBS_DIR)/$(NAME)"; \
		exit 1; \
	fi; \
	mkdir -p "$(APP_LIBS_DIR)/$(NAME)"; \
	sed -e 's/__DLL_NAME__/$(NAME)/g' "$(APP_TEMPLATE_DIR)/dll/main.c" > "$(APP_LIBS_DIR)/$(NAME)/main.c"; \
	echo "Created $(APP_LIBS_DIR)/$(NAME)/main.c"

new-sys:
	@set -e; \
	if [ -z "$(NAME)" ]; then \
		echo "Usage: make new-sys NAME=<driver_name>"; \
		exit 1; \
	fi; \
	if ! printf '%s' "$(NAME)" | grep -Eq '^[A-Za-z_][A-Za-z0-9_]*$$'; then \
		echo "NAME must be a C identifier: letters, digits, and underscores only, not starting with a digit"; \
		exit 1; \
	fi; \
	if [ -e "$(APP_DRIVERS_DIR)/$(NAME)" ]; then \
		echo "Driver already exists: $(APP_DRIVERS_DIR)/$(NAME)"; \
		exit 1; \
	fi; \
	mkdir -p "$(APP_DRIVERS_DIR)/$(NAME)"; \
	sed -e 's/__SYS_NAME__/$(NAME)/g' "$(APP_TEMPLATE_DIR)/system/module.c" > "$(APP_DRIVERS_DIR)/$(NAME)/main.c"; \
	echo "Created $(APP_DRIVERS_DIR)/$(NAME)/main.c"

new-app-pair:
	@set -e; \
	if [ -z "$(APP_NAME)" ] || [ -z "$(DLL_NAME)" ]; then \
		echo "Usage: make new-app-pair APP_NAME=<app_name> DLL_NAME=<dll_name>"; \
		exit 1; \
	fi; \
	$(MAKE) new-app NAME="$(APP_NAME)"; \
	$(MAKE) new-dll NAME="$(DLL_NAME)"

ifeq ($(IS_WINDOWS),1)
applications-vfs:
	@echo "applications-vfs is not supported on native Windows shells."
	@echo "Use WSL, MSYS2, or run this Makefile on macOS/Linux to use 'applications-vfs'."

user-vfs: applications-vfs

fat32-list:
	@echo "fat32-list is not supported on native Windows shells."
	@echo "Use WSL/MSYS2 or mount fat32.img manually."

fat32-mount:
	@echo "fat32-mount is not supported on native Windows shells."

fat32-umount:
	@echo "fat32-umount is not supported on native Windows shells."

fat32-read:
	@echo "fat32-read is not supported on native Windows shells."

fat32-hexdump:
	@echo "fat32-hexdump is not supported on native Windows shells."
else
applications-vfs: applications $(MAIN_KERNEL_IMAGE)
	@set -e; \
	if mount | grep -q " on $(USER_VFS_MOUNT_DIR_ABS) "; then umount -f $(USER_VFS_MOUNT_DIR_ABS) >/dev/null 2>&1 || true; fi; \
	if [ -f $(USER_VFS_DEV_FILE) ]; then hdiutil detach -force "$$(cat $(USER_VFS_DEV_FILE))" >/dev/null 2>&1 || true; rm -f $(USER_VFS_DEV_FILE); fi; \
	STALE_DEVS=$$(hdiutil info | awk -v target="$$PWD/fat32.img" '/image-path/ && $$NF == target { found=1; next } found && /^\/dev\// { print $$1; found=0 }'); \
	for STALE_DEV in $$STALE_DEVS; do hdiutil detach -force "$$STALE_DEV" >/dev/null 2>&1 || true; done; \
	mkdir -p $(USER_VFS_MOUNT_DIR_ABS); \
	DEV=$$($(USER_VFS_ATTACH) | awk 'NR==1 { print $$1 }'); \
	if [ -z "$$DEV" ]; then echo "Failed to attach fat32.img"; exit 1; fi; \
	trap 'umount -f $(USER_VFS_MOUNT_DIR_ABS) >/dev/null 2>&1 || true; hdiutil detach -force "'"'$$DEV'"'" >/dev/null 2>&1 || true' EXIT; \
	newfs_msdos -F 32 -O ROS -S 512 -c 1 -n 2 -v ROSROOT "$$DEV" >/dev/null; \
	mount -t msdos "$$DEV" $(USER_VFS_MOUNT_DIR_ABS); \
	mkdir -p $(USER_VFS_BIN_DIR); \
	mkdir -p $(USER_VFS_LIB_DIR); \
	mkdir -p $(USER_VFS_MOUNT_DIR)/system; \
	cp "$(MAIN_KERNEL_IMAGE)" $(USER_VFS_MOUNT_DIR)/$(ROSKRNL_NAME); \
	for exe in $(MY_LOADER_STAGE_APP_TARGETS); do cp "$${exe}" $(USER_VFS_BIN_DIR)/$$(basename "$$exe"); done; \
	for dll in $(MY_LOADER_STAGE_DLL_TARGETS); do cp "$${dll}" $(USER_VFS_LIB_DIR)/$$(basename "$$dll"); done; \
	for sys in $(MY_LOADER_STAGE_DRIVER_TARGETS); do cp "$${sys}" $(USER_VFS_MOUNT_DIR)/system/$$(basename "$$sys"); done; \
	echo "FAT32 image contents after sync:"; \
	find $(USER_VFS_MOUNT_DIR_ABS) -mindepth 1 -maxdepth 2 -print | sed 's#^$(USER_VFS_MOUNT_DIR_ABS)##' | LC_ALL=C sort; \
	umount -f $(USER_VFS_MOUNT_DIR_ABS); \
	hdiutil detach -force "$$DEV" >/dev/null; \
	trap - EXIT
endif

user-vfs: applications-vfs

fat32-list:
	@set -e; \
	if mount | grep -q " on $(USER_VFS_MOUNT_DIR_ABS) "; then umount -f $(USER_VFS_MOUNT_DIR_ABS) >/dev/null 2>&1 || true; fi; \
	if [ -f $(USER_VFS_DEV_FILE) ]; then hdiutil detach -force "$$(cat $(USER_VFS_DEV_FILE))" >/dev/null 2>&1 || true; rm -f $(USER_VFS_DEV_FILE); fi; \
	STALE_DEVS=$$(hdiutil info | awk -v target="$$PWD/fat32.img" '/image-path/ && $$NF == target { found=1; next } found && /^\/dev\// { print $$1; found=0 }'); \
	for STALE_DEV in $$STALE_DEVS; do hdiutil detach -force "$$STALE_DEV" >/dev/null 2>&1 || true; done; \
	mkdir -p $(USER_VFS_MOUNT_DIR_ABS); \
	DEV=$$($(USER_VFS_ATTACH) | awk 'NR==1 { print $$1 }'); \
	if [ -z "$$DEV" ]; then echo "Failed to attach fat32.img"; exit 1; fi; \
	trap 'umount -f $(USER_VFS_MOUNT_DIR_ABS) >/dev/null 2>&1 || true; hdiutil detach -force "'"'$$DEV'"'" >/dev/null 2>&1 || true' EXIT; \
	mount -t msdos "$$DEV" $(USER_VFS_MOUNT_DIR_ABS); \
	echo "FAT32 image contents:"; \
	find $(USER_VFS_MOUNT_DIR_ABS) -mindepth 1 -maxdepth 2 -print | sed 's#^$(USER_VFS_MOUNT_DIR_ABS)##' | LC_ALL=C sort; \
	umount -f $(USER_VFS_MOUNT_DIR_ABS); \
	hdiutil detach -force "$$DEV" >/dev/null; \
	trap - EXIT

fat32-mount:
	@set -e; \
	mkdir -p $(USER_VFS_MOUNT_DIR_ABS); \
	if mount | grep -q " on $(USER_VFS_MOUNT_DIR_ABS) "; then \
		echo "fat32.img is already mounted at $(USER_VFS_MOUNT_DIR_ABS)"; \
		exit 0; \
	fi; \
	if [ -f $(USER_VFS_DEV_FILE) ]; then hdiutil detach -force "$$(cat $(USER_VFS_DEV_FILE))" >/dev/null 2>&1 || true; rm -f $(USER_VFS_DEV_FILE); fi; \
	STALE_DEVS=$$(hdiutil info | awk -v target="$$PWD/fat32.img" '/image-path/ && $$NF == target { found=1; next } found && /^\/dev\// { print $$1; found=0 }'); \
	for STALE_DEV in $$STALE_DEVS; do hdiutil detach -force "$$STALE_DEV" >/dev/null 2>&1 || true; done; \
	DEV=$$($(USER_VFS_ATTACH) | awk 'NR==1 { print $$1 }'); \
	if [ -z "$$DEV" ]; then echo "Failed to attach fat32.img"; exit 1; fi; \
	echo "$$DEV" > $(USER_VFS_DEV_FILE); \
	mount -t msdos "$$DEV" $(USER_VFS_MOUNT_DIR_ABS); \
	echo "Mounted fat32.img at $(USER_VFS_MOUNT_DIR_ABS) using $$DEV"

fat32-umount:
	@set -e; \
	if mount | grep -q " on $(USER_VFS_MOUNT_DIR_ABS) "; then \
		umount -f $(USER_VFS_MOUNT_DIR_ABS) >/dev/null 2>&1 || true; \
	fi; \
	if [ -f $(USER_VFS_DEV_FILE) ]; then \
		DEV=$$(cat $(USER_VFS_DEV_FILE)); \
		hdiutil detach -force "$$DEV" >/dev/null 2>&1 || true; \
		rm -f $(USER_VFS_DEV_FILE); \
		echo "Detached $$DEV"; \
	else \
		echo "No recorded FAT32 device to detach"; \
	fi; \
	STALE_DEVS=$$(hdiutil info | awk -v target="$$PWD/fat32.img" '/image-path/ && $$NF == target { found=1; next } found && /^\/dev\// { print $$1; found=0 }'); \
	for STALE_DEV in $$STALE_DEVS; do hdiutil detach -force "$$STALE_DEV" >/dev/null 2>&1 || true; echo "Detached $$STALE_DEV"; done

fat32-read:
	@set -e; \
	if [ -z "$(FILE)" ]; then \
		echo "Usage: make fat32-read FILE=/path/in/image"; \
		exit 1; \
	fi; \
	if ! mount | grep -q " on $(USER_VFS_MOUNT_DIR_ABS) "; then \
		echo "fat32.img is not mounted. Run 'make fat32-mount' first."; \
		exit 1; \
	fi; \
	TARGET="$(USER_VFS_MOUNT_DIR_ABS)$(FILE)"; \
	if [ ! -f "$$TARGET" ]; then \
		echo "File not found in FAT32 image: $(FILE)"; \
		exit 1; \
	fi; \
	echo "Reading $(FILE):"; \
	cat "$$TARGET"

fat32-hexdump:
	@set -e; \
	if [ -z "$(FILE)" ]; then \
		echo "Usage: make fat32-hexdump FILE=/path/in/image"; \
		exit 1; \
	fi; \
	if ! mount | grep -q " on $(USER_VFS_MOUNT_DIR_ABS) "; then \
		echo "fat32.img is not mounted. Run 'make fat32-mount' first."; \
		exit 1; \
	fi; \
	TARGET="$(USER_VFS_MOUNT_DIR_ABS)$(FILE)"; \
	if [ ! -f "$$TARGET" ]; then \
		echo "File not found in FAT32 image: $(FILE)"; \
		exit 1; \
	fi; \
	echo "Hexdump of $(FILE):"; \
	xxd -g 1 -l 256 "$$TARGET"
#######################################################################################################


ifeq ($(IS_WINDOWS),1)
f32.disk:
	@echo "f32.disk target is not supported on native Windows shells."

f32.empty:
	@echo "f32.empty target is not supported on native Windows shells."

populate_disk:
	@echo "populate_disk is not supported on native Windows shells."
else
f32.disk:
	-rm f32.disk
	dd if=/dev/zero of=f32.disk bs=1M count=64
	sudo hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount f32.disk
	sudo newfs_msdos -F 32 -O ROS -S 512 -c 1 -n 2 -v ROSROOT /dev/disk2
	sudo hdiutil detach /dev/disk2
f32.empty:
	rm f32.disk&& cp f32.empty.disk f32.disk
populate_disk: mount_disk
	sudo cp *.c *.h fat32
	sudo cp -R deps fat32/
	sudo mkdir -p fat32/foo/bar/baz/boo/dep/doo/poo/goo/
	sudo cp common.h fat32/foo/bar/baz/boo/dep/doo/poo/goo/tood.txt
	sleep 1
	sudo umount fat32
	-@rm -Rf fat32
endif

#######################################################################################################


$(MAIN_KERNEL_ELF): $(MAIN_KERNEL_LINKER_SCRIPT) $(MAIN_KERNEL_OBJ_FILES)
	@mkdir -p $(BUILD_DIR)
	@$(ARMGNU)-ld -nostdlib -T $(MAIN_KERNEL_LINKER_SCRIPT) -o $(MAIN_KERNEL_ELF) $(MAIN_KERNEL_OBJ_FILES) -g

$(MAIN_KERNEL_IMAGE): $(MAIN_KERNEL_ELF) tools/my-loader/ldr_build.py
	@mkdir -p $(BUILD_DIR)
	@$(MAIN_KERNEL_PACKER) --input $(MAIN_KERNEL_ELF) --output $(MAIN_KERNEL_IMAGE)

$(KERNEL_IMG): $(LOADER_LINKER_SCRIPT) $(BOOTLOADER_OBJ_FILES)
	@mkdir -p $(BUILD_DIR)
	@$(ARMGNU)-ld -nostdlib -T $(LOADER_LINKER_SCRIPT) -o $(KERNEL_ELF) $(BOOTLOADER_OBJ_FILES) -g
	@$(ARMGNU)-objcopy $(KERNEL_ELF) -O binary $(KERNEL_IMG)

kernel8.img: applications-vfs $(MAIN_KERNEL_IMAGE) $(KERNEL_IMG)
	@true
dump: all
	@$(ARMGNU)-objdump --all-headers $(KERNEL_ELF)
diasm: all
	@$(ARMGNU)-objdump --all-headers $(KERNEL_ELF)
ifeq ($(IS_WINDOWS),1)
run: QEMU_GUI=0
run: kernel8.img
	@echo "Running on Windows: --------------------------------------------------------------------- "
	@echo "Using QEMU: $(QEMU)"
	"$(QEMU)" -M $(QEMU_MACHINE) -smp $(QEMU_CPUS) -kernel "$(KERNEL_IMG)" -serial stdio $(QEMU_USB_ARGS) -drive file=fat32.img,if=sd,format=raw
else
run: QEMU_GUI=0
run: kernel8.img
	@echo "Running: --------------------------------------------------------------------------------- "
	@$(QEMU) -M $(QEMU_MACHINE) -smp $(QEMU_CPUS) -kernel $(KERNEL_IMG) -serial stdio $(QEMU_DISPLAY_ARGS) $(QEMU_USB_ARGS) -drive file=fat32.img,if=sd,format=raw
endif
run-gfx: QEMU_GUI=1
run-gfx: kernel8.img
	@echo "Running with framebuffer display --------------------------------------------------------- "
	@$(QEMU) -M $(QEMU_MACHINE) -smp $(QEMU_CPUS) -kernel $(KERNEL_IMG) -serial stdio -display default $(QEMU_USB_ARGS) -drive file=fat32.img,if=sd,format=raw
run-headless: kernel8.img
	@echo "Running headless ------------------------------------------------------------------------- "
	@$(QEMU) -M $(QEMU_MACHINE) -smp $(QEMU_CPUS) -kernel $(KERNEL_IMG) -serial stdio -display none -drive file=fat32.img,if=sd,format=raw

# One-command bridge regression smoke test for /bin/myldr-user_app.exe.
smoke-myldr-user-app: kernel8.img
	@bash tools/my-loader/smoke_user_app.sh

debug: kernel8.img
	@echo "QEMU starting. Remember to start gdb------------------------------------------------------ "
	# @$(QEMU) -M $(QEMU_MACHINE) -kernel $(KERNEL_IMG) -serial null -serial stdio -display none -s -S -d trace:bcm2835_systmr*
	@$(QEMU) -M $(QEMU_MACHINE) -smp $(QEMU_CPUS) -kernel $(KERNEL_IMG) -serial stdio -display none -s -S

asm: kernel8.img
	@echo "Running: --------------------------------------------------------------------------------- "
	@$(QEMU) -M $(QEMU_MACHINE) -smp $(QEMU_CPUS) -kernel $(KERNEL_IMG) -serial null -d in_asm -monitor stdio -nographic -S -gdb tcp::1234
gdb:
	gdb -ex 'file $(KERNEL_ELF)' -ex 'set arch aarch64' -ex 'target remote localhost:1234' -ex 'layout split' -ex 'layout regs' -ex 'b _start' -ex 'b breakpoint' -q --nh
