# Storage

## Cache flush

The kernel can flush device write-back caches via `storage_flush()`. Boot log writes call this to persist `/var/log/boot`
before power-off.

## QEMU disk images

The QEMU targets attach these images by default:

- `image.hdd`: GPT with ESP (FAT32), root ext2, and data FAT32
- `image2.ide`: IDE disk with an ext2 partition
- `image3.usb`: USB mass storage disk with an ext2 partition
