# Userland Makefile
# Default: dynamically linked. Only cat is statically linked (as an example).

CC = x86_64-elf-gcc
LD = x86_64-elf-ld
AR = x86_64-elf-ar

BUILD_DIR = build

USER_CFLAGS = -nostdlib -fPIE -m64 -march=x86-64 -std=c23 -mavx -masm=intel \
              -Ilibc/include -I../include -O0 -Wa,--noexecstack -fno-builtin \
              -fno-omit-frame-pointer
override CFLAGS := $(USER_CFLAGS) $(CFLAGS)

STATIC_LDFLAGS = -static -Wl,-T,user.ld -Wl,--build-id=none
DYN_LDFLAGS    = -Wl,-T,user_dyn.ld -Wl,--build-id=none \
                 -Wl,-dynamic-linker,/lib/ld.so -Wl,--hash-style=sysv

# Static libc (for cat)
LIBC_SRC = $(wildcard libc/src/*.c)
LIBC_ASM = $(wildcard libc/src/*.S)
LIBC_OBJ = $(LIBC_SRC:libc/src/%.c=$(BUILD_DIR)/libc/src/%.o) \
           $(LIBC_ASM:libc/src/%.S=$(BUILD_DIR)/libc/src/%.o)
LIBC_A = $(BUILD_DIR)/libc/libc.a

# Shared libc (libc.so)
LIBC_SO_CFLAGS = -nostdlib -fPIC -m64 -march=x86-64 -std=c23 -mavx -masm=intel \
                 -Ilibc/include -I../include -O0 -Wa,--noexecstack -fno-builtin \
                 -fno-omit-frame-pointer -DSHARED -ftls-model=initial-exec
LIBC_SO_C_SRC = $(wildcard libc/src/*.c)
LIBC_SO_ASM_SRC = $(filter-out libc/src/start.S,$(wildcard libc/src/*.S))
LIBC_SO_OBJ = $(LIBC_SO_C_SRC:libc/src/%.c=$(BUILD_DIR)/libc/shared/%.o) \
              $(LIBC_SO_ASM_SRC:libc/src/%.S=$(BUILD_DIR)/libc/shared/%.o)
LIBC_SO = $(BUILD_DIR)/libc/libc.so

# Shared elflib (libelf.so)
LIBELF_SO_CFLAGS = -nostdlib -fPIC -m64 -march=x86-64 -std=c23 -mavx -masm=intel \
                   -Ilibc/include -I../include -O0 -Wa,--noexecstack -fno-builtin \
                   -fno-omit-frame-pointer -ftls-model=initial-exec
LIBELF_SO_SRC = $(wildcard elflib/*.c)
LIBELF_SO_OBJ = $(LIBELF_SO_SRC:elflib/%.c=$(BUILD_DIR)/elflib/shared/%.o)
LIBELF_SO = $(BUILD_DIR)/elflib/libelf.so

# Shared wmlib (libwm.so)
LIBWM_SO_CFLAGS = -nostdlib -fPIC -m64 -march=x86-64 -std=c23 -mavx -masm=intel \
                  -Ilibc/include -I../include -O0 -Wa,--noexecstack -fno-builtin \
                  -fno-omit-frame-pointer -ftls-model=initial-exec
LIBWM_SO_SRC = $(wildcard wmlib/*.c)
LIBWM_SO_OBJ = $(LIBWM_SO_SRC:wmlib/%.c=$(BUILD_DIR)/wmlib/shared/%.o)
LIBWM_SO = $(BUILD_DIR)/wmlib/libwm.so

# Runtime linker (ld.so) 
RTLD_CFLAGS = -nostdlib -fPIC -fvisibility=hidden -m64 -march=x86-64 -std=c23 \
              -masm=intel -O2 -Wa,--noexecstack -fno-builtin -fno-omit-frame-pointer \
              -fno-stack-protector -ffreestanding
RTLD_C_SRC = $(wildcard rtld/*.c)
RTLD_ASM_SRC = $(wildcard rtld/*.S)
RTLD_OBJ = $(RTLD_C_SRC:rtld/%.c=$(BUILD_DIR)/rtld/%.o) \
           $(RTLD_ASM_SRC:rtld/%.S=$(BUILD_DIR)/rtld/%.o)
RTLD_BIN = $(BUILD_DIR)/rtld/ld.so

# Programs
PROGRAM_SOURCES = $(wildcard *.c)
PROGRAMS = $(basename $(PROGRAM_SOURCES))

OSTEP_SOURCES = $(wildcard ostep/*.c)
OSTEP_PROGRAMS = $(basename $(OSTEP_SOURCES))

TEST_SOURCES = $(wildcard tests/*.c)
TEST_PROGRAMS = $(basename $(TEST_SOURCES))

WM_SRC = $(wildcard wm/*.c)
WM_OBJ = $(WM_SRC:wm/%.c=$(BUILD_DIR)/wm/%.o)
WM_TARGET = $(BUILD_DIR)/wm/main

# Dynamic link base command 
# CRT start.o + link against libc.so
DYN_LINK_CMD = $(CC) -nostdlib -m64 -march=x86-64 -masm=intel $(DYN_LDFLAGS) \
	-o $@ $(BUILD_DIR)/libc/src/start.o $< \
	-L$(BUILD_DIR)/libc -L$(BUILD_DIR)/wmlib -L$(BUILD_DIR)/elflib -lc

# Targets
TARGETS = $(BUILD_DIR)/user_prog \
          $(PROGRAMS:%=$(BUILD_DIR)/%) \
          $(OSTEP_PROGRAMS:%=$(BUILD_DIR)/%) \
          $(TEST_PROGRAMS:%=$(BUILD_DIR)/%) \
          $(WM_TARGET) \
          $(RTLD_BIN) $(LIBC_SO) $(LIBELF_SO) $(LIBWM_SO)

.PHONY: all
all: $(TARGETS)

# Static libc objects
$(BUILD_DIR)/libc/src/%.o: libc/src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/libc/src/%.o: libc/src/%.S
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBC_A): $(LIBC_OBJ)
	mkdir -p $(dir $@)
	$(AR) rcs $@ $(LIBC_OBJ)

# Shared libc objects
$(BUILD_DIR)/libc/shared/%.o: libc/src/%.c
	mkdir -p $(dir $@)
	$(CC) $(LIBC_SO_CFLAGS) -c $< -o $@

$(BUILD_DIR)/libc/shared/%.o: libc/src/%.S
	mkdir -p $(dir $@)
	$(CC) $(LIBC_SO_CFLAGS) -c $< -o $@

$(LIBC_SO): $(LIBC_SO_OBJ)
	mkdir -p $(dir $@)
	$(LD) -shared -soname libc.so --hash-style=sysv -o $@ $(LIBC_SO_OBJ) -T user_shared.ld

# Shared wmlib objects (libwm.so)
$(BUILD_DIR)/wmlib/shared/%.o: wmlib/%.c
	mkdir -p $(dir $@)
	$(CC) $(LIBWM_SO_CFLAGS) -c $< -o $@

$(LIBWM_SO): $(LIBWM_SO_OBJ) $(LIBC_SO)
	mkdir -p $(dir $@)
	$(LD) -shared -soname libwm.so --hash-style=sysv -o $@ $(LIBWM_SO_OBJ) -T user_shared.ld

# Shared elflib objects (libelf.so)
$(BUILD_DIR)/elflib/shared/%.o: elflib/%.c
	mkdir -p $(dir $@)
	$(CC) $(LIBELF_SO_CFLAGS) -c $< -o $@

$(LIBELF_SO): $(LIBELF_SO_OBJ) $(LIBC_SO)
	mkdir -p $(dir $@)
	$(LD) -shared -soname libelf.so --hash-style=sysv -o $@ $(LIBELF_SO_OBJ) -T user_shared.ld

# Runtime linker (ld.so)
$(BUILD_DIR)/rtld/%.o: rtld/%.c
	mkdir -p $(dir $@)
	$(CC) $(RTLD_CFLAGS) -c $< -o $@

$(BUILD_DIR)/rtld/%.o: rtld/%.S
	mkdir -p $(dir $@)
	$(CC) $(RTLD_CFLAGS) -c $< -o $@

$(RTLD_BIN): $(RTLD_OBJ)
	mkdir -p $(dir $@)
	$(LD) -shared -e _start --no-undefined --hash-style=sysv -soname ld.so \
		-o $@ $(RTLD_OBJ) -T rtld/rtld.ld

# Program objects
$(BUILD_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/wm/%.o: wm/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assembly program
$(BUILD_DIR)/user_prog: user_prog.S
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(STATIC_LDFLAGS) -o $@ $<

# Static programs
# cat: statically linked example
# tls_test: uses thread_local in the executable (rtld lacks per-module TLS)
$(BUILD_DIR)/cat: $(BUILD_DIR)/cat.o $(LIBC_A)
	$(CC) $(CFLAGS) $(STATIC_LDFLAGS) -o $@ $< $(LIBC_A)

$(BUILD_DIR)/tests/tls_test: $(BUILD_DIR)/tests/tls_test.o $(LIBC_A)
	$(CC) $(CFLAGS) $(STATIC_LDFLAGS) -o $@ $< $(LIBC_A)

# Dynamic programs with extra shared libraries
# GUI programs: libc.so + libwm.so
$(BUILD_DIR)/calculator: $(BUILD_DIR)/calculator.o $(BUILD_DIR)/libc/src/start.o $(LIBC_SO) $(LIBWM_SO) $(RTLD_BIN)
	$(DYN_LINK_CMD) -lwm

$(BUILD_DIR)/term: $(BUILD_DIR)/term.o $(BUILD_DIR)/libc/src/start.o $(LIBC_SO) $(LIBWM_SO) $(RTLD_BIN)
	$(DYN_LINK_CMD) -lwm

$(BUILD_DIR)/wmclient_demo: $(BUILD_DIR)/wmclient_demo.o $(BUILD_DIR)/libc/src/start.o $(LIBC_SO) $(LIBWM_SO) $(RTLD_BIN)
	$(DYN_LINK_CMD) -lwm

# ELF tools: libc.so + libelf.so
$(BUILD_DIR)/addr2line: $(BUILD_DIR)/addr2line.o $(BUILD_DIR)/libc/src/start.o $(LIBC_SO) $(LIBELF_SO) $(RTLD_BIN)
	$(DYN_LINK_CMD) -lelf

$(BUILD_DIR)/objdump: $(BUILD_DIR)/objdump.o $(BUILD_DIR)/libc/src/start.o $(LIBC_SO) $(LIBELF_SO) $(RTLD_BIN)
	$(DYN_LINK_CMD) -lelf

$(BUILD_DIR)/ldd: $(BUILD_DIR)/ldd.o $(BUILD_DIR)/libc/src/start.o $(LIBC_SO) $(LIBELF_SO) $(RTLD_BIN)
	$(DYN_LINK_CMD) -lelf

# WM server: libc.so + libwm.so + libelf.so
$(WM_TARGET): $(WM_OBJ) $(BUILD_DIR)/libc/src/start.o $(LIBC_SO) $(LIBWM_SO) $(LIBELF_SO) $(RTLD_BIN)
	mkdir -p $(dir $@)
	$(CC) -nostdlib -m64 -march=x86-64 -masm=intel $(DYN_LDFLAGS) \
		-o $@ $(BUILD_DIR)/libc/src/start.o $(WM_OBJ) \
		-L$(BUILD_DIR)/libc -L$(BUILD_DIR)/wmlib -L$(BUILD_DIR)/elflib -lc -lwm -lelf

# Default: dynamically linked with libc.so
$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(BUILD_DIR)/libc/src/start.o $(LIBC_SO) $(RTLD_BIN)
	$(DYN_LINK_CMD)

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
