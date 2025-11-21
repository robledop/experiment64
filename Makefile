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
    -std=c11 \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fPIE \
    -m64 \
    -march=x86-64 \
    -mno-80387 \
    -mno-red-zone

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

.PHONY: clean
clean:
	rm -rf build

.PHONY: distclean
distclean: clean
	rm -rf limine $(ROOTFS) image.iso image.hdd

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v8.x-binary --depth=1
	make -C limine

image.hdd: $(KERNEL) limine limine.conf
	rm -f image.hdd part.img
	dd if=/dev/zero of=image.hdd bs=1M count=64
	parted -s image.hdd mklabel gpt
	parted -s image.hdd mkpart ESP fat32 1MiB 63MiB
	parted -s image.hdd set 1 esp on
	dd if=/dev/zero of=part.img bs=1M count=62
	mformat -i part.img -F ::
	rm -rf $(ROOTFS)
	mkdir -p $(ROOTFS)/EFI/BOOT
	mkdir -p $(ROOTFS)/boot/limine
	cp -v $(KERNEL) $(ROOTFS)/boot/
	cp -v limine.conf limine/limine-bios.sys $(ROOTFS)/boot/limine/
	cp -v limine/BOOTX64.EFI limine/BOOTIA32.EFI $(ROOTFS)/EFI/BOOT/
	mcopy -i part.img -s $(ROOTFS)/* ::/
	dd if=part.img of=image.hdd bs=1M seek=1 conv=notrunc
	./limine/limine bios-install image.hdd
	rm -f part.img
	rm -rf $(ROOTFS)

.PHONY: run
run: image.hdd
	qemu-system-x86_64 -M q35 -m 2G -drive file=image.hdd,format=raw -serial stdio -display gtk,zoom-to-fit=on
