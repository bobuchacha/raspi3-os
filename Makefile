#ARMGNU ?= /Applications/ArmGNUToolchain/12.3.Rel1/aarch64-none-elf/bin/aarch64-none-elf
ARMGNU ?= E:\Nextcloud\raspo3b-os/toolchain/Windows/arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-aarch64-none-elf/bin/aarch64-none-elf
#ARMGNU ?= aarch64-none-elf
#QEMU ?= qemu-system-aarch64
QEMU = d:\qemu\qemu-system-aarch64.exe

BUILD_DIR = build
SRC_DIR = src
INCLUDE_DIR = include
OBJS_DIR = build/objs

COPS = -g -Wall -nostdlib -nostartfiles -ffreestanding -Iinclude -Isrc -mgeneral-regs-only
ASMOPS = -g -Iinclude

all: kernel8.img

clean:
	@echo "Cleaning build system"
	@rm -rf $(OBJS_DIR)
	@rm $(BUILD_DIR)/kernel8.elf $(BUILD_DIR)/*.o $(BUILD_DIR)/*.img >/dev/null 2>/dev/null || true

$(OBJS_DIR)/%_c.o: $(SRC_DIR)/%.c
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
#ASM_FILES = $(wildcard $(SRC_DIR)/*.S)
ASM_FILES = $(shell find $(SRC_DIR) -type f -name '*.S')
OBJ_FILES = $(C_FILES:$(SRC_DIR)/%.c=$(OBJS_DIR)/%_c.o)
OBJ_FILES += $(ASM_FILES:$(SRC_DIR)/%.S=$(OBJS_DIR)/%_s.o)
DEP_FILES = $(OBJ_FILES:%.o=%.d)
-include $(DEP_FILES)


kernel8.img: $(SRC_DIR)/link.ld $(OBJ_FILES)
	@$(ARMGNU)-ld -nostdlib -T $(SRC_DIR)/link.ld -o $(BUILD_DIR)/kernel8.elf  $(OBJ_FILES)
	@$(ARMGNU)-objcopy $(BUILD_DIR)/kernel8.elf -O binary kernel8.img

dump:
	@$(ARMGNU)-objdump --all-header $(BUILD_DIR)/kernel8.elf >> kernel8.txt

run: all
	@echo "Running: --------------------------------------------------------------------------------- "
	@$(QEMU) -M raspi3b -kernel kernel8.img -serial stdio -display none -m 1024M -s
debug:
	@echo "Running: --------------------------------------------------------------------------------- "
	@$(QEMU) -M raspi3b -kernel kernel8.img -serial stdio -display none -m 1024M -s -S
asm: all
	@echo "Running: --------------------------------------------------------------------------------- "
	@$(QEMU) -M raspi3b -kernel kernel8.img -serial null -d in_asm -m 1024M -s -S -nographic -monitor stdio

gdb:
	gdb-multiarch -ex 'file build/kernel8.elf' -ex 'set arch aarch64' -ex 'target remote localhost:1234' -ex 'layout split' -ex 'layout regs' -ex 'b _start' -q --nh

