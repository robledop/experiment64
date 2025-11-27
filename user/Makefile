# Userland Makefile

CC = x86_64-elf-gcc
LD = x86_64-elf-ld
AR = x86_64-elf-ar

BUILD_DIR = build

CFLAGS = -nostdlib -static -fPIE -m64 -march=x86-64 -std=c23 -mavx -masm=intel -Ilibc/include -g -Wa,--noexecstack
LDFLAGS = -Wl,-Ttext=0x400000 -Wl,--build-id=none

LIBC_SRC = $(wildcard libc/src/*.c)
LIBC_ASM = $(wildcard libc/src/*.S)
LIBC_OBJ = $(LIBC_SRC:libc/src/%.c=$(BUILD_DIR)/libc/src/%.o) \
           $(LIBC_ASM:libc/src/%.S=$(BUILD_DIR)/libc/src/%.o)
LIBC_A = $(BUILD_DIR)/libc/libc.a

PROGRAM_SOURCES = $(wildcard *.c)
PROGRAMS = $(basename $(PROGRAM_SOURCES))
PROGRAM_OBJS = $(PROGRAMS:%=$(BUILD_DIR)/%.o)

TARGETS = $(BUILD_DIR)/user_prog $(PROGRAMS:%=$(BUILD_DIR)/%)

.PHONY: all
all: $(TARGETS)

$(BUILD_DIR)/libc/src/%.o: libc/src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/libc/src/%.o: libc/src/%.S
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBC_A): $(LIBC_OBJ)
	mkdir -p $(dir $@)
	$(AR) rcs $@ $(LIBC_OBJ)

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_prog: user_prog.S
	mkdir -p $(dir $@)
	$(CC) -nostdlib -static -fPIE -m64 -march=x86-64 -Wa,-msyntax=intel -Wl,-Ttext=0x400000 -o $@ $<

$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(LIBC_A)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBC_A)

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
