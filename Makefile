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
$(eval $(call DEFAULT_VAR,CC,cc))
$(eval $(call DEFAULT_VAR,LD,ld))
$(eval $(call DEFAULT_VAR,CFLAGS,-O2 -g -Wall -Wextra -pipe))
$(eval $(call DEFAULT_VAR,LDFLAGS,))

ROOTFS=rootfs

# Internal C flags that should not be changed by the user.
override CFLAGS += \
    -I. \
    -Iinclude \
    -std=c2x \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fPIE \
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
	rm -rf build $(USER_BUILD_DIR) $(ROOTFS) image.iso image.hdd test.log part.img

.PHONY: distclean
distclean: clean
	rm -rf limine $(ROOTFS) image.iso image.hdd

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v8.x-binary --depth=1
	make -C limine

# Userland
USER_BUILD_DIR = user/build
USER_CFLAGS = -nostdlib -static -fPIE -m64 -march=x86-64 -Iuser/libc/include -g
USER_LDFLAGS = -Wl,-Ttext=0x400000 -Wl,--build-id=none

LIBC_SRC = $(wildcard user/libc/src/*.c)
LIBC_ASM = $(wildcard user/libc/src/*.S)
LIBC_OBJ = $(LIBC_SRC:user/libc/src/%.c=$(USER_BUILD_DIR)/libc/src/%.o) \
           $(LIBC_ASM:user/libc/src/%.S=$(USER_BUILD_DIR)/libc/src/%.o)

$(USER_BUILD_DIR)/libc/src/%.o: user/libc/src/%.c
	mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_BUILD_DIR)/libc/src/%.o: user/libc/src/%.S
	mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_BUILD_DIR)/user_prog: user/user_prog.S
	mkdir -p $(dir $@)
	$(CC) -nostdlib -static -fPIE -m64 -march=x86-64 -Wa,-msyntax=intel -Wl,-Ttext=0x400000 -o $@ $<

$(USER_BUILD_DIR)/init: user/init.c $(LIBC_OBJ)
	mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ user/init.c $(LIBC_OBJ)

$(USER_BUILD_DIR)/shell: user/shell.c $(LIBC_OBJ)
	mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ user/shell.c $(LIBC_OBJ)

image.hdd: $(KERNEL) limine limine.conf $(USER_BUILD_DIR)/user_prog $(USER_BUILD_DIR)/init $(USER_BUILD_DIR)/shell
	rm -f image.hdd part.img
	dd if=/dev/zero of=image.hdd bs=1M count=128
	parted -s image.hdd mklabel gpt
	parted -s image.hdd mkpart ESP fat32 1MiB 63MiB
	parted -s image.hdd set 1 esp on
	parted -s image.hdd mkpart DATA fat32 63MiB 95MiB
	parted -s image.hdd mkpart LINUX ext2 95MiB 100%
	dd if=/dev/zero of=part.img bs=1M count=62
	mformat -i part.img -F ::
	rm -rf $(ROOTFS)
	mkdir -p $(ROOTFS)/EFI/BOOT
	mkdir -p $(ROOTFS)/boot/limine
	cp -v $(KERNEL) $(ROOTFS)/boot/
	cp -v limine.conf limine/limine-bios.sys $(ROOTFS)/boot/limine/
	cp -v limine/BOOTX64.EFI limine/BOOTIA32.EFI $(ROOTFS)/EFI/BOOT/
	cp -v $(USER_BUILD_DIR)/user_prog $(ROOTFS)/prog
	cp -v $(USER_BUILD_DIR)/init $(ROOTFS)/init
	cp -v $(USER_BUILD_DIR)/shell $(ROOTFS)/shell
	echo "Hello FAT32" > $(ROOTFS)/test.txt
	mcopy -i part.img -s $(ROOTFS)/* ::/
	dd if=part.img of=image.hdd bs=1M seek=1 conv=notrunc
	./limine/limine bios-install image.hdd

.PHONY: run
run: clean
	$(MAKE) image.hdd
	qemu-system-x86_64 -M pc -m 2G -drive file=image.hdd,format=raw -serial stdio -display gtk,zoom-to-fit=on

.PHONY: tests
tests: clean
	$(MAKE) image.hdd CFLAGS="$(CFLAGS) -DTEST_MODE"
	qemu-system-x86_64 -M pc -m 2G -drive file=image.hdd,format=raw -serial stdio -display none -device isa-debug-exit,iobase=0x501,iosize=0x04 | tee test.log
	grep "ALL TESTS PASSED" test.log
