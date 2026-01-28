#include <io/storage.h>
#include <drivers/ahci.h>
#include <drivers/ide.h>
#include <drivers/usb/xhci.h>
#include <task/sleeplock.h>

#define STORAGE_DEVICE_COUNT 3
#define STORAGE_DEVICE_USB 2

enum storage_backend
{
    STORAGE_BACKEND_NONE = 0,
    STORAGE_BACKEND_AHCI,
    STORAGE_BACKEND_IDE,
    STORAGE_BACKEND_USB,
};

struct storage_device
{
    enum storage_backend backend; // Storage backend type.
    uint8_t port;                 // AHCI port index or IDE drive index; unused for USB.
};

static struct storage_device g_devices[STORAGE_DEVICE_COUNT];
static sleeplock_t storage_locks[STORAGE_DEVICE_COUNT];
static bool storage_lock_initialized = false;

static const char *storage_backend_name(const enum storage_backend backend)
{
    switch (backend) {
    case STORAGE_BACKEND_AHCI:
        return "ahci";
    case STORAGE_BACKEND_IDE:
        return "ide";
    case STORAGE_BACKEND_USB:
        return "usb";
    default:
        return "none";
    }
}

void storage_init(void)
{
    sleeplock_init(&storage_locks[0], "storage0");
    sleeplock_init(&storage_locks[1], "storage1");
    sleeplock_init(&storage_locks[2], "storage2");
    storage_lock_initialized = true;

    for (uint8_t i = 0; i < (uint8_t)STORAGE_DEVICE_COUNT; i++) {
        g_devices[i].backend = STORAGE_BACKEND_NONE;
        g_devices[i].port    = 0;
    }

    uint8_t ide_first  = 0xFF;
    uint8_t ide_second = 0xFF;
    for (uint8_t i = 0; i < 4; i++) {
        if (!ide_devices[i].exists) {
            continue;
        }
        if (ide_first == 0xFF) {
            ide_first = i;
        } else {
            ide_second = i;
            break;
        }
    }

    // Default: try AHCI on device 0, fallback to the first IDE drive.
    if (ahci_port_ready()) {
        g_devices[0].backend = STORAGE_BACKEND_AHCI;
        g_devices[0].port    = 0;
    } else if (ide_first != 0xFF) {
        g_devices[0].backend = STORAGE_BACKEND_IDE;
        g_devices[0].port    = ide_first;
    }

    // Device 1: pick the next available IDE drive (other than any already used).
    if (ide_first != 0xFF) {
        if (g_devices[0].backend == STORAGE_BACKEND_IDE) {
            if (ide_second != 0xFF) {
                g_devices[1].backend = STORAGE_BACKEND_IDE;
                g_devices[1].port    = ide_second;
            }
        } else {
            g_devices[1].backend = STORAGE_BACKEND_IDE;
            g_devices[1].port    = ide_first;
        }
    }

    if (xhci_usb_storage_present()) {
        g_devices[STORAGE_DEVICE_USB
            ]
            .
            backend = STORAGE_BACKEND_USB;
    }
}

static int storage_read_backend(const struct storage_device *dev, uint32_t lba, uint8_t count, uint8_t *buffer)
{
    switch (dev->backend) {
    case STORAGE_BACKEND_AHCI:
        return ahci_read(lba, count, buffer);
    case STORAGE_BACKEND_IDE:
        return ide_read_sectors(dev->port, lba, count, buffer);
    case STORAGE_BACKEND_USB:
        return xhci_usb_storage_read(lba, count, buffer);
    default:
        return -1;
    }
}

static int storage_write_backend(const struct storage_device *dev, uint32_t lba, uint8_t count, const uint8_t *buffer)
{
    switch (dev->backend) {
    case STORAGE_BACKEND_AHCI:
        return ahci_write(lba, count, buffer);
    case STORAGE_BACKEND_IDE:
        return ide_write_sectors(dev->port, lba, count, (uint8_t *)buffer);
    case STORAGE_BACKEND_USB:
        return xhci_usb_storage_write(lba, count, buffer);
    default:
        return -1;
    }
}

static int storage_flush_backend(const struct storage_device *dev)
{
    switch (dev->backend) {
    case STORAGE_BACKEND_AHCI:
        return ahci_flush();
    case STORAGE_BACKEND_IDE:
        return ide_flush_cache(dev->port);
    case STORAGE_BACKEND_USB:
    default:
        return -1;
    }
}

int storage_read(uint8_t device, uint32_t lba, uint8_t count, uint8_t *buffer)
{
    if (device >= (sizeof(g_devices) / sizeof(g_devices[0])) || count == 0 || buffer == nullptr) {
        return -1;
    }

    if (storage_lock_initialized) {
        sleeplock_acquire(&storage_locks[device]);
    }

    int result = storage_read_backend(&g_devices[device], lba, count, buffer);

    if (storage_lock_initialized) {
        sleeplock_release(&storage_locks[device]);
    }

    return result;
}

int storage_write(uint8_t device, uint32_t lba, uint8_t count, const uint8_t *buffer)
{
    if (device >= (sizeof(g_devices) / sizeof(g_devices[0])) || count == 0 || buffer == nullptr) {
        return -1;
    }

    if (storage_lock_initialized) {
        sleeplock_acquire(&storage_locks[device]);
    }

    int result = storage_write_backend(&g_devices[device], lba, count, buffer);

    if (storage_lock_initialized) {
        sleeplock_release(&storage_locks[device]);
    }

    return result;
}

int storage_flush(uint8_t device)
{
    if (device >= (sizeof(g_devices) / sizeof(g_devices[0]))) {
        return -1;
    }

    if (storage_lock_initialized) {
        sleeplock_acquire(&storage_locks[device]);
    }

    int result = storage_flush_backend(&g_devices[device]);

    if (storage_lock_initialized) {
        sleeplock_release(&storage_locks[device]);
    }

    return result;
}

uint8_t storage_device_count(void)
{
    return STORAGE_DEVICE_COUNT;
}

bool storage_device_present(uint8_t device)
{
    if (device >= STORAGE_DEVICE_COUNT) {
        return false;
    }

    return g_devices[device].backend != STORAGE_BACKEND_NONE;
}

const char *storage_device_backend_name(uint8_t device)
{
    if (device >= STORAGE_DEVICE_COUNT) {
        return "unknown";
    }

    return storage_backend_name(g_devices[device].backend);
}