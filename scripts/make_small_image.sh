#!/bin/bash
set -euo pipefail

KERNEL=$1
_ROOTFS=$2
USER_BUILD_DIR=$3

IMAGE="small-image.hdd"
ESP_IMAGE="small-esp.img"
ROOT_IMAGE="small-root.img"

ESP_DIR="build/rootfs_small_esp"
ROOT_DIR="build/rootfs_small_ext2"

bytes_for_mb() {
    echo $(( $1 * 1024 * 1024 ))
}

bytes_to_ceil_mb() {
    local bytes=$1
    local mb=$((1024 * 1024))
    echo $(( (bytes + mb - 1) / mb ))
}

check_gpt_signature() {
    local image=$1
    local sig
    sig=$(dd if="$image" bs=1 skip=512 count=8 2>/dev/null || true)
    if [[ "$sig" != "EFI PART" ]]; then
        echo "Sanity check failed: missing GPT signature in $image" >&2
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

rm -f "$IMAGE" "$ESP_IMAGE" "$ROOT_IMAGE"
rm -rf "$ESP_DIR" "$ROOT_DIR"

mkdir -p "$ESP_DIR/EFI/BOOT"
mkdir -p "$ESP_DIR/boot/limine"
mkdir -p "$ROOT_DIR/bin"
mkdir -p "$ROOT_DIR/boot"
mkdir -p "$ROOT_DIR/mnt"
mkdir -p "$ROOT_DIR/var/log"

cp -v "$KERNEL" "$ESP_DIR/boot/"
cp -v limine.conf limine/limine-bios.sys "$ESP_DIR/boot/limine/"
cp -v limine/BOOTX64.EFI limine/BOOTIA32.EFI "$ESP_DIR/EFI/BOOT/"

# Install shared libraries to /lib
mkdir -p "$ROOT_DIR/lib"
if [ -f "$USER_BUILD_DIR/libc/libc.so" ]; then
    cp -v "$USER_BUILD_DIR/libc/libc.so" "$ROOT_DIR/lib/libc.so"
fi
if [ -f "$USER_BUILD_DIR/rtld/ld.so" ]; then
    cp -v "$USER_BUILD_DIR/rtld/ld.so" "$ROOT_DIR/lib/ld.so"
fi
if [ -f "$USER_BUILD_DIR/wmlib/libwm.so" ]; then
    cp -v "$USER_BUILD_DIR/wmlib/libwm.so" "$ROOT_DIR/lib/libwm.so"
fi
if [ -f "$USER_BUILD_DIR/elflib/libelf.so" ]; then
    cp -v "$USER_BUILD_DIR/elflib/libelf.so" "$ROOT_DIR/lib/libelf.so"
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
    cp -v "$bin" "$ROOT_DIR/bin/$dest"
done

if [ -f "$USER_BUILD_DIR/wm/main" ]; then
    cp -v "$USER_BUILD_DIR/wm/main" "$ROOT_DIR/bin/wm"
fi

echo "Hello Ext2" > "$ROOT_DIR/test.txt"

esp_payload_bytes=$(du -sb "$ESP_DIR" | awk '{print $1}')
root_payload_bytes=$(du -sb "$ROOT_DIR" | awk '{print $1}')

# Keep images tiny but leave enough space for filesystem metadata.
esp_target_bytes=$((esp_payload_bytes * 2 + 2 * 1024 * 1024))
root_target_bytes=$((root_payload_bytes + root_payload_bytes / 2 + 4 * 1024 * 1024))

ESP_SIZE_MB=$(bytes_to_ceil_mb "$esp_target_bytes")
ROOT_SIZE_MB=$(bytes_to_ceil_mb "$root_target_bytes")
if (( ESP_SIZE_MB < 33 )); then
    ESP_SIZE_MB=33
fi
if (( ROOT_SIZE_MB < 12 )); then
    ROOT_SIZE_MB=12
fi

ESP_START_MB=1
ESP_END_MB=$((ESP_START_MB + ESP_SIZE_MB))
ROOT_START_MB=$ESP_END_MB
ROOT_END_MB=$((ROOT_START_MB + ROOT_SIZE_MB))
DISK_SIZE_MB=$((ROOT_END_MB + 1))

dd if=/dev/zero of="$IMAGE" bs=1M count="$DISK_SIZE_MB"
parted -s "$IMAGE" mklabel gpt
parted -s "$IMAGE" mkpart ESP fat32 "${ESP_START_MB}MiB" "${ESP_END_MB}MiB"
parted -s "$IMAGE" set 1 esp on
parted -s "$IMAGE" mkpart LINUX ext2 "${ROOT_START_MB}MiB" "${ROOT_END_MB}MiB"

dd if=/dev/zero of="$ESP_IMAGE" bs=1M count="$ESP_SIZE_MB"
mformat -i "$ESP_IMAGE" -F ::
mcopy -i "$ESP_IMAGE" -s "$ESP_DIR"/* ::/

dd if=/dev/zero of="$ROOT_IMAGE" bs=1M count="$ROOT_SIZE_MB"
mkfs.ext2 -b 1024 -d "$ROOT_DIR" -r 1 -N 0 -m 0 -L "ROOT" "$ROOT_IMAGE"

dd if="$ESP_IMAGE" of="$IMAGE" bs=1M seek="$ESP_START_MB" conv=notrunc
dd if="$ROOT_IMAGE" of="$IMAGE" bs=1M seek="$ROOT_START_MB" conv=notrunc

./limine/limine bios-install "$IMAGE"

check_gpt_signature "$IMAGE"
check_ext2_superblock "$IMAGE" "$(bytes_for_mb "$ROOT_START_MB")"
