# Userland Makefile

CC = x86_64-elf-gcc
LD = x86_64-elf-ld
AR = x86_64-elf-ar

BUILD_DIR = build

USER_CFLAGS = -nostdlib -static -fPIE -m64 -march=x86-64 -std=c23 -mavx -masm=intel -Ilibc/include -ggdb -O0 -Wa,--noexecstack -fno-builtin
override CFLAGS := $(USER_CFLAGS) $(CFLAGS)
LDFLAGS = -Wl,-T,user.ld -Wl,--build-id=none

LIBC_SRC = $(wildcard libc/src/*.c)
LIBC_ASM = $(wildcard libc/src/*.S)
LIBC_OBJ = $(LIBC_SRC:libc/src/%.c=$(BUILD_DIR)/libc/src/%.o) \
           $(LIBC_ASM:libc/src/%.S=$(BUILD_DIR)/libc/src/%.o)
LIBC_A = $(BUILD_DIR)/libc/libc.a

PROGRAM_SOURCES = $(wildcard *.c)
PROGRAMS = $(basename $(PROGRAM_SOURCES))
PROGRAM_OBJS = $(PROGRAMS:%=$(BUILD_DIR)/%.o)

OSTEP_SOURCES = $(wildcard ostep/*.c)
OSTEP_PROGRAMS = $(basename $(OSTEP_SOURCES))
OSTEP_OBJS = $(OSTEP_PROGRAMS:%=$(BUILD_DIR)/%.o)

TEST_SOURCES = $(wildcard tests/*.c)
TEST_PROGRAMS = $(basename $(TEST_SOURCES))
TEST_OBJS = $(TEST_PROGRAMS:%=$(BUILD_DIR)/%.o)

WM_SRC = $(wildcard wm/*.c)
WM_LIB_SRC = $(wildcard wmlib/*.c)
WM_OBJ = $(WM_SRC:wm/%.c=$(BUILD_DIR)/wm/%.o)
WM_LIB_OBJ = $(WM_LIB_SRC:wmlib/%.c=$(BUILD_DIR)/wmlib/%.o)
WM_TARGET = $(BUILD_DIR)/wm/main
WM_BASE_OBJ = $(filter-out $(BUILD_DIR)/wm/main.o,$(WM_OBJ))

TARGETS = $(BUILD_DIR)/user_prog $(PROGRAMS:%=$(BUILD_DIR)/%) $(OSTEP_PROGRAMS:%=$(BUILD_DIR)/%) $(TEST_PROGRAMS:%=$(BUILD_DIR)/%) $(WM_TARGET)

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
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(LIBC_A)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBC_A)

$(BUILD_DIR)/term: $(BUILD_DIR)/term.o $(LIBC_A) $(WM_BASE_OBJ) $(WM_LIB_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(BUILD_DIR)/term.o $(WM_BASE_OBJ) $(WM_LIB_OBJ) $(LIBC_A)


$(BUILD_DIR)/calculator: $(BUILD_DIR)/calculator.o $(LIBC_A) $(WM_BASE_OBJ) $(WM_LIB_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(BUILD_DIR)/calculator.o $(WM_BASE_OBJ) $(WM_LIB_OBJ) $(LIBC_A)

$(BUILD_DIR)/wmclient_demo: $(BUILD_DIR)/wmclient_demo.o $(LIBC_A) $(WM_BASE_OBJ) $(WM_LIB_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(BUILD_DIR)/wmclient_demo.o $(WM_BASE_OBJ) $(WM_LIB_OBJ) $(LIBC_A)

$(BUILD_DIR)/wm/%.o: wm/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/wmlib/%.o: wm/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(WM_TARGET): $(WM_OBJ) $(WM_LIB_OBJ) $(LIBC_A)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(WM_OBJ) $(WM_LIB_OBJ) $(LIBC_A)

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	#$(MAKE) -C doom clean
