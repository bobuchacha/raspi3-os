# Don't use normal gcc, use the arm cross compiler

#---------------------------

CC = aarch64-none-elf-gcc
LD = aarch64-none-elf-ld
QEMU = qemu-system-aarch64


SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

# Location of the files
KER_SRC = ../src/kernel
KER_HEAD = ../include
COMMON_SRC = ../src/common
OBJ_DIR = objects
KERSOURCES = $(wildcard $(KER_SRC)/*.c)
COMMONSOURCES = $(wildcard $(COMMON_SRC)/*.c)
ASMSOURCES = $(wildcard $(KER_SRC)/*.S)
OBJECTS = $(patsubst $(KER_SRC)/%.c, $(OBJ_DIR)/%.o, $(KERSOURCES))
OBJECTS += $(patsubst $(COMMON_SRC)/%.c, $(OBJ_DIR)/%.o, $(COMMONSOURCES))
OBJECTS += $(patsubst $(KER_SRC)/%.S, $(OBJ_DIR)/%.o, $(ASMSOURCES))
HEADERS = $(wildcard $(KER_HEAD)/*.h)

IMG_NAME=kernel8.img

CFLAGS = -Wall \
	-O2 \
	-ffreestanding \
	-fno-stack-protector \
	-nostdinc \
	-nostdlib \
	-nostartfiles

LFLAGS = -nostdlib -nostartfiles

QEMUGFLAGS = -M raspi3b \
	-kernel $(IMG_NAME) \
	-serial stdio  \
	-drive file=test.dd,if=sd,format=raw

#modified 10/01/23 - added rd.o init ramdisk
all: $(OBJECTS) $(HEADERS) 
	@echo Building kernel...
	$(CC) -T linker.ld -o $(IMG_NAME) $(LFLAGS) $(OBJECTS) rd.o

$(OBJ_DIR)/%.o: $(KER_SRC)/%.c
	@echo --> Compiling $<
	mkdir -p $(@D)
	@$(CC) $(CFLAGS) -I$(KER_SRC) -I$(KER_HEAD) -c $< -o $@ $(CSRCFLAGS)

$(OBJ_DIR)/%.o: $(KER_SRC)/%.S
	@echo --> Compiling $<
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) -I$(KER_SRC) -I$(KER_HEAD) -c $< -o $@

$(OBJ_DIR)/%.o: $(COMMON_SRC)/%.c
	@echo --> Compiling $<
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) -I$(KER_SRC) -I$(KER_HEAD) -c $< -o $@ $(CSRCFLAGS)


#modified 10/01/23 - make ram disk
tar:
	@tar -cf ramdisk ./ramdisk_content

#modified 10/01/23 - this is co cpio ramdisk
cpio:
	@ls *.md *.c *.h | cpio -H hpodc -o >ramdisk


#modified 10/01/23 - added rd.o init ramdisk
rd.o: ramdisk
	@$(LD) -r -b binary -o rd.o ramdisk

#modified 10/01/23 - added for font
font_psf.o: font.psf
	@$(LD) -r -b binary -o $(OBJ_DIR)/font_psf.o font.psf

font_sfn.o: font.sfn
	@$(LD) -r -b binary -o $(OBJ_DIR)/font_sfn.o font.sfn


clean:
	@echo Cleaning the build folder
	@rm -rf $(OBJ_DIR) >/dev/null 2>/dev/null || true
	@rm $(IMG_NAME) >/dev/null 2>/dev/null || true
	
run:
	@echo Starting QEMU...
	@echo --------------------------------------------------
	$(QEMU) $(QEMUGFLAGS)
	