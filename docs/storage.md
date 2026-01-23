# Storage

## Cache flush

The kernel can flush device write-back caches via `storage_flush()`. Boot log writes call this to persist `/var/log/boot`
before power-off.
