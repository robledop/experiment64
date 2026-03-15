PATH := $(HOME)/opt/cross/bin:$(PATH)
export PATH

# Force MAKE to be the real make, not whatever Cursor sets it to
override MAKE := $(shell which make)

# Nuke built-in rules and variables.
override MAKEFLAGS += -rR

override KERNEL := build/kernel.elf
DOOM_BIN := assets/fbdoom

define DEFAULT_VAR =
    ifeq ($(origin $1),default)
        override $(1) := $(2)
    endif
    ifeq ($(origin $1),undefined)
        override $(1) := $(2)
    endif
endef

$(eval $(call DEFAULT_VAR,CC,x86_64-elf-gcc))
$(eval $(call DEFAULT_VAR,LD,x86_64-elf-ld))
$(eval $(call DEFAULT_VAR,CFLAGS,-O3 -g -Wall -Wextra -pipe -pedantic))
$(eval $(call DEFAULT_VAR,LDFLAGS,))

ROOTFS=rootfs

override MEM ?= 128M
override SMP ?= 22
# Secondary disk image for IDE (ext2).
IDE_DISK := image2.ide
# USB disk image (ext2).
USB_DISK := image3.usb

QEMU_BASE := qemu-system-x86_64 -M pc -m $(MEM) -smp $(SMP)
QEMU_DRIVES :=  \
	-drive if=none,file=image.hdd,format=raw,id=ahcibase \
	-device ahci,id=ahci \
	-device ide-hd,bus=ahci.0,drive=ahcibase,bootindex=1 \
	-drive if=ide,file=$(IDE_DISK),format=raw,index=1 \
	-device qemu-xhci,id=xhci \
	-drive if=none,file=$(USB_DISK),format=raw,id=usbdrive \
	-device usb-storage,bus=xhci.0,drive=usbdrive

QEMU_DRIVES_SMALL :=  \
	-drive if=none,file=small-image.hdd,format=raw,id=ahcibase \
	-device ahci,id=ahci \
	-device ide-hd,bus=ahci.0,drive=ahcibase,bootindex=1 \
	-device qemu-xhci,id=xhci

QEMU_USB_TABLET := -device usb-tablet,bus=xhci.0

QEMU_DRIVES_USBBOOT :=  \
	-device qemu-xhci,id=xhci \
	-drive if=none,file=image.hdd,format=raw,id=usbboot \
	-device usb-storage,bus=xhci.0,drive=usbboot,bootindex=1

QEMU_NETWORK_USER=-netdev user,id=net0,hostfwd=tcp::8080-:80 -device e1000,netdev=net0
QEMU_NETWORK_TAP=-netdev tap,id=net0,ifname=tap1,script=no,downscript=no -device e1000,netdev=net0
QEMU_NETWORK_BRIDGE=-netdev bridge,id=n0,br=br0 -device e1000,netdev=n0
QEMU_NETWORK=$(QEMU_NETWORK_USER)

override CFLAGS += \
    -I. \
    -Iinclude \
    -std=c23 \
    -ffreestanding \
	-fno-builtin \
    -ftree-vectorize \
    -nostdlib \
    -fno-lto \
    -fPIE \
    -ggdb \
    -mavx \
    -msse2 \
    -mfpmath=sse \
    -m64 \
    -march=x86-64 \
    -masm=intel \
    -mno-80387 \
    -mno-red-zone \
    -MMD \
    -MP \
    -Wa,--noexecstack \

override LDFLAGS += \
    -m elf_x86_64 \
    -nostdlib \
    -static \
    -z max-page-size=0x1000 \
    -T linker.ld

override CFILES := $(shell find kernel -type f -name '*.c')
override ASFILES := $(shell find kernel -type f -name '*.S')
override OBJ := $(CFILES:kernel/%.c=build/%.o) $(ASFILES:kernel/%.S=build/%.o)
override DEPS := $(CFILES:kernel/%.c=build/%.d)

run-gdb: CFLAGS := $(patsubst -O%,-O0,$(CFLAGS))
run-gdb tests tests-gdb vbox: CFLAGS += -DDEBUG -fstack-protector-strong -fsanitize=undefined

QEMUGDB = -daemonize -S -gdb tcp::1234 -d int -D qemu.log -cpu max

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
	rm -rf build $(USER_BUILD_DIR) $(ROOTFS) *.hdd *.img *.log *.ide *.usb *.vdi scripts/stress_logs
	$(MAKE) -C user clean

.PHONY: clean-doom
clean-doom:
	$(MAKE) -C user/doom clean

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v8.x-binary --depth=1
	make -C limine

# Userland
USER_BUILD_DIR = user/build
LIBC_A = user/build/libc/libc.a
USERLAND_FLAGS = $(filter -DDEBUG -DTEST_MODE -DHEADLESS,$(CFLAGS))

all: $(KERNEL) userland

.PHONY: userland
userland:
	$(MAKE) -C user CFLAGS="$(USERLAND_FLAGS)"

$(LIBC_A): userland

.PHONY: doom
doom: $(DOOM_BIN)

$(DOOM_BIN): $(LIBC_A)
	$(MAKE) -C user/doom -j1 CFLAGS="$(USERLAND_FLAGS)"

image.hdd: $(KERNEL) limine limine.conf userland $(DOOM_BIN)
	./scripts/make_image.sh $(KERNEL) $(ROOTFS) $(USER_BUILD_DIR)

small-image.hdd: $(KERNEL) limine limine.conf userland
	./scripts/make_small_image.sh $(KERNEL) $(ROOTFS) $(USER_BUILD_DIR)

.PHONY: small-image
small-image:
	$(MAKE) small-image.hdd

.PHONY: disk
disk: bear
	$(MAKE) image.hdd
	./scripts/install-on-disk.sh

.PHONE: qemu-nobuild
qemu-nobuild:
	$(QEMU_BASE) $(QEMU_DRIVES) $(QEMU_USB_TABLET) -serial stdio -display gtk,zoom-to-fit=on -cpu host -enable-kvm

.PHONY: run
run: clean
	$(MAKE) image.hdd
	$(QEMU_BASE) $(QEMU_DRIVES) $(QEMU_USB_TABLET) $(QEMU_NETWORK) -serial stdio -display gtk,zoom-to-fit=on -cpu host -enable-kvm

.PHONY: run-small-image
run-small-image:
	$(MAKE) small-image.hdd
	$(QEMU_BASE) $(QEMU_DRIVES_SMALL) $(QEMU_USB_TABLET) $(QEMU_NETWORK) -serial stdio -display gtk,zoom-to-fit=on -cpu host -enable-kvm

.PHONY: run-usb
run-usb:
	$(MAKE) image.hdd
	$(QEMU_BASE) $(QEMU_DRIVES_USBBOOT) $(QEMU_USB_TABLET) $(QEMU_NETWORK) -serial stdio -display gtk,zoom-to-fit=on -cpu host -enable-kvm

.PHONY: run-nox
run-nox:
	$(MAKE) image.hdd CFLAGS="$(CFLAGS) -DHEADLESS"
	$(QEMU_BASE) $(QEMU_DRIVES) $(QEMU_USB_TABLET) $(QEMU_NETWORK) -nographic -cpu host -enable-kvm

.PHONY: run-tap
run-tap:
	$(MAKE) image.hdd
	$(QEMU_BASE) $(QEMU_DRIVES) $(QEMU_USB_TABLET) $(QEMU_NETWORK_TAP) -serial stdio -display gtk,zoom-to-fit=on -cpu host -enable-kvm

.PHONY: vbox
vbox: 
	$(MAKE) image.hdd CFLAGS="$(CFLAGS)"
	./scripts/start_vbox.sh

.PHONY: run-gdb
run-gdb: bear
	$(MAKE) image.hdd CFLAGS="$(CFLAGS)"
	$(QEMU_BASE) $(QEMU_DRIVES) $(QEMU_USB_TABLET) $(QEMU_NETWORK) -display gtk,zoom-to-fit=on ${QEMUGDB}

.PHONY: tests
tests: clean
	$(MAKE) image.hdd CFLAGS="$(CFLAGS) -DTEST_MODE"
	timeout 20s $(QEMU_BASE) $(QEMU_DRIVES) -display none -serial file:test.log -device isa-debug-exit,iobase=0x501,iosize=0x04  -cpu host -enable-kvm || true
	cat test.log
	@grep -q "ALL TESTS PASSED" test.log || (echo "Tests did not complete successfully"; exit 1)

.PHONY: tests-gdb
tests-gdb: clean
	$(MAKE) image.hdd CFLAGS="$(CFLAGS) -DTEST_MODE"
	$(QEMU_BASE) $(QEMU_DRIVES) -device isa-debug-exit,iobase=0x501,iosize=0x04 ${QEMUGDB} -cpu max | tee test.log

.PHONY: bear
bear: clean clean-doom
	bear -- $(MAKE) $(KERNEL) userland doom -j22

.PHONY: clang-tidy
clang-tidy:
	@echo "Running clang-tidy checks..."
	@python3 scripts/clang_tidy_all.py
