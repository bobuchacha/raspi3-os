#ARMGNU ?= /Applications/ArmGNUToolchain/12.3.Rel1/aarch64-none-elf/bin/aarch64-none-elf
#ARMGNU ?= /Applications/ArmGNUToolchain/13.2.Rel1/aarch64-none-elf/bin/aarch64-none-elf
ARMGNU ?= E:\Nextcloud\raspo3b-os/toolchain/Windows/arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-aarch64-none-elf/bin/aarch64-none-elf
#ARMGNU ?= C:\Users\bobuc\Nextcloud\raspo3b-os/toolchain/Windows/arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-aarch64-none-elf/bin/aarch64-none-elf
#ARMGNU ?= aarch64-none-elf
#QEMU ?= qemu-system-aarch64
QEMU ?= d:\qemu\qemu-system-aarch64.exe
QEMU_CPUS ?= 4
QEMU_USB ?= 0
QEMU_GUI ?= 0
comma := ,

ARCH ?= aarch64
CPU ?= cortex-a53
BOARD ?= raspi3b
QEMU_MACHINE ?= raspi3b
QEMU_USB_ARGS = $(if $(filter 1,$(QEMU_USB)),-device qemu-xhci$(comma)id=xhci -device usb-kbd -device usb-mouse -device usb-tablet,)
QEMU_DISPLAY_ARGS = $(if $(filter 1,$(QEMU_GUI)),-display default,$(if $(filter 1,$(QEMU_USB)),-display default,-display none))

PLATFORM_CPPFLAGS = -DROS_BOARD_HEADER=\"platform/board/$(BOARD).h\"
PLATFORM_CPPFLAGS += -DROS_CPU_MMU_HEADER=\"platform/cpu/$(CPU)/mmu.h\"
PLATFORM_CPPFLAGS += -DROS_DEBUG_UART_HEADER=\"platform/cpu/$(CPU)/debug_uart.h\"
PLATFORM_CPPFLAGS += -DROS_QEMU_USB_ENABLED=$(QEMU_USB)

BUILD_DIR = output
SRC_DIR = kernel
USR_DIR = applications
USR_INCLUDE_DIR = $(USR_DIR)/include
USR_COMMON_DIR = $(USR_DIR)/common
USR_LIB_DIR = $(USR_DIR)/lib
USR_DLLS_DIR = $(USR_DIR)/dlls
USR_PROGRAMS_DIR = $(USR_DIR)/programs
INCLUDE_DIR = $(SRC_DIR)/include
OBJS_DIR = $(BUILD_DIR)/objs
KERNEL_ELF = $(BUILD_DIR)/kernel8.elf
KERNEL_IMG = $(BUILD_DIR)/kernel8.img

COPS = -g -Werror -nostdlib -nostartfiles -ffreestanding -fno-omit-frame-pointer -I$(INCLUDE_DIR) -I$(SRC_DIR) -mgeneral-regs-only $(PLATFORM_CPPFLAGS)
ASMOPS = -g -I$(INCLUDE_DIR) $(PLATFORM_CPPFLAGS)
USER_COPS = -g -Werror -nostdlib -nostartfiles -ffreestanding -fno-omit-frame-pointer -I$(USR_INCLUDE_DIR) -mgeneral-regs-only
USER_CXXOPS = $(USER_COPS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
USER_ASMOPS = -g -I$(USR_INCLUDE_DIR)

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

FONT_PSF_OBJ = $(OBJS_DIR)/font_psf.o
FONT_PSF_SRC = build/screenfont/font.psf
FONT_SFN_OBJ = $(OBJS_DIR)/font_sfn.o
FONT_SFN_SRC = build/screenfont/font.sfn

$(FONT_PSF_OBJ): $(FONT_PSF_SRC)
	@$(ARMGNU)-ld -r -b binary -o $(FONT_PSF_OBJ) $(FONT_PSF_SRC)

$(FONT_SFN_OBJ): $(FONT_SFN_SRC)
	@$(ARMGNU)-ld -r -b binary -o $(FONT_SFN_OBJ) $(FONT_SFN_SRC)

#C_FILES = $(wildcard $(SRC_DIR)/*.c)
SOURCE_FIND = find $(SRC_DIR) -type d -name '_*' -prune -o -type f
C_FILES = $(shell $(SOURCE_FIND) -name '*.c' -print)
CPP_FILES = $(shell $(SOURCE_FIND) -name '*.cpp' -print)
#ASM_FILES = $(wildcard $(SRC_DIR)/*.S)
ASM_FILES = $(shell $(SOURCE_FIND) -name '*.S' -print)
OBJ_FILES = $(C_FILES:$(SRC_DIR)/%.c=$(OBJS_DIR)/%_c.o)
OBJ_FILES += $(CPP_FILES:$(SRC_DIR)/%.cpp=$(OBJS_DIR)/%_cpp.o)
OBJ_FILES += $(ASM_FILES:$(SRC_DIR)/%.S=$(OBJS_DIR)/%_s.o)
OBJ_FILES += $(FONT_PSF_OBJ)
DEP_FILES = $(OBJ_FILES:%.o=%.d)
-include $(DEP_FILES)


#######################################################################################################
USER_BUILD_DIR = $(BUILD_DIR)/applications
USER_OBJS_DIR = $(USER_BUILD_DIR)/objs
USER_DLL_OBJ_DIR = $(USER_BUILD_DIR)/dll-objs
USER_PROGRAM_ELF_DIR = $(USER_BUILD_DIR)/elf
USER_PROGRAM_EXE_DIR = $(USER_BUILD_DIR)/programs
USER_DLL_ELF_DIR = $(USER_BUILD_DIR)/dll-elf
USER_DLL_DIR = $(USER_BUILD_DIR)/dll
USER_VFS_MOUNT_DIR = $(USER_BUILD_DIR)/fat32mnt
USER_VFS_MOUNT_DIR_ABS = $(abspath $(USER_VFS_MOUNT_DIR))
USER_VFS_BIN_DIR = $(USER_VFS_MOUNT_DIR)/bin
USER_VFS_LIB_DIR = $(USER_VFS_MOUNT_DIR)/lib
USER_VFS_DEV_FILE = $(USER_BUILD_DIR)/fat32.dev
USER_VFS_ATTACH = hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount -readwrite fat32.img
USER_LINKER_SCRIPT = $(USR_COMMON_DIR)/link.ld
USER_DLL_LINKER_SCRIPT = $(USR_COMMON_DIR)/dll.ld
USER_PACKER = python3 tools/pack_user_exe.py
USER_DLL_PACKER = python3 tools/pack_user_dll.py
USER_MODULE_PACKER = python3 tools/pack_kernel_module.py
USER_TEMPLATE_DIR = $(USR_DIR)/_templates
USER_SOURCE_FIND = find $(USR_DIR) -type d -name '_*' -prune -o -type f
.PHONY: all clean applications applications-vfs user user-vfs new-app new-dll new-sys new-app-pair new-user-app new-user-dll new-user-sys new-user-pair fat32-mount fat32-umount fat32-list fat32-read fat32-hexdump run debug asm dump diasm gdb kernel8.img
USER_C_FILES = $(shell $(USER_SOURCE_FIND) -name '*.c' -print)
USER_CPP_FILES = $(shell $(USER_SOURCE_FIND) -name '*.cpp' -print)
USER_ASM_FILES = $(shell $(USER_SOURCE_FIND) -name '*.S' -print)
USER_OBJ_FILES = $(USER_C_FILES:$(USR_DIR)/%.c=$(USER_OBJS_DIR)/%_c.o)
USER_OBJ_FILES += $(USER_CPP_FILES:$(USR_DIR)/%.cpp=$(USER_OBJS_DIR)/%_cpp.o)
USER_OBJ_FILES += $(USER_ASM_FILES:$(USR_DIR)/%.S=$(USER_OBJS_DIR)/%_s.o)
USER_DEP_FILES = $(USER_OBJ_FILES:%.o=%.d)
USER_PROGRAMS = $(sort $(notdir $(shell find $(USR_PROGRAMS_DIR) -mindepth 1 -maxdepth 1 -type d)))
USER_COMMON_C_FILES = $(shell find $(USR_COMMON_DIR) -type f -name '*.c')
USER_COMMON_CPP_FILES = $(shell find $(USR_COMMON_DIR) -type f -name '*.cpp')
USER_COMMON_ASM_FILES = $(shell find $(USR_COMMON_DIR) -type f -name '*.S')
USER_COMMON_OBJ_FILES = $(USER_COMMON_C_FILES:$(USR_DIR)/%.c=$(USER_OBJS_DIR)/%_c.o)
USER_COMMON_OBJ_FILES += $(USER_COMMON_CPP_FILES:$(USR_DIR)/%.cpp=$(USER_OBJS_DIR)/%_cpp.o)
USER_COMMON_OBJ_FILES += $(USER_COMMON_ASM_FILES:$(USR_DIR)/%.S=$(USER_OBJS_DIR)/%_s.o)
USER_LIB_C_FILES = $(shell find $(USR_LIB_DIR) -type f -name '*.c')
USER_LIB_CPP_FILES = $(shell find $(USR_LIB_DIR) -type f -name '*.cpp')
USER_LIB_ASM_FILES = $(shell find $(USR_LIB_DIR) -type f -name '*.S')
USER_LIB_OBJ_FILES = $(USER_LIB_C_FILES:$(USR_DIR)/%.c=$(USER_OBJS_DIR)/%_c.o)
USER_LIB_OBJ_FILES += $(USER_LIB_CPP_FILES:$(USR_DIR)/%.cpp=$(USER_OBJS_DIR)/%_cpp.o)
USER_LIB_OBJ_FILES += $(USER_LIB_ASM_FILES:$(USR_DIR)/%.S=$(USER_OBJS_DIR)/%_s.o)
USER_DLL_COPS = $(USER_COPS) -fPIC
USER_DLL_CXXOPS = $(USER_CXXOPS) -fPIC
USER_DLL_ASMOPS = $(USER_ASMOPS)
USER_DLL_DEP_FILES =
USER_DLL_COMMON_C_FILES = $(filter-out $(USR_COMMON_DIR)/startup.c,$(USER_COMMON_C_FILES))
USER_DLL_COMMON_CPP_FILES = $(filter-out $(USR_COMMON_DIR)/startup.cpp,$(USER_COMMON_CPP_FILES))
USER_DLL_COMMON_ASM_FILES = $(filter-out $(USR_COMMON_DIR)/startup.S,$(USER_COMMON_ASM_FILES))
USER_DLL_COMMON_OBJ_FILES = $(USER_DLL_COMMON_C_FILES:$(USR_DIR)/%.c=$(USER_DLL_OBJ_DIR)/%_c.o)
USER_DLL_COMMON_OBJ_FILES += $(USER_DLL_COMMON_CPP_FILES:$(USR_DIR)/%.cpp=$(USER_DLL_OBJ_DIR)/%_cpp.o)
USER_DLL_COMMON_OBJ_FILES += $(USER_DLL_COMMON_ASM_FILES:$(USR_DIR)/%.S=$(USER_DLL_OBJ_DIR)/%_s.o)
USER_DLL_LIB_C_FILES = $(filter-out $(USR_LIB_DIR)/cxx_init.c,$(USER_LIB_C_FILES))
USER_DLL_LIB_CPP_FILES = $(filter-out $(USR_LIB_DIR)/cxx_init.cpp,$(USER_LIB_CPP_FILES))
USER_DLL_LIB_ASM_FILES = $(filter-out $(USR_LIB_DIR)/cxx_init.S,$(USER_LIB_ASM_FILES))
USER_DLL_LIB_OBJ_FILES = $(USER_DLL_LIB_C_FILES:$(USR_DIR)/%.c=$(USER_DLL_OBJ_DIR)/%_c.o)
USER_DLL_LIB_OBJ_FILES += $(USER_DLL_LIB_CPP_FILES:$(USR_DIR)/%.cpp=$(USER_DLL_OBJ_DIR)/%_cpp.o)
USER_DLL_LIB_OBJ_FILES += $(USER_DLL_LIB_ASM_FILES:$(USR_DIR)/%.S=$(USER_DLL_OBJ_DIR)/%_s.o)
USER_DLL_SUPPORT_OBJ_FILES = $(USER_DLL_COMMON_OBJ_FILES) $(USER_DLL_LIB_OBJ_FILES)
USER_PROGRAM_ELFS =
USER_PROGRAM_EXES =
USER_DLL_ELFS =
USER_DLLS =
USER_SHARED_LIBRARIES = $(sort $(notdir $(shell if [ -d $(USR_DLLS_DIR) ]; then find $(USR_DLLS_DIR) -mindepth 1 -maxdepth 1 -type d; fi)))

# System extensions (.sys) support
USR_SYSTEM_DIR = $(USR_DIR)/system
USER_SYSTEM_OBJ_DIR = $(USER_BUILD_DIR)/system-objs
USER_SYSTEM_ELF_DIR = $(USER_BUILD_DIR)/system-elf
USER_SYSTEM_DIR_OUT = $(USER_BUILD_DIR)/system
USER_SYSTEMS = $(sort $(notdir $(shell if [ -d $(USR_SYSTEM_DIR) ]; then find $(USR_SYSTEM_DIR) -mindepth 1 -maxdepth 1 -type d; fi)))
USER_SYS_ELFS =
USER_SYS_PACKED =
USER_SYS_DEP_FILES =
USER_SYS_RUNTIME_PACKED = $(USER_SYS_PACKED)

MODULE_COPS = $(USER_COPS) -fPIC
MODULE_CXXOPS = $(USER_CXXOPS) -fPIC
MODULE_ASMOPS = $(USER_ASMOPS)

$(USER_OBJS_DIR)/%_c.o: $(USR_DIR)/%.c
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(USER_COPS) -MMD -c $< -o $@
$(USER_OBJS_DIR)/%_cpp.o: $(USR_DIR)/%.cpp
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-g++ $(USER_CXXOPS) -MMD -c $< -o $@
$(USER_OBJS_DIR)/%_s.o: $(USR_DIR)/%.S
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(USER_ASMOPS) -MMD -c $< -o $@

$(USER_DLL_OBJ_DIR)/%_c.o: $(USR_DIR)/%.c
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(USER_DLL_COPS) -MMD -c $< -o $@
$(USER_DLL_OBJ_DIR)/%_cpp.o: $(USR_DIR)/%.cpp
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-g++ $(USER_DLL_CXXOPS) -MMD -c $< -o $@
$(USER_DLL_OBJ_DIR)/%_s.o: $(USR_DIR)/%.S
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(USER_DLL_ASMOPS) -MMD -c $< -o $@

$(USER_SYSTEM_OBJ_DIR)/%_c.o: $(USR_SYSTEM_DIR)/%.c
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(MODULE_COPS) -MMD -c $< -o $@
$(USER_SYSTEM_OBJ_DIR)/%_cpp.o: $(USR_SYSTEM_DIR)/%.cpp
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-g++ $(MODULE_CXXOPS) -MMD -c $< -o $@
$(USER_SYSTEM_OBJ_DIR)/%_s.o: $(USR_SYSTEM_DIR)/%.S
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(MODULE_ASMOPS) -MMD -c $< -o $@

define BUILD_USER_PROGRAM
USER_$(1)_C_FILES := $$(shell find $(USR_PROGRAMS_DIR)/$(1) -type f -name '*.c')
USER_$(1)_CPP_FILES := $$(shell find $(USR_PROGRAMS_DIR)/$(1) -type f -name '*.cpp')
USER_$(1)_ASM_FILES := $$(shell find $(USR_PROGRAMS_DIR)/$(1) -type f -name '*.S')
USER_$(1)_OBJ_FILES := $$(USER_$(1)_C_FILES:$(USR_DIR)/%.c=$(USER_OBJS_DIR)/%_c.o)
USER_$(1)_OBJ_FILES += $$(USER_$(1)_CPP_FILES:$(USR_DIR)/%.cpp=$(USER_OBJS_DIR)/%_cpp.o)
USER_$(1)_OBJ_FILES += $$(USER_$(1)_ASM_FILES:$(USR_DIR)/%.S=$(USER_OBJS_DIR)/%_s.o)
USER_PROGRAM_ELFS += $(USER_PROGRAM_ELF_DIR)/$(1).elf
USER_PROGRAM_EXES += $(USER_PROGRAM_EXE_DIR)/$(1).exe

$(USER_PROGRAM_ELF_DIR)/$(1).elf: $(USER_LINKER_SCRIPT) $(USER_COMMON_OBJ_FILES) $(USER_LIB_OBJ_FILES) $$(USER_$(1)_OBJ_FILES)
	@mkdir -p $$(@D)
	@$(ARMGNU)-ld -nostdlib -T $(USER_LINKER_SCRIPT) -o $$@ $(USER_COMMON_OBJ_FILES) $(USER_LIB_OBJ_FILES) $$(USER_$(1)_OBJ_FILES) -g

$(USER_PROGRAM_EXE_DIR)/$(1).exe: $(USER_PROGRAM_ELF_DIR)/$(1).elf tools/pack_user_exe.py
	@mkdir -p $$(@D)
	@$(USER_PACKER) --input $$< --output $$@
endef

$(foreach prog,$(USER_PROGRAMS),$(eval $(call BUILD_USER_PROGRAM,$(prog))))

define BUILD_USER_DLL
USER_DLL_$(1)_C_FILES := $$(shell find $(USR_DLLS_DIR)/$(1) -type f -name '*.c')
USER_DLL_$(1)_CPP_FILES := $$(shell find $(USR_DLLS_DIR)/$(1) -type f -name '*.cpp')
USER_DLL_$(1)_ASM_FILES := $$(shell find $(USR_DLLS_DIR)/$(1) -type f -name '*.S')
USER_DLL_$(1)_OBJ_FILES := $$(USER_DLL_$(1)_C_FILES:$(USR_DIR)/%.c=$(USER_DLL_OBJ_DIR)/%_c.o)
USER_DLL_$(1)_OBJ_FILES += $$(USER_DLL_$(1)_CPP_FILES:$(USR_DIR)/%.cpp=$(USER_DLL_OBJ_DIR)/%_cpp.o)
USER_DLL_$(1)_OBJ_FILES += $$(USER_DLL_$(1)_ASM_FILES:$(USR_DIR)/%.S=$(USER_DLL_OBJ_DIR)/%_s.o)
USER_DLL_DEP_FILES += $$(USER_DLL_$(1)_OBJ_FILES:%.o=%.d)
USER_DLL_ELFS += $(USER_DLL_ELF_DIR)/$(1).elf
USER_DLLS += $(USER_DLL_DIR)/$(1).dll

$(USER_DLL_ELF_DIR)/$(1).elf: $(USER_DLL_SUPPORT_OBJ_FILES) $$(USER_DLL_$(1)_OBJ_FILES)
	@mkdir -p $$(@D)
	@$(ARMGNU)-ld -shared -o $$@ $(USER_DLL_SUPPORT_OBJ_FILES) $$(USER_DLL_$(1)_OBJ_FILES) -g

$(USER_DLL_DIR)/$(1).dll: $(USER_DLL_ELF_DIR)/$(1).elf tools/pack_user_dll.py
	@mkdir -p $$(@D)
	@$(USER_DLL_PACKER) --input $$< --output $$@
endef

$(foreach dll,$(USER_SHARED_LIBRARIES),$(eval $(call BUILD_USER_DLL,$(dll))))

define BUILD_USER_SYS
USER_SYS_$(1)_C_FILES := $$(shell find $(USR_SYSTEM_DIR)/$(1) -type f -name '*.c')
USER_SYS_$(1)_CPP_FILES := $$(shell find $(USR_SYSTEM_DIR)/$(1) -type f -name '*.cpp')
USER_SYS_$(1)_ASM_FILES := $$(shell find $(USR_SYSTEM_DIR)/$(1) -type f -name '*.S')
USER_SYS_$(1)_OBJ_FILES := $$(USER_SYS_$(1)_C_FILES:$(USR_SYSTEM_DIR)/%.c=$(USER_SYSTEM_OBJ_DIR)/%_c.o)
USER_SYS_$(1)_OBJ_FILES += $$(USER_SYS_$(1)_CPP_FILES:$(USR_SYSTEM_DIR)/%.cpp=$(USER_SYSTEM_OBJ_DIR)/%_cpp.o)
USER_SYS_$(1)_OBJ_FILES += $$(USER_SYS_$(1)_ASM_FILES:$(USR_SYSTEM_DIR)/%.S=$(USER_SYSTEM_OBJ_DIR)/%_s.o)
USER_SYS_DEP_FILES += $$(USER_SYS_$(1)_OBJ_FILES:%.o=%.d)
USER_SYS_ELFS += $(USER_SYSTEM_ELF_DIR)/$(1).elf
USER_SYS_PACKED += $(USER_SYSTEM_DIR_OUT)/$(1).sys

$(USER_SYSTEM_ELF_DIR)/$(1).elf: $$(USER_SYS_$(1)_OBJ_FILES)
	@mkdir -p $$(@D)
	@$(ARMGNU)-ld -shared -o $$@ $$(USER_SYS_$(1)_OBJ_FILES) -g

$(USER_SYSTEM_DIR_OUT)/$(1).sys: $(USER_SYSTEM_ELF_DIR)/$(1).elf $(wildcard $(USR_SYSTEM_DIR)/$(1)/module.json) tools/pack_kernel_module.py
	@mkdir -p $$(@D)
	@if [ -f "$(USR_SYSTEM_DIR)/$(1)/module.json" ]; then \
		$(USER_MODULE_PACKER) --input $$< --manifest $(USR_SYSTEM_DIR)/$(1)/module.json --output $$@; \
	else \
		$(USER_MODULE_PACKER) --input $$< --output $$@; \
	fi
endef

$(foreach sys,$(USER_SYSTEMS),$(eval $(call BUILD_USER_SYS,$(sys))))

-include $(USER_DEP_FILES)
-include $(USER_DLL_DEP_FILES)
-include $(USER_SYS_DEP_FILES)

applications: $(USER_PROGRAM_EXES) $(USER_DLLS) $(USER_SYS_PACKED)

user: applications

new-app: new-user-app

new-dll: new-user-dll

new-sys: new-user-sys

new-app-pair: new-user-pair

new-user-app:
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
	if [ -z "$$LANG_CHOICE" ]; then \
		case "$(LANG)" in \
			c|cpp|c++) LANG_CHOICE="$(LANG)" ;; \
			*) LANG_CHOICE=c ;; \
		esac; \
	fi; \
	case "$$LANG_CHOICE" in \
		c) TEMPLATE="$(USER_TEMPLATE_DIR)/program-c/main.c"; DEST="$(USR_PROGRAMS_DIR)/$(NAME)/main.c" ;; \
		cpp|c++) TEMPLATE="$(USER_TEMPLATE_DIR)/program-cpp/main.cpp"; DEST="$(USR_PROGRAMS_DIR)/$(NAME)/main.cpp" ;; \
		*) echo "APP_LANG must be c or cpp"; exit 1 ;; \
	esac; \
	if [ -e "$(USR_PROGRAMS_DIR)/$(NAME)" ]; then \
		echo "Program already exists: $(USR_PROGRAMS_DIR)/$(NAME)"; \
		exit 1; \
	fi; \
	mkdir -p "$(USR_PROGRAMS_DIR)/$(NAME)"; \
	sed -e 's/__APP_NAME__/$(NAME)/g' "$$TEMPLATE" > "$$DEST"; \
	echo "Created $$DEST"

new-user-dll:
	@set -e; \
	if [ -z "$(NAME)" ]; then \
		echo "Usage: make new-dll NAME=<dll_name>"; \
		exit 1; \
	fi; \
	if ! printf '%s' "$(NAME)" | grep -Eq '^[A-Za-z_][A-Za-z0-9_]*$$'; then \
		echo "NAME must be a C identifier: letters, digits, and underscores only, not starting with a digit"; \
		exit 1; \
	fi; \
	DLL_NAME_UPPER=$$(printf '%s' "$(NAME)" | tr '[:lower:]' '[:upper:]'); \
	if [ -e "$(USR_DLLS_DIR)/$(NAME)" ] || [ -e "$(USR_INCLUDE_DIR)/app/$(NAME).h" ]; then \
		echo "DLL already exists: $(NAME)"; \
		exit 1; \
	fi; \
	mkdir -p "$(USR_DLLS_DIR)/$(NAME)" "$(USR_INCLUDE_DIR)/app"; \
	sed \
		-e 's/__DLL_NAME__/$(NAME)/g' \
		-e "s/__DLL_NAME_UPPER__/$$DLL_NAME_UPPER/g" \
		"$(USER_TEMPLATE_DIR)/dll/include/app/template-dll.h" > "$(USR_INCLUDE_DIR)/app/$(NAME).h"; \
	sed \
		-e 's/__DLL_NAME__/$(NAME)/g' \
		-e "s/__DLL_NAME_UPPER__/$$DLL_NAME_UPPER/g" \
		"$(USER_TEMPLATE_DIR)/dll/main.c" > "$(USR_DLLS_DIR)/$(NAME)/main.c"; \
	echo "Created $(USR_INCLUDE_DIR)/app/$(NAME).h"; \
	echo "Created $(USR_DLLS_DIR)/$(NAME)/main.c"

new-user-sys:
	@set -e; \
	if [ -z "$(NAME)" ]; then \
		echo "Usage: make new-sys NAME=<sys_name>"; \
		exit 1; \
	fi; \
	if ! printf '%s' "$(NAME)" | grep -Eq '^[A-Za-z_][A-Za-z0-9_]*$$'; then \
		echo "NAME must be a C identifier: letters, digits, and underscores only, not starting with a digit"; \
		exit 1; \
	fi; \
	if [ -e "$(USR_SYSTEM_DIR)/$(NAME)" ]; then \
		echo "System extension already exists: $(USR_SYSTEM_DIR)/$(NAME)"; \
		exit 1; \
	fi; \
	mkdir -p "$(USR_SYSTEM_DIR)/$(NAME)"; \
	sed -e 's/__SYS_NAME__/$(NAME)/g' "$(USER_TEMPLATE_DIR)/system/module.c" > "$(USR_SYSTEM_DIR)/$(NAME)/module.c"; \
	sed -e 's/__SYS_NAME__/$(NAME)/g' "$(USER_TEMPLATE_DIR)/system/module.json" > "$(USR_SYSTEM_DIR)/$(NAME)/module.json"; \
	echo "Created $(USR_SYSTEM_DIR)/$(NAME)/module.c"; \
	echo "Created $(USR_SYSTEM_DIR)/$(NAME)/module.json"

new-user-pair:
	@set -e; \
	if [ -z "$(APP_NAME)" ] || [ -z "$(DLL_NAME)" ]; then \
		echo "Usage: make new-app-pair APP_NAME=<app_name> DLL_NAME=<dll_name>"; \
		exit 1; \
	fi; \
	if ! printf '%s' "$(APP_NAME)" | grep -Eq '^[A-Za-z_][A-Za-z0-9_]*$$'; then \
		echo "APP_NAME must be a C identifier: letters, digits, and underscores only, not starting with a digit"; \
		exit 1; \
	fi; \
	if ! printf '%s' "$(DLL_NAME)" | grep -Eq '^[A-Za-z_][A-Za-z0-9_]*$$'; then \
		echo "DLL_NAME must be a C identifier: letters, digits, and underscores only, not starting with a digit"; \
		exit 1; \
	fi; \
	DLL_NAME_UPPER=$$(printf '%s' "$(DLL_NAME)" | tr '[:lower:]' '[:upper:]'); \
	if [ -e "$(USR_PROGRAMS_DIR)/$(APP_NAME)" ] || [ -e "$(USR_DLLS_DIR)/$(DLL_NAME)" ] || [ -e "$(USR_INCLUDE_DIR)/app/$(DLL_NAME).h" ]; then \
		echo "Paired scaffold target already exists"; \
		exit 1; \
	fi; \
	mkdir -p "$(USR_PROGRAMS_DIR)/$(APP_NAME)" "$(USR_DLLS_DIR)/$(DLL_NAME)" "$(USR_INCLUDE_DIR)/app"; \
	sed \
		-e 's/__APP_NAME__/$(APP_NAME)/g' \
		-e 's/__DLL_NAME__/$(DLL_NAME)/g' \
		-e "s/__DLL_NAME_UPPER__/$$DLL_NAME_UPPER/g" \
		"$(USER_TEMPLATE_DIR)/paired/app/main.c" > "$(USR_PROGRAMS_DIR)/$(APP_NAME)/main.c"; \
	sed \
		-e 's/__DLL_NAME__/$(DLL_NAME)/g' \
		-e "s/__DLL_NAME_UPPER__/$$DLL_NAME_UPPER/g" \
		"$(USER_TEMPLATE_DIR)/paired/dll/include/app/template-dll.h" > "$(USR_INCLUDE_DIR)/app/$(DLL_NAME).h"; \
	sed \
		-e 's/__APP_NAME__/$(APP_NAME)/g' \
		-e 's/__DLL_NAME__/$(DLL_NAME)/g' \
		-e "s/__DLL_NAME_UPPER__/$$DLL_NAME_UPPER/g" \
		"$(USER_TEMPLATE_DIR)/paired/dll/main.c" > "$(USR_DLLS_DIR)/$(DLL_NAME)/main.c"; \
	echo "Created $(USR_PROGRAMS_DIR)/$(APP_NAME)/main.c"; \
	echo "Created $(USR_INCLUDE_DIR)/app/$(DLL_NAME).h"; \
	echo "Created $(USR_DLLS_DIR)/$(DLL_NAME)/main.c"

applications-vfs: applications
	@set -e; \
	if mount | grep -q " on $(USER_VFS_MOUNT_DIR_ABS) "; then umount $(USER_VFS_MOUNT_DIR_ABS) >/dev/null 2>&1 || true; fi; \
	if [ -f $(USER_VFS_DEV_FILE) ]; then hdiutil detach "$$(cat $(USER_VFS_DEV_FILE))" >/dev/null 2>&1 || true; rm -f $(USER_VFS_DEV_FILE); fi; \
	mkdir -p $(USER_VFS_MOUNT_DIR_ABS); \
	DEV=$$($(USER_VFS_ATTACH) | awk 'NR==1 { print $$1 }'); \
	trap 'umount $(USER_VFS_MOUNT_DIR_ABS) >/dev/null 2>&1 || true; hdiutil detach "'"'$$DEV'"'" >/dev/null 2>&1 || true' EXIT; \
	mount -t msdos "$$DEV" $(USER_VFS_MOUNT_DIR_ABS); \
	mkdir -p $(USER_VFS_BIN_DIR); \
	mkdir -p $(USER_VFS_LIB_DIR); \
	mkdir -p $(USER_VFS_MOUNT_DIR)/system; \
	rm -f $(USER_VFS_MOUNT_DIR)/user.exe; \
	find $(USER_VFS_BIN_DIR) -maxdepth 1 -type f \( -name '*.elf' -o -name '*.exe' \) -delete; \
	find $(USER_VFS_LIB_DIR) -maxdepth 1 -type f -name '*.dll' -delete; \
	find $(USER_VFS_MOUNT_DIR)/system -maxdepth 1 -type f -name '*.sys' -delete; \
	for exe in $(USER_PROGRAM_EXES); do cp "$${exe}" $(USER_VFS_BIN_DIR)/$$(basename "$$exe"); done; \
	for dll in $(USER_DLLS); do cp "$${dll}" $(USER_VFS_LIB_DIR)/$$(basename "$$dll"); done; \
	for sys in $(USER_SYS_RUNTIME_PACKED); do cp "$${sys}" $(USER_VFS_MOUNT_DIR)/system/$$(basename "$$sys"); done; \
	echo "FAT32 image contents after sync:"; \
	find $(USER_VFS_MOUNT_DIR_ABS) -mindepth 1 -maxdepth 2 -print | sed 's#^$(USER_VFS_MOUNT_DIR_ABS)##' | LC_ALL=C sort; \
	umount $(USER_VFS_MOUNT_DIR_ABS); \
	hdiutil detach "$$DEV" >/dev/null; \
	trap - EXIT

user-vfs: applications-vfs

fat32-list:
	@set -e; \
	if mount | grep -q " on $(USER_VFS_MOUNT_DIR_ABS) "; then umount $(USER_VFS_MOUNT_DIR_ABS) >/dev/null 2>&1 || true; fi; \
	if [ -f $(USER_VFS_DEV_FILE) ]; then hdiutil detach "$$(cat $(USER_VFS_DEV_FILE))" >/dev/null 2>&1 || true; rm -f $(USER_VFS_DEV_FILE); fi; \
	mkdir -p $(USER_VFS_MOUNT_DIR_ABS); \
	DEV=$$($(USER_VFS_ATTACH) | awk 'NR==1 { print $$1 }'); \
	trap 'umount $(USER_VFS_MOUNT_DIR_ABS) >/dev/null 2>&1 || true; hdiutil detach "'"'$$DEV'"'" >/dev/null 2>&1 || true' EXIT; \
	mount -t msdos "$$DEV" $(USER_VFS_MOUNT_DIR_ABS); \
	echo "FAT32 image contents:"; \
	find $(USER_VFS_MOUNT_DIR_ABS) -mindepth 1 -maxdepth 2 -print | sed 's#^$(USER_VFS_MOUNT_DIR_ABS)##' | LC_ALL=C sort; \
	umount $(USER_VFS_MOUNT_DIR_ABS); \
	hdiutil detach "$$DEV" >/dev/null; \
	trap - EXIT

fat32-mount:
	@set -e; \
	mkdir -p $(USER_VFS_MOUNT_DIR_ABS); \
	if mount | grep -q " on $(USER_VFS_MOUNT_DIR_ABS) "; then \
		echo "fat32.img is already mounted at $(USER_VFS_MOUNT_DIR_ABS)"; \
		exit 0; \
	fi; \
	DEV=$$($(USER_VFS_ATTACH) | awk 'NR==1 { print $$1 }'); \
	echo "$$DEV" > $(USER_VFS_DEV_FILE); \
	mount -t msdos "$$DEV" $(USER_VFS_MOUNT_DIR_ABS); \
	echo "Mounted fat32.img at $(USER_VFS_MOUNT_DIR_ABS) using $$DEV"

fat32-umount:
	@set -e; \
	if mount | grep -q " on $(USER_VFS_MOUNT_DIR_ABS) "; then \
		umount $(USER_VFS_MOUNT_DIR_ABS); \
	fi; \
	if [ -f $(USER_VFS_DEV_FILE) ]; then \
		DEV=$$(cat $(USER_VFS_DEV_FILE)); \
		hdiutil detach "$$DEV" >/dev/null 2>&1 || true; \
		rm -f $(USER_VFS_DEV_FILE); \
		echo "Detached $$DEV"; \
	else \
		echo "No recorded FAT32 device to detach"; \
	fi

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

#######################################################################################################


$(KERNEL_IMG): $(SRC_DIR)/link.ld $(OBJ_FILES)
	@mkdir -p $(BUILD_DIR)
	@$(ARMGNU)-ld -nostdlib -T $(SRC_DIR)/link.ld -o $(KERNEL_ELF) $(OBJ_FILES) -g
	@$(ARMGNU)-objcopy $(KERNEL_ELF) -O binary $(KERNEL_IMG)

kernel8.img: applications-vfs $(KERNEL_IMG)
	@true
dump: all
	@$(ARMGNU)-objdump --all-headers $(KERNEL_ELF)
diasm: all
	@$(ARMGNU)-objdump --all-headers $(KERNEL_ELF)
run: QEMU_GUI=1
run: all
	@echo "Running: --------------------------------------------------------------------------------- "
	@$(QEMU) -M $(QEMU_MACHINE) -smp $(QEMU_CPUS) -kernel $(KERNEL_IMG) -serial stdio -s $(QEMU_DISPLAY_ARGS) $(QEMU_USB_ARGS) -drive file=fat32.img,if=sd,format=raw
run-gfx: QEMU_GUI=1
run-gfx: all
	@echo "Running with framebuffer display --------------------------------------------------------- "
	@$(QEMU) -M $(QEMU_MACHINE) -smp $(QEMU_CPUS) -kernel $(KERNEL_IMG) -serial stdio -s -display default $(QEMU_USB_ARGS) -drive file=fat32.img,if=sd,format=raw
run-headless: all
	@echo "Running headless ------------------------------------------------------------------------- "
	@$(QEMU) -M $(QEMU_MACHINE) -smp $(QEMU_CPUS) -kernel $(KERNEL_IMG) -serial stdio -s -display none -drive file=fat32.img,if=sd,format=raw
debug: all
	@echo "QEMU starting. Remember to start gdb------------------------------------------------------ "
	# @$(QEMU) -M $(QEMU_MACHINE) -kernel $(KERNEL_IMG) -serial null -serial stdio -display none -s -S -d trace:bcm2835_systmr*
	@$(QEMU) -M $(QEMU_MACHINE) -smp $(QEMU_CPUS) -kernel $(KERNEL_IMG) -serial stdio -display none -s -S

asm: all
	@echo "Running: --------------------------------------------------------------------------------- "
	@$(QEMU) -M $(QEMU_MACHINE) -smp $(QEMU_CPUS) -kernel $(KERNEL_IMG) -serial null -d in_asm -monitor stdio -nographic -S -gdb tcp::1234
gdb:
	gdb -ex 'file $(KERNEL_ELF)' -ex 'set arch aarch64' -ex 'target remote localhost:1234' -ex 'layout split' -ex 'layout regs' -ex 'b _start' -ex 'b breakpoint' -q --nh
