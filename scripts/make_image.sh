#!/bin/bash
set -euo pipefail

KERNEL=$1
ROOTFS=$2
USER_BUILD_DIR=$3

# Paths for second disk (IDE) with a single ext2 partition.
SECOND_DISK="image2.ide"
SECOND_EXT2_DISK_SIZE_MB=64
SECOND_EXT2_PART_START_MB=1
SECOND_EXT2_PART_SIZE_MB=31
# Paths for USB disk with a single ext2 partition.
USB_DISK="image3.usb"
USB_EXT2_DISK_SIZE_MB=64
USB_EXT2_PART_START_MB=1
USB_EXT2_PART_SIZE_MB=31

bytes_for_mb() {
    echo $(( $1 * 1024 * 1024 ))
}

check_gpt_signature() {
    local image=$1
    local label=$2
    local sig
    sig=$(dd if="$image" bs=1 skip=512 count=8 2>/dev/null || true)
    if [[ "$sig" != "EFI PART" ]]; then
        echo "Sanity check failed: missing GPT signature in $label ($image)" >&2
        exit 1
    fi
}

check_ext2_superblock() {
    local image=$1
    local partition_offset_bytes=$2
    local magic_offset=$((partition_offset_bytes + 1024 + 56))
    local magic
    magic=$(dd if="$image" bs=1 skip=$magic_offset count=2 2>/dev/null | hexdump -v -e '1/1 "%02x"' || true)
    if [[ "$magic" != "53ef" ]]; then
        echo "Sanity check failed: ext2 magic not found in $image (got '$magic')" >&2
        exit 1
    fi
}

check_secondary_disk() {
    local expected_bytes
    expected_bytes=$(bytes_for_mb "$SECOND_EXT2_DISK_SIZE_MB")
    local actual_bytes
    actual_bytes=$(wc -c < "$SECOND_DISK" | tr -d '[:space:]')
    if (( actual_bytes < expected_bytes )); then
        echo "Sanity check failed: $SECOND_DISK is smaller than expected ($actual_bytes < $expected_bytes bytes)" >&2
        exit 1
    fi

    check_gpt_signature "$SECOND_DISK" "secondary IDE disk"
    local part_offset_bytes
    part_offset_bytes=$(bytes_for_mb "$SECOND_EXT2_PART_START_MB")
    check_ext2_superblock "$SECOND_DISK" "$part_offset_bytes"
}

check_usb_disk() {
    local expected_bytes
    expected_bytes=$(bytes_for_mb "$USB_EXT2_DISK_SIZE_MB")
    local actual_bytes
    actual_bytes=$(wc -c < "$USB_DISK" | tr -d '[:space:]')
    if (( actual_bytes < expected_bytes )); then
        echo "Sanity check failed: $USB_DISK is smaller than expected ($actual_bytes < $expected_bytes bytes)" >&2
        exit 1
    fi

    check_gpt_signature "$USB_DISK" "USB disk"
    local part_offset_bytes
    part_offset_bytes=$(bytes_for_mb "$USB_EXT2_PART_START_MB")
    check_ext2_superblock "$USB_DISK" "$part_offset_bytes"
}

rm -f image.hdd part.img "$SECOND_DISK" second_root.img "$USB_DISK" usb_root.img
dd if=/dev/zero of=image.hdd bs=1M count=160
parted -s image.hdd mklabel gpt
parted -s image.hdd mkpart ESP fat32 1MiB 63MiB
parted -s image.hdd set 1 esp on
parted -s image.hdd mkpart LINUX ext2 63MiB 127MiB
parted -s image.hdd mkpart DATA fat32 127MiB 159MiB

# Prepare directories
rm -rf build/rootfs_esp build/rootfs_ext2 build/rootfs_data
mkdir -p build/rootfs_esp/EFI/BOOT
mkdir -p build/rootfs_esp/boot/limine
mkdir -p build/rootfs_ext2/bin
mkdir -p build/rootfs_ext2/ostep
mkdir -p build/rootfs_ext2/tests
mkdir -p build/rootfs_ext2/mnt
mkdir -p build/rootfs_ext2/disk1
mkdir -p build/rootfs_ext2/usb
mkdir -p build/rootfs_ext2/boot
mkdir -p build/rootfs_data/test_dir
mkdir -p build/rootfs_data/docs

cp -r assets/web build/rootfs_ext2/web/

# Populate ESP
cp -v "$KERNEL" build/rootfs_esp/boot/
cp -v limine.conf limine/limine-bios.sys build/rootfs_esp/boot/limine/
cp -v limine/BOOTX64.EFI limine/BOOTIA32.EFI build/rootfs_esp/EFI/BOOT/

# Populate RootFS (Ext2)
mkdir -p build/rootfs_ext2/var
mkdir -p build/rootfs_ext2/var/log
cp -v assets/logo.bmp build/rootfs_ext2/var/logo.bmp
if [ -f assets/wpaper.bmp ]; then
    cp -v assets/wpaper.bmp build/rootfs_ext2/var/wpaper.bmp
fi
if [ -f assets/doom.wad ]; then
    cp -v assets/doom.wad build/rootfs_ext2/doom.wad
fi
if [ -f assets/fbdoom ]; then
    cp -v assets/fbdoom build/rootfs_ext2/bin/doom
fi
if [ -f assets/httpd.c ]; then
    cp -v assets/httpd.c build/rootfs_ext2/var/httpd.c
fi
for bin in "$USER_BUILD_DIR"/*; do
    if [ ! -f "$bin" ]; then
        continue
    fi
    base=$(basename "$bin")
    case "$base" in
        *.o|*.a|*.d) continue ;;
    esac
    dest="$base"
    if [ "$base" = "user_prog" ]; then
        dest="prog"
    fi
    cp -v "$bin" "build/rootfs_ext2/bin/$dest"
done
# Copy window manager binary
if [ -f "$USER_BUILD_DIR/wm/main" ]; then
    cp -v "$USER_BUILD_DIR/wm/main" "build/rootfs_ext2/bin/wm"
fi
for ostep_bin in "$USER_BUILD_DIR"/ostep/*; do
    if [ ! -f "$ostep_bin" ]; then
        continue
    fi
    base=$(basename "$ostep_bin")
    case "$base" in
        *.o|*.a|*.d) continue ;;
    esac
    cp -v "$ostep_bin" "build/rootfs_ext2/ostep/$base"
done
for test_bin in "$USER_BUILD_DIR"/tests/*; do
    if [ ! -f "$test_bin" ]; then
        continue
    fi
    base=$(basename "$test_bin")
    case "$base" in
        *.o|*.a|*.d) continue ;;
    esac
    cp -v "$test_bin" "build/rootfs_ext2/tests/$base"
done
echo "Hello Ext2" > build/rootfs_ext2/test.txt
echo "Hello Ext2 Upper" > build/rootfs_ext2/TEST.TXT

# Create ESP Image (Part 1, 62MB)
dd if=/dev/zero of=esp.img bs=1M count=62
mformat -i esp.img -F ::
mcopy -i esp.img -s build/rootfs_esp/* ::/

# Create RootFS Image (Part 2, 64MB)
dd if=/dev/zero of=root.img bs=1M count=64
mkfs.ext2 -b 1024 -d build/rootfs_ext2 -r 1 -N 0 -m 0 -L "ROOT" root.img

# Create Data Image (Part 3, 32MB)
echo "Hello Data Partition" > build/rootfs_data/data_test.txt
echo "This is a file in a subdirectory" > build/rootfs_data/test_dir/subfile.txt
echo "Documentation file" > build/rootfs_data/docs/readme.md
echo "Hello FAT32" > build/rootfs_data/TEST.TXT

dd if=/dev/zero of=data.img bs=1M count=32
mformat -i data.img -F ::
mcopy -i data.img -s build/rootfs_data/* ::/

# Assemble Image
dd if=esp.img of=image.hdd bs=1M seek=1 conv=notrunc
dd if=root.img of=image.hdd bs=1M seek=63 conv=notrunc
dd if=data.img of=image.hdd bs=1M seek=127 conv=notrunc

./limine/limine bios-install image.hdd

# Second disk (IDE) with a single ext2 partition starting at 1MiB.
dd if=/dev/zero of="$SECOND_DISK" bs=1M count=$SECOND_EXT2_DISK_SIZE_MB
parted -s "$SECOND_DISK" mklabel gpt
parted -s "$SECOND_DISK" mkpart EXT2 ext2 "${SECOND_EXT2_PART_START_MB}MiB" "$((SECOND_EXT2_PART_START_MB + SECOND_EXT2_PART_SIZE_MB))MiB"

# Build ext2 content for second disk
rm -rf build/rootfs_ext2_disk2
mkdir -p build/rootfs_ext2_disk2
echo "Hello from IDE disk ext2" > build/rootfs_ext2_disk2/hello.txt

dd if=/dev/zero of=second_root.img bs=1M count=$SECOND_EXT2_PART_SIZE_MB
mkfs.ext2 -b 1024 -d build/rootfs_ext2_disk2 -r 1 -N 0 -m 0 -L "IDEEXT2" second_root.img
dd if=second_root.img of="$SECOND_DISK" bs=1M seek=$SECOND_EXT2_PART_START_MB conv=notrunc

# USB disk with a single ext2 partition starting at 1MiB.
dd if=/dev/zero of="$USB_DISK" bs=1M count=$USB_EXT2_DISK_SIZE_MB
parted -s "$USB_DISK" mklabel gpt
parted -s "$USB_DISK" mkpart EXT2 ext2 "${USB_EXT2_PART_START_MB}MiB" "$((USB_EXT2_PART_START_MB + USB_EXT2_PART_SIZE_MB))MiB"

# Build ext2 content for USB disk
rm -rf build/rootfs_ext2_usb
mkdir -p build/rootfs_ext2_usb
echo "Hello from USB disk ext2" > build/rootfs_ext2_usb/hello.txt

dd if=/dev/zero of=usb_root.img bs=1M count=$USB_EXT2_PART_SIZE_MB
mkfs.ext2 -b 1024 -d build/rootfs_ext2_usb -r 1 -N 0 -m 0 -L "USBEXT2" usb_root.img
dd if=usb_root.img of="$USB_DISK" bs=1M seek=$USB_EXT2_PART_START_MB conv=notrunc

# Quick sanity checks to catch a bad image early instead of flaking in QEMU.
check_gpt_signature image.hdd "primary disk image"
check_secondary_disk
check_usb_disk
