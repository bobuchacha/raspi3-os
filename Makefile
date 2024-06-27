#ARMGNU ?= /Applications/ArmGNUToolchain/12.3.Rel1/aarch64-none-elf/bin/aarch64-none-elf
#ARMGNU ?= /Applications/ArmGNUToolchain/13.2.Rel1/aarch64-none-elf/bin/aarch64-none-elf
#ARMGNU ?= E:\Nextcloud\raspo3b-os/toolchain/Windows/arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-aarch64-none-elf/bin/aarch64-none-elf
ARMGNU ?= C:\Users\bobuc\Nextcloud\raspo3b-os/toolchain/Windows/arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-aarch64-none-elf/bin/aarch64-none-elf
#ARMGNU ?= aarch64-none-elf
#QEMU ?= qemu-system-aarch64
 QEMU = c:\qemu\qemu-system-aarch64.exe

BUILD_DIR = build
SRC_DIR = src
USR_SRC_DIR = user
INCLUDE_DIR = include
OBJS_DIR = build/objs

COPS = -g -Werror -nostdlib -nostartfiles -ffreestanding -Iinclude -Isrc -Isrc/include -mgeneral-regs-only
ASMOPS = -g -Iinclude

all: kernel8.img

clean:
	@echo "Cleaning build system"
	@rm -rf $(OBJS_DIR)
	@mkdir -p $(OBJS_DIR)
	@rm $(BUILD_DIR)/kernel8.elf $(BUILD_DIR)/*.o $(BUILD_DIR)/*.img >/dev/null 2>/dev/null || true

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

font_psf.o: $(BUILD_DIR)/screenfont/font.psf
	@$(ARMGNU)-ld -r -b binary -o $(OBJS_DIR)/font_psf.o $(BUILD_DIR)/screenfont/font.psf

font_sfn.o: $(BUILD_DIR)/screenfont/font.sfn
	@$(ARMGNU)-ld -r -b binary -o $(OBJS_DIR)/font_sfn.o $(BUILD_DIR)/screenfont/font.sfn

#C_FILES = $(wildcard $(SRC_DIR)/*.c)
C_FILES = $(shell find $(SRC_DIR) -type f -name '*.c')
CPP_FILES = $(shell find $(SRC_DIR) -type f -name '*.cpp')
#ASM_FILES = $(wildcard $(SRC_DIR)/*.S)
ASM_FILES = $(shell find $(SRC_DIR) -type f -name '*.S')
OBJ_FILES = $(C_FILES:$(SRC_DIR)/%.c=$(OBJS_DIR)/%_c.o)
OBJ_FILES += $(CPP_FILES:$(SRC_DIR)/%.cpp=$(OBJS_DIR)/%_cpp.o)
OBJ_FILES += $(ASM_FILES:$(SRC_DIR)/%.S=$(OBJS_DIR)/%_s.o)
DEP_FILES = $(OBJ_FILES:%.o=%.d)
-include $(DEP_FILES)


#######################################################################################################
USER_C_FILES = $(shell find $(USR_SRC_DIR) -type f -name '*.c')
USER_ASM_FILES = $(shell find $(USR_SRC_DIR) -type f -name '*.S')
USER_OBJ_FILES = $(USER_C_FILES:$(USR_SRC_DIR)/%.c=$(USER_OBJS_DIR)/%_c.o)
USER_OBJ_FILES += $(USER_ASM_FILES:$(USR_SRC_DIR)/%.S=$(USER_OBJS_DIR)/%_s.o)
USER_OBJS_DIR = build/user/objs

$(USER_OBJS_DIR)/%_c.o: $(USR_SRC_DIR)/%.c
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(ARMGNU)-gcc $(COPS) -MMD -c $< -o $@
$(USER_OBJS_DIR)/%_s.o: $(USR_SRC_DIR)/%.S
	@echo "-> $@..."
	@$(ARMGNU)-gcc $(ASMOPS) -MMD -c $< -o $@
user:  $(USR_SRC_DIR)/link.ld $(USER_OBJ_FILES)
	@$(ARMGNU)-ld -nostdlib -T $(USR_SRC_DIR)/link.ld -o $(BUILD_DIR)/user.elf  $(USER_OBJ_FILES) -g
#######################################################################################################

f32.disk:
	-rm f32.disk
	dd if=/dev/zero of=f32.disk bs=1M count=64
	mkfs.fat -F32 f32.disk -s 1

mount_disk: f32.disk
	mkdir -p fat32
	sudo hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount f32.disk

populate_disk: mount_disk
	sudo cp *.c *.h fat32
	sudo cp -R deps fat32/
	sudo mkdir -p fat32/foo/bar/baz/boo/dep/doo/poo/goo/
	sudo cp common.h fat32/foo/bar/baz/boo/dep/doo/poo/goo/tood.txt
	sleep 1
	sudo umount fat32
	-@rm -Rf fat32

#######################################################################################################


kernel8.img: $(SRC_DIR)/link.ld $(OBJ_FILES)
	@$(ARMGNU)-ld -nostdlib -T $(SRC_DIR)/link.ld -o $(BUILD_DIR)/kernel8.elf  $(OBJ_FILES) -g
	@$(ARMGNU)-objcopy $(BUILD_DIR)/kernel8.elf -O binary kernel8.img
dump: all
	@$(ARMGNU)-objdump --all-headers $(BUILD_DIR)/kernel8.elf
diasm: all
	@$(ARMGNU)-objdump --all-headers $(BUILD_DIR)/kernel8.elf
run: all
	@echo "Running: --------------------------------------------------------------------------------- "
	@$(QEMU) -M raspi3b -kernel kernel8.img  -serial stdio -s -display none -drive file=fat32.img,if=sd,format=raw
debug: all
	@echo "QEMU starting. Remember to start gdb------------------------------------------------------ "
	# @$(QEMU) -M raspi3b -kernel kernel8.img -serial null -serial stdio -display none -s -S -d trace:bcm2835_systmr*
	@$(QEMU) -M raspi3b -kernel kernel8.img -serial stdio -display none -s -S

asm: all
	@echo "Running: --------------------------------------------------------------------------------- "
	@$(QEMU) -M raspi3b -kernel kernel8.img -serial null -d in_asm -monitor stdio -nographic -S -gdb tcp::1234
gdb:
	gdb -ex 'file build/kernel8.elf'  -ex 'set arch aarch64' -ex 'target remote localhost:1234' -ex 'layout split' -ex 'layout regs' -ex 'b _start' -ex 'b breakpoint' -q --nh