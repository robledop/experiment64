# Userland Makefile

CC = x86_64-elf-gcc
LD = x86_64-elf-ld

BUILD_DIR = build

CFLAGS = -nostdlib -static -fPIE -m64 -march=x86-64 -masm=intel -Ilibc/include -g -Wa,--noexecstack
LDFLAGS = -Wl,-Ttext=0x400000 -Wl,--build-id=none

LIBC_SRC = $(wildcard libc/src/*.c)
LIBC_ASM = $(wildcard libc/src/*.S)
LIBC_OBJ = $(LIBC_SRC:libc/src/%.c=$(BUILD_DIR)/libc/src/%.o) \
           $(LIBC_ASM:libc/src/%.S=$(BUILD_DIR)/libc/src/%.o)

TARGETS = $(BUILD_DIR)/user_prog $(BUILD_DIR)/init $(BUILD_DIR)/shell $(BUILD_DIR)/ls

.PHONY: all
all: $(TARGETS)

$(BUILD_DIR)/libc/src/%.o: libc/src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/libc/src/%.o: libc/src/%.S
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_prog: user_prog.S
	mkdir -p $(dir $@)
	$(CC) -nostdlib -static -fPIE -m64 -march=x86-64 -Wa,-msyntax=intel -Wl,-Ttext=0x400000 -o $@ $<

$(BUILD_DIR)/init: init.c $(LIBC_OBJ)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ init.c $(LIBC_OBJ)

$(BUILD_DIR)/shell: shell.c $(LIBC_OBJ)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ shell.c $(LIBC_OBJ)

$(BUILD_DIR)/ls: ls.c $(LIBC_OBJ)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ ls.c $(LIBC_OBJ)

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
