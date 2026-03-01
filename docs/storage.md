# Storage

## Cache flush

The kernel can flush device write-back caches via `storage_flush()`. Boot log writes call this to persist
`/var/log/boot`
before power-off.

## Device mapping

- device 0: prefer AHCI port if available, else first IDE drive
- device 1: next available IDE drive (if any)
- device 2: USB mass storage (if detected)

## Boot disk detection

The boot disk is selected by scanning all available storage devices for GPT partitions. The first device with both an
ESP and a Linux Filesystem partition is treated as the boot device. If none is found, the first device with an ESP is
chosen, then the first device with a Linux Filesystem partition, otherwise the first detected device is used as a
fallback. Root, `/mnt`, and `/boot` are mounted from the boot device.

To boot the USB path in QEMU, run `make run-usb` (the USB device uses the same image as `image.hdd`).

## QEMU disk images

The QEMU targets attach these images by default:

- `image.hdd`: GPT with ESP (FAT32), root ext2, and data FAT32
- `image2.ide`: IDE disk with an ext2 partition
- `image3.usb`: USB mass storage disk with an ext2 partition

You can also build a minimal bootable disk with:

- `make small-image`: creates `small-image.hdd` with only ESP (FAT32) + root ext2
- `make run-small-image`: boots QEMU with `small-image.hdd`

The VirtualBox launcher also attaches these images, including `image3.usb` as USB mass storage.

When detected and not the boot device, the USB ext2 partition mounts at `/usb`.
