PATH := $(HOME)/opt/cross/bin:$(PATH)
export PATH

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
$(eval $(call DEFAULT_VAR,CFLAGS,-O2 -g -Wall -Wextra -pipe -pedantic))
$(eval $(call DEFAULT_VAR,LDFLAGS,))

ifdef KASAN
override CFLAGS += -DKASAN
endif

ROOTFS=rootfs

override MEM ?= 2G
override SMP ?= 8
# Secondary disk image for IDE (ext2).
IDE_DISK := image2.ide

# Common QEMU pieces
QEMU_BASE := qemu-system-x86_64 -M pc -m $(MEM) -smp $(SMP)
QEMU_DRIVES :=  \
	-drive if=none,file=image.hdd,format=raw,id=ahcibase \
	-device ahci,id=ahci \
	-device ide-hd,bus=ahci.0,drive=ahcibase,bootindex=1 \
	-drive if=ide,file=$(IDE_DISK),format=raw,index=1

override CFLAGS += \
    -I. \
    -Iinclude \
    -std=c23 \
    -ffreestanding \
    -nostdlib \
    -fno-lto \
    -fPIE \
    -ggdb \
    -mavx \
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

run-gdb tests tests-gdb: CFLAGS += -DDEBUG -fstack-protector-strong -fsanitize=undefined

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
	rm -rf build $(USER_BUILD_DIR) $(ROOTFS) *.hdd *.img *.log *.ide $(DOOM_BIN)
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

.PHONY: doom
doom: $(DOOM_BIN)

$(DOOM_BIN):
	$(MAKE) -C user/doom

image.hdd: $(KERNEL) limine limine.conf userland $(DOOM_BIN)
	./scripts/make_image.sh $(KERNEL) $(ROOTFS) $(USER_BUILD_DIR)

.PHONY: disk
disk: clean
	$(MAKE) image.hdd CFLAGS+=" -DDEBUG -DKASAN"
	./scripts/install-on-disk.sh

.PHONY: run
run: clean
	$(MAKE) image.hdd
	$(QEMU_BASE) $(QEMU_DRIVES) -serial stdio -display gtk,zoom-to-fit=on -cpu host -enable-kvm

.PHONY: vbox
vbox: clean
	$(MAKE) KASAN=1 image.hdd
	./scripts/start_vbox.sh

.PHONY: run-gdb
run-gdb: clean
	$(MAKE) KASAN=1 image.hdd
	$(QEMU_BASE) $(QEMU_DRIVES) -display gtk,zoom-to-fit=on ${QEMUGDB}

.PHONY: tests
tests: clean
	$(MAKE) KASAN=1 image.hdd CFLAGS="$(CFLAGS) -DTEST_MODE"
	timeout 10s $(QEMU_BASE) $(QEMU_DRIVES) -display none -serial file:test.log -device isa-debug-exit,iobase=0x501,iosize=0x04  -cpu host -enable-kvm || true
	cat test.log
	@grep -q "ALL TESTS PASSED" test.log || (echo "Tests did not complete successfully"; exit 1)

.PHONY: tests-gdb
tests-gdb: clean
	$(MAKE) KASAN=1 image.hdd CFLAGS="$(CFLAGS) -DTEST_MODE"
	$(QEMU_BASE) $(QEMU_DRIVES) -device isa-debug-exit,iobase=0x501,iosize=0x04 ${QEMUGDB} -cpu max | tee test.log

.PHONY: bear
bear: clean
	bear -- $(MAKE) $(KERNEL) userland -j22

.PHONY: clangd-check
clangd-check:
	@echo "Running clangd checks..."
	@python3 scripts/clangd_check.py

.PHONY: clang-tidy
clang-tidy:
	@echo "Running clang-tidy checks..."
	@python3 scripts/clang_tidy_all.py

.PHONY: check
check: clangd-check clang-tidy
