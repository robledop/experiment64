#!/usr/bin/env bash
set -euo pipefail

# Default target disk and image. Override with DISK=/dev/xyz IMAGE=path.img
DISK="${DISK:-/dev/sdb}"
IMAGE="${IMAGE:-image.hdd}"

if [ ! -b "$DISK" ]; then
	echo "Error: $DISK is not a block device" >&2
	exit 1
fi

if [ ! -f "$IMAGE" ]; then
	echo "Error: image file '$IMAGE' not found" >&2
	echo "Hint: run 'make image.hdd' first." >&2
	exit 1
fi

echo "Writing $IMAGE to $DISK"

# Best effort unmount any mounted partitions.
sudo umount "${DISK}"?* 2>/dev/null || true

# Wipe existing signatures and write the image.
sudo wipefs -a "$DISK"
sudo dd if="$IMAGE" of="$DISK" bs=1M conv=fsync status=progress

# Install limine BIOS stages onto the target disk (image already contains limine files).
if [ -x limine/limine ]; then
	sudo limine/limine bios-install "$DISK"
else
	echo "Warning: limine installer not found at limine/limine; skipping bios-install" >&2
fi

sync
echo "Done."
