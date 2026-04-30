CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -g
INCLUDES = -Iinclude
BUILD_DIR = build

LIB_SOURCES = \
	src/gwe_context.c \
	src/gwe_window.c \
	src/gwe_message.c \
	src/gwe_input.c \
	src/gwe_graphics.c \
	src/gwe_debug.c

TEST_SOURCES = \
	tests/smoke.c

LIB_OBJECTS = $(LIB_SOURCES:%.c=$(BUILD_DIR)/%.o)
TEST_OBJECTS = $(TEST_SOURCES:%.c=$(BUILD_DIR)/%.o)
TARGET = $(BUILD_DIR)/tests/smoke

all: $(TARGET)

test: $(TARGET)
	./$(TARGET)

$(TARGET): $(LIB_OBJECTS) $(TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LIB_OBJECTS) $(TEST_OBJECTS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all test clean