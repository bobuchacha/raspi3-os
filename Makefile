#
# Copyright (C) 2018 bzt (bztsrc@github)
#
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation
# files (the "Software"), to deal in the Software without
# restriction, including without limitation the rights to use, copy,
# modify, merge, publish, distribute, sublicense, and/or sell copies
# of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
# HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#
#

OBJ_FOLDER = objs
OUT_FOLDER = out
SRCS = $(shell find kernel -type f -name '*.c') #$(wildcard *.c)
OBJS = $(addprefix $(OBJ_FOLDER)/, $(SRCS:.c=.o))
INC_FLAGS=-Ikernel -Iinclude
CFLAGS = -Wall -O2 -ffreestanding -nostdinc -nostdlib -nostartfiles -mstrict-align $(INC_FLAGS)

#TOOLCHAIN = /Applications/ArmGNUToolchain/12.3.rel1/aarch64-none-elf/bin/aarch64-none-elf
TOOLCHAIN = /Applications/ArmGNUToolchain/13.2.Rel1/aarch64-none-elf/bin/aarch64-none-elf
QEMU = qemu-system-aarch64
TOOLCHAIN = C:\Users/bobuc/Nextcloud/raspo3b-os/toolchain/Windows/arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-aarch64-none-elf/bin/aarch64-none-elf
QEMU = c:\qemu\qemu-system-aarch64.exe
GCC = $(TOOLCHAIN)-gcc
LD = $(TOOLCHAIN)-ld
OBJ_COPY = $(TOOLCHAIN)-objcopy

all: clean kernel8.img

start.o: kernel/start.S
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(GCC) $(CFLAGS) -c kernel/start.S -o $(OBJ_FOLDER)/start.o

font_psf.o: screenfont/font.psf
	@$(LD) -r -b binary -o $(OBJ_FOLDER)/font_psf.o screenfont/font.psf

font_sfn.o: screenfont/font.sfn
	@$(LD) -r -b binary -o $(OBJ_FOLDER)/font_sfn.o screenfont/font.sfn

$(OBJ_FOLDER)/%.o: %.S
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(GCC) $(CFLAGS) -c $< -o $@

$(OBJ_FOLDER)/%.o: %.c
	@echo "-> $@..."
	@mkdir -p $(@D)
	@$(GCC) $(CFLAGS) $(INC_FLAGS) -c $< -o $@

kernel8.img: start.o font_psf.o font_sfn.o $(OBJS)
	@echo "Building kernel8.img..."
	@mkdir -p $(OUT_FOLDER)
	@echo $(OBJS)
	@$(LD) -nostdlib $(OBJ_FOLDER)/start.o $(OBJ_FOLDER)/font_psf.o $(OBJ_FOLDER)/font_sfn.o $(OBJS) -T link.ld -o $(OUT_FOLDER)/kernel8.elf
	@$(OBJ_COPY) -O binary $(OUT_FOLDER)/kernel8.elf $(OUT_FOLDER)/kernel8.img

clean:
	@echo "Cleaning build system"
	@rm -rf objs/kernel
	@rm $(OUT_FOLDER)/kernel8.elf *.o >/dev/null 2>/dev/null || true

run: kernel8.img
	@echo Running.....
	@echo ------------------------------------------------------------------------------------------
	@echo
	@echo
	@$(QEMU) -M raspi3b -kernel $(OUT_FOLDER)/kernel8.img -drive file=fat32.img,if=sd,format=raw -serial stdio -d int -display none
