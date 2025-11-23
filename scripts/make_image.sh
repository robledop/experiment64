#!/bin/bash
set -e

KERNEL=$1
ROOTFS=$2
USER_BUILD_DIR=$3

rm -f image.hdd part.img
dd if=/dev/zero of=image.hdd bs=1M count=128
parted -s image.hdd mklabel gpt
parted -s image.hdd mkpart ESP fat32 1MiB 63MiB
parted -s image.hdd set 1 esp on
parted -s image.hdd mkpart LINUX ext2 63MiB 95MiB
parted -s image.hdd mkpart DATA fat32 95MiB 127MiB

# Prepare directories
rm -rf build/rootfs_esp build/rootfs_ext2 build/rootfs_data
mkdir -p build/rootfs_esp/EFI/BOOT
mkdir -p build/rootfs_esp/boot/limine
mkdir -p build/rootfs_ext2/bin
mkdir -p build/rootfs_ext2/mnt
mkdir -p build/rootfs_data/test_dir
mkdir -p build/rootfs_data/docs

# Populate ESP
cp -v $KERNEL build/rootfs_esp/boot/
cp -v assets/logo.bmp build/rootfs_esp/boot/logo.bmp
cp -v limine.conf limine/limine-bios.sys build/rootfs_esp/boot/limine/
cp -v limine/BOOTX64.EFI limine/BOOTIA32.EFI build/rootfs_esp/EFI/BOOT/
echo "Hello FAT32" > build/rootfs_esp/TEST.TXT

# Populate RootFS (Ext2)
mkdir -p build/rootfs_ext2/boot
cp -v assets/logo.bmp build/rootfs_ext2/boot/logo.bmp
cp -v $USER_BUILD_DIR/user_prog build/rootfs_ext2/bin/prog
cp -v $USER_BUILD_DIR/init build/rootfs_ext2/bin/init
cp -v $USER_BUILD_DIR/shell build/rootfs_ext2/bin/shell
cp -v $USER_BUILD_DIR/ls build/rootfs_ext2/bin/ls
echo "Hello Ext2" > build/rootfs_ext2/test.txt
echo "Hello FAT32" > build/rootfs_ext2/TEST.TXT

# Create ESP Image (Part 1, 62MB)
dd if=/dev/zero of=esp.img bs=1M count=62
mformat -i esp.img -F ::
mcopy -i esp.img -s build/rootfs_esp/* ::/

# Create RootFS Image (Part 2, 32MB)
dd if=/dev/zero of=root.img bs=1M count=32
mkfs.ext2 -b 1024 -d build/rootfs_ext2 -r 1 -N 0 -m 0 -L "ROOT" root.img

# Create Data Image (Part 3, 32MB)
echo "Hello Data Partition" > build/rootfs_data/data_test.txt
echo "This is a file in a subdirectory" > build/rootfs_data/test_dir/subfile.txt
echo "Documentation file" > build/rootfs_data/docs/readme.md

dd if=/dev/zero of=data.img bs=1M count=32
mformat -i data.img -F ::
mcopy -i data.img -s build/rootfs_data/* ::/

# Assemble Image
dd if=esp.img of=image.hdd bs=1M seek=1 conv=notrunc
dd if=root.img of=image.hdd bs=1M seek=63 conv=notrunc
dd if=data.img of=image.hdd bs=1M seek=95 conv=notrunc

./limine/limine bios-install image.hdd
