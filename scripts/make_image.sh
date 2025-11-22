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
parted -s image.hdd mkpart DATA fat32 63MiB 95MiB
parted -s image.hdd mkpart LINUX ext2 95MiB 100%
dd if=/dev/zero of=part.img bs=1M count=62
mformat -i part.img -F ::
rm -rf $ROOTFS
mkdir -p $ROOTFS/EFI/BOOT
mkdir -p $ROOTFS/boot/limine
mkdir -p $ROOTFS/bin
cp -v $KERNEL $ROOTFS/boot/
cp -v assets/logo.bmp $ROOTFS/boot/logo.bmp
cp -v limine.conf limine/limine-bios.sys $ROOTFS/boot/limine/
cp -v limine/BOOTX64.EFI limine/BOOTIA32.EFI $ROOTFS/EFI/BOOT/
cp -v $USER_BUILD_DIR/user_prog $ROOTFS/bin/prog
cp -v $USER_BUILD_DIR/init $ROOTFS/bin/init
cp -v $USER_BUILD_DIR/shell $ROOTFS/bin/shell
cp -v $USER_BUILD_DIR/ls $ROOTFS/bin/ls
echo "Hello FAT32" > $ROOTFS/test.txt
mcopy -i part.img -s $ROOTFS/* ::/
dd if=part.img of=image.hdd bs=1M seek=1 conv=notrunc
./limine/limine bios-install image.hdd
