# Nuke built-in rules and variables.
override MAKEFLAGS += -rR

# Define the kernel name.
override KERNEL := build/kernel.elf

# Convenience macro to reliably declare user overridable variables.
define DEFAULT_VAR =
    ifeq ($(origin $1),default)
        override $(1) := $(2)
    endif
    ifeq ($(origin $1),undefined)
        override $(1) := $(2)
    endif
endef

# Toolchain configuration.
$(eval $(call DEFAULT_VAR,CC,gcc))
$(eval $(call DEFAULT_VAR,LD,ld))
$(eval $(call DEFAULT_VAR,CFLAGS,-O2 -g -Wall -Wextra -pipe))
$(eval $(call DEFAULT_VAR,LDFLAGS,))

ROOTFS=rootfs

# Internal C flags that should not be changed by the user.
override CFLAGS += \
    -I. \
    -Iinclude \
    -std=c23 \
    -ffreestanding \
    -nostdlib \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fPIE \
    -ggdb \
    -m64 \
    -march=x86-64 \
    -mno-80387 \
    -mno-red-zone \
    -MMD \
    -MP

# Internal linker flags that should not be changed by the user.
override LDFLAGS += \
    -m elf_x86_64 \
    -nostdlib \
    -static \
    -z max-page-size=0x1000 \
    -T linker.ld

# Source files.
override CFILES := $(shell find kernel -type f -name '*.c')
override ASFILES := $(shell find kernel -type f -name '*.S')
override OBJ := $(CFILES:kernel/%.c=build/%.o) $(ASFILES:kernel/%.S=build/%.o)
override DEPS := $(CFILES:kernel/%.c=build/%.d)

QEMUGDB = -daemonize -S -gdb tcp::1234 -d int -D qemu.log

.PHONY: all
all: $(KERNEL)

$(KERNEL): $(OBJ)
	mkdir -p build
	$(LD) $(OBJ) $(LDFLAGS) -o $@

build/%.o: kernel/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: kernel/%.S
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wa,-msyntax=intel -Wa,-mnaked-reg -c $< -o $@

-include $(DEPS)

.PHONY: clean
clean:
	rm -rf build $(USER_BUILD_DIR) $(ROOTFS) image.hdd test.log part.img
	$(MAKE) -C user clean

.PHONY: distclean
distclean: clean
	rm -rf limine $(ROOTFS) image.iso image.hdd

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v8.x-binary --depth=1
	make -C limine

# Userland
USER_BUILD_DIR = user/build

.PHONY: userland
userland:
	$(MAKE) -C user

image.hdd: $(KERNEL) limine limine.conf userland
	./scripts/make_image.sh $(KERNEL) $(ROOTFS) $(USER_BUILD_DIR)

.PHONY: run
run: clean
	$(MAKE) image.hdd
	qemu-system-x86_64 -M pc -m 2G -smp 4 -drive file=image.hdd,format=raw -serial stdio -display gtk,zoom-to-fit=on


.PHONY: run-gdb
run-gdb: clean
	$(MAKE) image.hdd
	qemu-system-x86_64 -M pc -m 2G -smp 4 -drive file=image.hdd,format=raw -display gtk,zoom-to-fit=on ${QEMUGDB}

.PHONY: tests
tests: clean
	$(MAKE) image.hdd CFLAGS="$(CFLAGS) -DTEST_MODE"
	qemu-system-x86_64 -M pc -m 2G -drive file=image.hdd,format=raw -serial stdio -display none -device isa-debug-exit,iobase=0x501,iosize=0x04 | tee test.log
	grep "ALL TESTS PASSED" test.log

.PHONY: tests-gdb
tests-gdb: clean
	$(MAKE) image.hdd CFLAGS="$(CFLAGS) -DTEST_MODE"
	qemu-system-x86_64 -M pc -m 2G -drive file=image.hdd,format=raw -device isa-debug-exit,iobase=0x501,iosize=0x04 ${QEMUGDB} | tee test.log


.PHONY: bear
bear: clean
	bear -- $(MAKE) $(KERNEL) userland -j22
