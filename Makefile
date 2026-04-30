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
APP_DLLBURST_DIR := $(APP_ROOT)/apps/dllburst
APP_DLLREBASEPROBE_DIR := $(APP_ROOT)/apps/dllrebaseprobe
APP_DLLSMOKE_DIR := $(APP_ROOT)/apps/dllsmoke
APP_CONSOLEDEMO_DIR := $(APP_ROOT)/apps/consoledemo
APP_EXPLORER_DIR := $(APP_ROOT)/apps/explorer
APP_GUESAMPLE_DIR := $(APP_ROOT)/apps/guisample
APP_GWES_DIR := $(APP_ROOT)/apps/gwes
APP_TTFDEMO_DIR := $(APP_ROOT)/apps/ttfdemo
APP_IMAGEBOXDEMO_DIR := $(APP_ROOT)/apps/imageboxdemo
APP_GWES_GDI_BRIDGE_SOURCE := $(APP_GWES_DIR)/gdi_bridge.c
APP_GWES_JPEG_RENDER_SOURCE := $(APP_GWES_DIR)/jpeg_render.cpp
APP_GDI_DIR := $(APP_ROOT)/libs/gdi
APP_JPEG_SOURCE := $(APP_GDI_DIR)/jpeg.cpp
APP_PNG_DIR := $(APP_ROOT)/libs/png
APP_KEVENTMON_DIR := $(APP_ROOT)/apps/keventmon
APP_MULTIWIN_DIR := $(APP_ROOT)/apps/multiwin
APP_SHELL_DIR := $(APP_ROOT)/apps/shell
APP_SYSTEMTEST_DIR := $(APP_ROOT)/apps/systemtest
APP_CRTSMOKE_DIR := $(APP_ROOT)/apps/crtsmoke
APP_TESTUSERHEAP_EXE_DIR := $(APP_ROOT)/apps/testuserheap
APP_TESTUSERHEAP_DLL_DIR := $(APP_ROOT)/libs/testuserheap
APP_TESTUSERHEAP2_EXE_DIR := $(APP_ROOT)/apps/testuserheap2
APP_TESTUSERHEAP2_DLL_DIR := $(APP_ROOT)/libs/testuserheap2
APP_FILEMAPPING_READER_DIR := $(APP_ROOT)/apps/filemappingreader
APP_FILEMAPPING_WRITER_DIR := $(APP_ROOT)/apps/filemappingwriter
APP_IRQHANDOFF_DIR := $(APP_ROOT)/apps/irqhandoff
APP_RCMAN_DIR := $(APP_ROOT)/apps/rcman
APP_WIDGETDEMO_DIR := $(APP_ROOT)/apps/widgetdemo
APP_SAMPLEMATH_DIR := $(APP_ROOT)/libs/samplemath
APP_WIDGETS_DIR := $(APP_ROOT)/libs/widgets
APP_WINDOWKIT_SOURCE := $(APP_ROOT)/libs/window/window.cpp
APP_WIDGETS_SOURCE := $(APP_WIDGETS_DIR)/widgets.cpp
APP_RUNTIME_WINDOW_SOURCE := $(APP_RUNTIME_DIR)/window_support.c
APP_RUNTIME_GDI_SOURCE := $(APP_RUNTIME_DIR)/gdi_support.c
APP_RUNTIME_GDI_FONT_SOURCE := $(APP_RUNTIME_DIR)/gdi_font_support.c
APP_RUNTIME_WIDGET_SOURCE := $(APP_RUNTIME_DIR)/widget_support.c
APP_CRT_SOURCE := $(APP_RUNTIME_DIR)/crt.c
APP_RUNTIME_CPP_SOURCE := $(APP_RUNTIME_DIR)/cpp_runtime.cpp
APP_LIB_CRT_DIR := $(APP_ROOT)/libs/crt
APP_LIB_CRT_SOURCE := $(APP_LIB_CRT_DIR)/crt.cpp
APP_SYSTEM_UI_FONT_ASSET := $(APP_FONT_ASSET_DIR)/system_ui.font
APP_EXPLORER_GENERATED_DIR := $(APP_EXPLORER_DIR)/generated
APP_EXPLORER_START_ICON_HEADER := $(APP_EXPLORER_GENERATED_DIR)/start_icon.h
APP_EXPLORER_START_ICON_SCRIPT := tools/generate_explorer_start_icon.py
USER_OUTPUT_ROOT := $(OUTPUT_ROOT)/userspace
USER_CORE_ARTIFACT := $(USER_OUTPUT_ROOT)/core.exe
USER_DLLBURST_ARTIFACT := $(USER_OUTPUT_ROOT)/dllburst.exe
USER_DLLREBASEPROBE_ARTIFACT := $(USER_OUTPUT_ROOT)/dllrebaseprobe.exe
USER_CONSOLEDEMO_ARTIFACT := $(USER_OUTPUT_ROOT)/consoledemo.exe
USER_DLLSMOKE_ARTIFACT := $(USER_OUTPUT_ROOT)/dllsmoke.exe
USER_EXPLORER_ARTIFACT := $(USER_OUTPUT_ROOT)/explorer.exe
USER_GUESAMPLE_ARTIFACT := $(USER_OUTPUT_ROOT)/guisample.exe
USER_GWES_ARTIFACT := $(USER_OUTPUT_ROOT)/gwes.exe
USER_TTFDEMO_ARTIFACT := $(USER_OUTPUT_ROOT)/ttfdemo.exe
USER_IMAGEBOXDEMO_ARTIFACT := $(USER_OUTPUT_ROOT)/imageboxdemo.exe
USER_GDI_ARTIFACT := $(USER_OUTPUT_ROOT)/gdi.dll
USER_JPEG_ARTIFACT := $(USER_OUTPUT_ROOT)/jpeg.dll
USER_KEVENTMON_ARTIFACT := $(USER_OUTPUT_ROOT)/kevent.exe
USER_MULTIWIN_ARTIFACT := $(USER_OUTPUT_ROOT)/multiwin.exe
USER_CRT_ARTIFACT := $(USER_OUTPUT_ROOT)/crt.dll
USER_PNG_ARTIFACT := $(USER_OUTPUT_ROOT)/png.dll
USER_SAMPLEMATH_ARTIFACT := $(USER_OUTPUT_ROOT)/samplemath.dll
USER_SHELL_ARTIFACT := $(USER_OUTPUT_ROOT)/shell.exe
USER_SYSTEMTEST_ARTIFACT := $(USER_OUTPUT_ROOT)/systemtest.exe
USER_CRTSMOKE_ARTIFACT := $(USER_OUTPUT_ROOT)/crtsmoke.exe
USER_TESTUSERHEAP_EXE_ARTIFACT := $(USER_OUTPUT_ROOT)/testuserheap.exe
USER_TESTUSERHEAP_DLL_ARTIFACT := $(USER_OUTPUT_ROOT)/testuserheap.dll
USER_TESTUSERHEAP2_EXE_ARTIFACT := $(USER_OUTPUT_ROOT)/testuserheap2.exe
USER_TESTUSERHEAP2_DLL_ARTIFACT := $(USER_OUTPUT_ROOT)/testuserheap2.dll
USER_FILEMAPPING_READER_ARTIFACT := $(USER_OUTPUT_ROOT)/filemapping_reader.exe
USER_FILEMAPPING_WRITER_ARTIFACT := $(USER_OUTPUT_ROOT)/filemapping_writer.exe
USER_IRQHANDOFF_ARTIFACT := $(USER_OUTPUT_ROOT)/irqhandoff.exe
USER_RCMAN_ARTIFACT := $(USER_OUTPUT_ROOT)/rcman.exe
USER_WIDGETDEMO_ARTIFACT := $(USER_OUTPUT_ROOT)/widgetdemo.exe
USER_WIDGETS_ARTIFACT := $(USER_OUTPUT_ROOT)/widgets.dll
USER_WINDOWKIT_ARTIFACT := $(USER_OUTPUT_ROOT)/window.dll
USER_BUILDER := $(PYTHON) tools/my-loader/ldr_build.py
USER_BUILDER_COMMON_FLAGS := --project-root . --include-dir $(KERNEL_INCLUDE_DIR) --include-dir $(APP_INCLUDE_DIR) --define LDR_MVP=1 --cflag=-O2 --cflag=-g --cflag=-ffunction-sections --cflag=-fdata-sections --ldflag=--gc-sections --cflag=-fno-inline --output-dir $(USER_OUTPUT_ROOT)
USER_GWES_BUILDER_FLAGS := $(USER_BUILDER_COMMON_FLAGS) --include-dir $(APP_GWES_DIR) --include-dir $(APP_GWES_DIR)/include
USER_EXPLORER_BUILDER_FLAGS := $(USER_BUILDER_COMMON_FLAGS) --include-dir $(APP_GWES_DIR) --include-dir $(APP_GWES_DIR)/include
USER_GDI_BUILDER_FLAGS := $(USER_BUILDER_COMMON_FLAGS) --include-dir $(APP_GWES_DIR) --include-dir $(APP_GWES_DIR)/include
USER_RUNTIME_SOURCES := $(sort \
	$(wildcard $(APP_RUNTIME_DIR)/*.c) \
	$(wildcard $(APP_RUNTIME_DIR)/*.cpp) \
	$(wildcard $(APP_RUNTIME_DIR)/*.cc) \
	$(wildcard $(APP_RUNTIME_DIR)/*.cxx))
USER_RUNTIME_COMMON_SOURCES := $(filter-out $(APP_RUNTIME_WINDOW_SOURCE) $(APP_RUNTIME_GDI_SOURCE) $(APP_RUNTIME_GDI_FONT_SOURCE) $(APP_RUNTIME_WIDGET_SOURCE) $(APP_CRT_SOURCE),$(USER_RUNTIME_SOURCES))
USER_CORE_SOURCES := $(sort \
	$(wildcard $(APP_CORE_DIR)/*.c) \
	$(wildcard $(APP_CORE_DIR)/*.cpp) \
	$(wildcard $(APP_CORE_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_DLLBURST_SOURCES := $(sort \
	$(wildcard $(APP_DLLBURST_DIR)/*.c) \
	$(wildcard $(APP_DLLBURST_DIR)/*.cpp) \
	$(wildcard $(APP_DLLBURST_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_DLLREBASEPROBE_SOURCES := $(sort \
	$(wildcard $(APP_DLLREBASEPROBE_DIR)/*.c) \
	$(wildcard $(APP_DLLREBASEPROBE_DIR)/*.cpp) \
	$(wildcard $(APP_DLLREBASEPROBE_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_CONSOLEDEMO_SOURCES := $(sort \
	$(wildcard $(APP_CONSOLEDEMO_DIR)/*.c) \
	$(wildcard $(APP_CONSOLEDEMO_DIR)/*.cpp) \
	$(wildcard $(APP_CONSOLEDEMO_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_DLLSMOKE_SOURCES := $(sort \
	$(wildcard $(APP_DLLSMOKE_DIR)/*.c) \
	$(wildcard $(APP_DLLSMOKE_DIR)/*.cpp) \
	$(wildcard $(APP_DLLSMOKE_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_EXPLORER_SOURCES := $(sort \
	$(wildcard $(APP_EXPLORER_DIR)/*.c) \
	$(wildcard $(APP_EXPLORER_DIR)/*.cpp) \
	$(wildcard $(APP_EXPLORER_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_GUESAMPLE_SOURCES := $(sort \
	$(wildcard $(APP_GUESAMPLE_DIR)/*.c) \
	$(wildcard $(APP_GUESAMPLE_DIR)/*.cpp) \
	$(wildcard $(APP_GUESAMPLE_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_GWES_SOURCES := $(filter-out $(APP_GWES_GDI_BRIDGE_SOURCE) $(APP_GWES_JPEG_RENDER_SOURCE),$(sort \
	$(wildcard $(APP_GWES_DIR)/*.c) \
	$(wildcard $(APP_GWES_DIR)/*.cpp) \
	$(wildcard $(APP_GWES_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES)))
USER_TTFDEMO_SOURCES := $(sort \
	$(wildcard $(APP_TTFDEMO_DIR)/*.c) \
	$(wildcard $(APP_TTFDEMO_DIR)/*.cpp) \
	$(wildcard $(APP_TTFDEMO_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_IMAGEBOXDEMO_SOURCES := $(sort \
	$(wildcard $(APP_IMAGEBOXDEMO_DIR)/*.c) \
	$(wildcard $(APP_IMAGEBOXDEMO_DIR)/*.cpp) \
	$(wildcard $(APP_IMAGEBOXDEMO_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_GDI_SOURCES := $(sort \
	$(APP_RUNTIME_CPP_SOURCE) \
	$(APP_GDI_DIR)/gdi.c \
	$(APP_RUNTIME_GDI_FONT_SOURCE))
USER_JPEG_SOURCES := $(sort \
	$(APP_JPEG_SOURCE) \
	$(APP_RUNTIME_CPP_SOURCE))
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
USER_PNG_SOURCES := $(sort \
	$(APP_PNG_DIR)/png.c \
	$(APP_RUNTIME_DIR)/ros_support.c)
USER_CRT_SOURCES := $(APP_LIB_CRT_SOURCE)
USER_SAMPLEMATH_SOURCES := $(sort \
	$(wildcard $(APP_SAMPLEMATH_DIR)/*.c) \
	$(wildcard $(APP_SAMPLEMATH_DIR)/*.cpp) \
	$(wildcard $(APP_SAMPLEMATH_DIR)/*.S) \
	$(APP_RUNTIME_CPP_SOURCE))
USER_WINDOWKIT_SOURCES := $(sort $(APP_WINDOWKIT_SOURCE) $(APP_RUNTIME_CPP_SOURCE))
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
USER_CRTSMOKE_SOURCES := $(sort \
	$(wildcard $(APP_CRTSMOKE_DIR)/*.c) \
	$(wildcard $(APP_CRTSMOKE_DIR)/*.cpp) \
	$(wildcard $(APP_CRTSMOKE_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_TESTUSERHEAP_EXE_SOURCES := $(sort \
	$(wildcard $(APP_TESTUSERHEAP_EXE_DIR)/*.c) \
	$(wildcard $(APP_TESTUSERHEAP_EXE_DIR)/*.cpp) \
	$(wildcard $(APP_TESTUSERHEAP_EXE_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_TESTUSERHEAP_DLL_SOURCES := $(sort \
	$(wildcard $(APP_TESTUSERHEAP_DLL_DIR)/*.c) \
	$(wildcard $(APP_TESTUSERHEAP_DLL_DIR)/*.cpp) \
	$(wildcard $(APP_TESTUSERHEAP_DLL_DIR)/*.S) \
	$(APP_RUNTIME_CPP_SOURCE))
USER_TESTUSERHEAP2_EXE_SOURCES := $(sort \
	$(wildcard $(APP_TESTUSERHEAP2_EXE_DIR)/*.c) \
	$(wildcard $(APP_TESTUSERHEAP2_EXE_DIR)/*.cpp) \
	$(wildcard $(APP_TESTUSERHEAP2_EXE_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_TESTUSERHEAP2_DLL_SOURCES := $(sort \
	$(wildcard $(APP_TESTUSERHEAP2_DLL_DIR)/*.c) \
	$(wildcard $(APP_TESTUSERHEAP2_DLL_DIR)/*.cpp) \
	$(wildcard $(APP_TESTUSERHEAP2_DLL_DIR)/*.S) \
	$(APP_RUNTIME_CPP_SOURCE))
USER_FILEMAPPING_READER_SOURCES := $(sort \
	$(wildcard $(APP_FILEMAPPING_READER_DIR)/*.c) \
	$(wildcard $(APP_FILEMAPPING_READER_DIR)/*.cpp) \
	$(wildcard $(APP_FILEMAPPING_READER_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_FILEMAPPING_WRITER_SOURCES := $(sort \
	$(wildcard $(APP_FILEMAPPING_WRITER_DIR)/*.c) \
	$(wildcard $(APP_FILEMAPPING_WRITER_DIR)/*.cpp) \
	$(wildcard $(APP_FILEMAPPING_WRITER_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_IRQHANDOFF_SOURCES := $(sort \
	$(wildcard $(APP_IRQHANDOFF_DIR)/*.c) \
	$(wildcard $(APP_IRQHANDOFF_DIR)/*.cpp) \
	$(wildcard $(APP_IRQHANDOFF_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_RCMAN_SOURCES := $(sort \
	$(wildcard $(APP_RCMAN_DIR)/*.c) \
	$(wildcard $(APP_RCMAN_DIR)/*.cpp) \
	$(wildcard $(APP_RCMAN_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_WIDGETDEMO_SOURCES := $(sort \
	$(wildcard $(APP_WIDGETDEMO_DIR)/*.c) \
	$(wildcard $(APP_WIDGETDEMO_DIR)/*.cpp) \
	$(wildcard $(APP_WIDGETDEMO_DIR)/*.S) \
	$(USER_RUNTIME_COMMON_SOURCES))
USER_WIDGETS_SOURCES := $(sort $(APP_WIDGETS_SOURCE) $(APP_RUNTIME_CPP_SOURCE))
DLLBURST_STAGE_COUNT ?= 0

USER_APP_ARTIFACTS := $(USER_CORE_ARTIFACT) $(USER_DLLBURST_ARTIFACT) $(USER_DLLREBASEPROBE_ARTIFACT) $(USER_CONSOLEDEMO_ARTIFACT) $(USER_DLLSMOKE_ARTIFACT) $(USER_EXPLORER_ARTIFACT) $(USER_GUESAMPLE_ARTIFACT) $(USER_GWES_ARTIFACT) $(USER_TTFDEMO_ARTIFACT) $(USER_IMAGEBOXDEMO_ARTIFACT) $(USER_KEVENTMON_ARTIFACT) $(USER_MULTIWIN_ARTIFACT) $(USER_SHELL_ARTIFACT) $(USER_SYSTEMTEST_ARTIFACT) $(USER_CRTSMOKE_ARTIFACT)
USER_APP_ARTIFACTS += $(USER_TESTUSERHEAP_EXE_ARTIFACT) $(USER_TESTUSERHEAP2_EXE_ARTIFACT) $(USER_WIDGETDEMO_ARTIFACT)
USER_APP_ARTIFACTS += $(USER_FILEMAPPING_READER_ARTIFACT) $(USER_FILEMAPPING_WRITER_ARTIFACT)
USER_APP_ARTIFACTS += $(USER_IRQHANDOFF_ARTIFACT)
USER_APP_ARTIFACTS += $(USER_RCMAN_ARTIFACT)
USER_DLL_ARTIFACTS := $(USER_SAMPLEMATH_ARTIFACT) $(USER_TESTUSERHEAP_DLL_ARTIFACT) $(USER_TESTUSERHEAP2_DLL_ARTIFACT) $(USER_WINDOWKIT_ARTIFACT) $(USER_GDI_ARTIFACT) $(USER_JPEG_ARTIFACT) $(USER_WIDGETS_ARTIFACT) $(USER_CRT_ARTIFACT) $(USER_PNG_ARTIFACT)
USER_ARTIFACTS := $(USER_APP_ARTIFACTS) $(USER_DLL_ARTIFACTS)
USER_DEP_FILES := $(addsuffix .d,$(USER_ARTIFACTS))

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
BOOTLOADER_DEP_FILES := $(BOOTLOADER_OBJ_FILES:.o=.d)
COMMON_RUNTIME_OBJ_FILES := $(OBJ_DIR)/kernel/lib/memory_c.o

-include $(DEP_FILES) $(BOOTLOADER_DEP_FILES) $(USER_DEP_FILES)

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

$(USER_CONSOLEDEMO_ARTIFACT): tools/my-loader/ldr_build.py $(USER_CONSOLEDEMO_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name consoledemo --kind exe $(foreach src,$(USER_CONSOLEDEMO_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_DLLBURST_ARTIFACT): tools/my-loader/ldr_build.py $(USER_DLLBURST_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name dllburst --kind exe $(foreach src,$(USER_DLLBURST_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_DLLREBASEPROBE_ARTIFACT): tools/my-loader/ldr_build.py $(USER_DLLREBASEPROBE_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name dllrebaseprobe --kind exe $(foreach src,$(USER_DLLREBASEPROBE_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_DLLSMOKE_ARTIFACT): tools/my-loader/ldr_build.py $(USER_DLLSMOKE_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name dllsmoke --kind exe $(foreach src,$(USER_DLLSMOKE_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_EXPLORER_ARTIFACT): tools/my-loader/ldr_build.py $(USER_EXPLORER_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name explorer --kind exe $(foreach src,$(USER_EXPLORER_SOURCES),--source $(src)) --entry-symbol _start $(USER_EXPLORER_BUILDER_FLAGS)

$(APP_EXPLORER_START_ICON_HEADER): $(APP_EXPLORER_START_ICON_SCRIPT)
	@mkdir -p $(APP_EXPLORER_GENERATED_DIR)
	@$(PYTHON) $(APP_EXPLORER_START_ICON_SCRIPT) --output $@

$(USER_GUESAMPLE_ARTIFACT): tools/my-loader/ldr_build.py $(USER_GUESAMPLE_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name guisample --kind exe $(foreach src,$(USER_GUESAMPLE_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_GWES_ARTIFACT): tools/my-loader/ldr_build.py $(USER_GWES_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name gwes --kind exe $(foreach src,$(USER_GWES_SOURCES),--source $(src)) --entry-symbol _start $(USER_GWES_BUILDER_FLAGS)

$(USER_TTFDEMO_ARTIFACT): tools/my-loader/ldr_build.py $(USER_TTFDEMO_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name ttfdemo --kind exe $(foreach src,$(USER_TTFDEMO_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_IMAGEBOXDEMO_ARTIFACT): tools/my-loader/ldr_build.py $(USER_IMAGEBOXDEMO_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name imageboxdemo --kind exe $(foreach src,$(USER_IMAGEBOXDEMO_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_EXPLORER_ARTIFACT): tools/my-loader/ldr_build.py $(APP_EXPLORER_START_ICON_HEADER) $(USER_EXPLORER_SOURCES)
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

$(USER_GDI_ARTIFACT): tools/my-loader/ldr_build.py $(USER_GDI_SOURCES) $(APP_RUNTIME_GDI_SOURCE)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name gdi --kind dll $(foreach src,$(USER_GDI_SOURCES),--source $(src)) --entry-symbol gdi_entry $(USER_GDI_BUILDER_FLAGS)

$(USER_JPEG_ARTIFACT): tools/my-loader/ldr_build.py $(USER_JPEG_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name jpeg --kind dll $(foreach src,$(USER_JPEG_SOURCES),--source $(src)) --entry-symbol jpeg_entry $(USER_BUILDER_COMMON_FLAGS)

$(USER_PNG_ARTIFACT): tools/my-loader/ldr_build.py $(USER_PNG_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name png --kind dll $(foreach src,$(USER_PNG_SOURCES),--source $(src)) --entry-symbol png_entry $(USER_BUILDER_COMMON_FLAGS)

$(USER_CRT_ARTIFACT): tools/my-loader/ldr_build.py $(USER_CRT_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name crt --kind dll $(foreach src,$(USER_CRT_SOURCES),--source $(src)) --entry-symbol crt_entry $(USER_BUILDER_COMMON_FLAGS)

$(USER_SHELL_ARTIFACT): tools/my-loader/ldr_build.py $(USER_SHELL_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name shell --kind exe $(foreach src,$(USER_SHELL_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_SYSTEMTEST_ARTIFACT): tools/my-loader/ldr_build.py $(USER_SYSTEMTEST_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name systemtest --kind exe $(foreach src,$(USER_SYSTEMTEST_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_CRTSMOKE_ARTIFACT): tools/my-loader/ldr_build.py $(USER_CRTSMOKE_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name crtsmoke --kind exe $(foreach src,$(USER_CRTSMOKE_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_TESTUSERHEAP_EXE_ARTIFACT): tools/my-loader/ldr_build.py $(USER_TESTUSERHEAP_EXE_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name testuserheap --kind exe $(foreach src,$(USER_TESTUSERHEAP_EXE_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_TESTUSERHEAP_DLL_ARTIFACT): tools/my-loader/ldr_build.py $(USER_TESTUSERHEAP_DLL_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name testuserheap --kind dll $(foreach src,$(USER_TESTUSERHEAP_DLL_SOURCES),--source $(src)) --entry-symbol testuserheap_entry $(USER_BUILDER_COMMON_FLAGS)

$(USER_TESTUSERHEAP2_EXE_ARTIFACT): tools/my-loader/ldr_build.py $(USER_TESTUSERHEAP2_EXE_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name testuserheap2 --kind exe $(foreach src,$(USER_TESTUSERHEAP2_EXE_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_TESTUSERHEAP2_DLL_ARTIFACT): tools/my-loader/ldr_build.py $(USER_TESTUSERHEAP2_DLL_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name testuserheap2 --kind dll $(foreach src,$(USER_TESTUSERHEAP2_DLL_SOURCES),--source $(src)) --entry-symbol testuserheap2_entry $(USER_BUILDER_COMMON_FLAGS)

$(USER_FILEMAPPING_READER_ARTIFACT): tools/my-loader/ldr_build.py $(USER_FILEMAPPING_READER_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name filemapping_reader --kind exe $(foreach src,$(USER_FILEMAPPING_READER_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_FILEMAPPING_WRITER_ARTIFACT): tools/my-loader/ldr_build.py $(USER_FILEMAPPING_WRITER_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name filemapping_writer --kind exe $(foreach src,$(USER_FILEMAPPING_WRITER_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_IRQHANDOFF_ARTIFACT): tools/my-loader/ldr_build.py $(USER_IRQHANDOFF_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name irqhandoff --kind exe $(foreach src,$(USER_IRQHANDOFF_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

$(USER_RCMAN_ARTIFACT): tools/my-loader/ldr_build.py $(USER_RCMAN_SOURCES)
	@mkdir -p $(USER_OUTPUT_ROOT)
	@$(USER_BUILDER) --name rcman --kind exe $(foreach src,$(USER_RCMAN_SOURCES),--source $(src)) --entry-symbol _start $(USER_BUILDER_COMMON_FLAGS)

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
	if ! mount -t msdos "$$DEV" $(FAT32_MOUNT_DIR_ABS) >/dev/null 2>&1; then \
		newfs_msdos -F 32 -O ROS -S 512 -c 1 -n 2 -v ROSROOT "$$DEV" >/dev/null; \
		mount -t msdos "$$DEV" $(FAT32_MOUNT_DIR_ABS); \
	fi; \
	cp "$(MAIN_KERNEL_IMAGE)" "$(FAT32_MOUNT_DIR_ABS)/$(MAIN_KERNEL_NAME)"; \
	mkdir -p "$(FAT32_MOUNT_DIR_ABS)/bin" "$(FAT32_MOUNT_DIR_ABS)/fonts" "$(FAT32_MOUNT_DIR_ABS)/cursors" "$(FAT32_MOUNT_DIR_ABS)/wallpapers" "$(FAT32_MOUNT_DIR_ABS)/icons" "$(FAT32_MOUNT_DIR_ABS)/lib"; \
	cp "$(USER_CORE_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/core.exe"; \
	cp "$(USER_DLLBURST_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/dllburst.exe"; \
	cp "$(USER_DLLREBASEPROBE_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/dllrebaseprobe.exe"; \
	cp "$(USER_CONSOLEDEMO_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/consoledemo.exe"; \
	cp "$(USER_DLLSMOKE_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/dllsmoke.exe"; \
	cp "$(USER_EXPLORER_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/explorer.exe"; \
	cp "$(USER_GUESAMPLE_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/guisample.exe"; \
	cp "$(USER_GWES_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/gwes.exe"; \
	cp "$(USER_TTFDEMO_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/ttfdemo.exe"; \
	cp "$(USER_IMAGEBOXDEMO_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/imageboxdemo.exe"; \
	cp "$(USER_KEVENTMON_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/kevent.exe"; \
	cp "$(USER_MULTIWIN_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/multiwin.exe"; \
	cp "$(USER_CRTSMOKE_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/crtsmoke.exe"; \
	cp "$(USER_SYSTEMTEST_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/systemtest.exe"; \
	cp "$(USER_TESTUSERHEAP_EXE_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/testuserheap.exe"; \
	cp "$(USER_TESTUSERHEAP2_EXE_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/testuserheap2.exe"; \
	cp "$(USER_FILEMAPPING_READER_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/filemapping_reader.exe"; \
	cp "$(USER_FILEMAPPING_WRITER_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/filemapping_writer.exe"; \
	cp "$(USER_IRQHANDOFF_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/irqhandoff.exe"; \
	cp "$(USER_WIDGETDEMO_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/widgetdemo.exe"; \
	cp "$(USER_RCMAN_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/rcman.exe"; \
	cp "$(USER_SAMPLEMATH_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/lib/samplemath.dll"; \
	if [ "$(DLLBURST_STAGE_COUNT)" -gt 0 ] 2>/dev/null; then \
		index=0; \
		while [ "$$index" -lt "$(DLLBURST_STAGE_COUNT)" ]; do \
			suffix=$$(printf '%03d' "$$index"); \
			cp "$(USER_SAMPLEMATH_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/lib/samplemath_$${suffix}.dll"; \
			index=$$((index + 1)); \
		done; \
	fi; \
	cp "$(USER_TESTUSERHEAP_DLL_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/lib/testuserheap.dll"; \
	cp "$(USER_TESTUSERHEAP2_DLL_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/lib/testuserheap2.dll"; \
	cp "$(USER_WINDOWKIT_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/lib/window.dll"; \
	cp "$(USER_GDI_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/lib/gdi.dll"; \
	cp "$(USER_JPEG_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/lib/jpeg.dll"; \
	cp "$(USER_CRT_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/lib/crt.dll"; \
	cp "$(USER_PNG_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/lib/png.dll"; \
	cp "$(USER_WIDGETS_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/lib/widgets.dll"; \
	for font_asset in $(APP_FONT_ASSET_DIR)/system_ui.rtf $(APP_FONT_ASSET_DIR)/system_ui.font $(APP_FONT_ASSET_DIR)/tahoma-*.rtf $(APP_FONT_ASSET_DIR)/*.ttf; do if [ -f "$$font_asset" ]; then cp "$$font_asset" "$(FAT32_MOUNT_DIR_ABS)/fonts/"; fi; done; \
	for cursor_asset in $(APP_CURSOR_ASSET_DIR)/*.cur32; do if [ -f "$$cursor_asset" ]; then cp "$$cursor_asset" "$(FAT32_MOUNT_DIR_ABS)/cursors/"; fi; done; \
	for wallpaper_asset in $(APP_WALLPAPER_ASSET_DIR)/*.jpg; do if [ -f "$$wallpaper_asset" ]; then cp "$$wallpaper_asset" "$(FAT32_MOUNT_DIR_ABS)/wallpapers/"; fi; done; \
	for icon_asset in $(APP_ASSET_DIR)/icons/*.png; do if [ -f "$$icon_asset" ]; then cp "$$icon_asset" "$(FAT32_MOUNT_DIR_ABS)/icons/"; fi; done; \
	cp "$(USER_SHELL_ARTIFACT)" "$(FAT32_MOUNT_DIR_ABS)/bin/shell.exe"; \
	printf 'lfn sample\n' > "$(FAT32_MOUNT_DIR_ABS)/Long File Name.txt"; \
	printf 'initial writable sample\n' > "$(FAT32_MOUNT_DIR_ABS)/Writable Sample.txt"; \
	umount -f $(FAT32_MOUNT_DIR_ABS); \
	hdiutil detach -force "$$DEV" >/dev/null; \
	trap - EXIT

kernel8.img: fat32-sync $(KERNEL_IMG)
	@true

run:
	@$(MAKE) kernel8.img
	@$(QEMU) -M $(QEMU_MACHINE_ARG) $(QEMU_CPU_ARGS) -smp $(QEMU_CPUS) $(QEMU_BOOT_ARGS) $(QEMU_SERIAL_ARGS) $(QEMU_DISPLAY_ARGS) $(QEMU_USB_ARGS) $(QEMU_PLATFORM_ARGS) $(QEMU_STORAGE_ARGS)

run-gfx: QEMU_GUI=1
run-gfx:
	@$(MAKE) kernel8.img
	@$(QEMU) -M $(QEMU_MACHINE_ARG) $(QEMU_CPU_ARGS) -smp $(QEMU_CPUS) $(QEMU_BOOT_ARGS) $(QEMU_SERIAL_ARGS) -display default $(QEMU_USB_ARGS) $(QEMU_PLATFORM_ARGS) $(QEMU_STORAGE_ARGS)

run-headless:
	@$(MAKE) kernel8.img
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
