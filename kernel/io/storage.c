#include "storage.h"
#include "ahci.h"
#include "ide.h"
#include <stddef.h>

enum storage_backend
{
    STORAGE_BACKEND_NONE = 0,
    STORAGE_BACKEND_AHCI,
    STORAGE_BACKEND_IDE,
};

struct storage_device
{
    enum storage_backend backend;
    uint8_t port; // AHCI port index or IDE drive index depending on backend
};

static struct storage_device g_devices[2];

void storage_init(void)
{
    // Default: try AHCI on device 0, fallback to IDE drive 0.
    if (ahci_port_ready())
    {
        g_devices[0].backend = STORAGE_BACKEND_AHCI;
        g_devices[0].port = 0;
    }
    else
    {
        g_devices[0].backend = STORAGE_BACKEND_IDE;
        g_devices[0].port = 0;
    }

    // Device 1: pick the first available IDE drive (other than any already used).
    g_devices[1].backend = STORAGE_BACKEND_NONE;
    g_devices[1].port = 0;
    for (uint8_t i = 0; i < 4; i++)
    {
        if (ide_devices[i].exists)
        {
            // If device0 is also IDE and already uses this index, skip it.
            if (g_devices[0].backend == STORAGE_BACKEND_IDE && g_devices[0].port == i)
                continue;
            g_devices[1].backend = STORAGE_BACKEND_IDE;
            g_devices[1].port = i;
            break;
        }
    }
}

static int storage_read_backend(const struct storage_device *dev, uint32_t lba, uint8_t count, uint8_t *buffer)
{
    switch (dev->backend)
    {
    case STORAGE_BACKEND_AHCI:
        return ahci_read(lba, count, buffer);
    case STORAGE_BACKEND_IDE:
        return ide_read_sectors(dev->port, lba, count, buffer);
    default:
        return -1;
    }
}

static int storage_write_backend(const struct storage_device *dev, uint32_t lba, uint8_t count, const uint8_t *buffer)
{
    switch (dev->backend)
    {
    case STORAGE_BACKEND_AHCI:
        return ahci_write(lba, count, buffer);
    case STORAGE_BACKEND_IDE:
        return ide_write_sectors(dev->port, lba, count, (uint8_t *)buffer);
    default:
        return -1;
    }
}

int storage_read(uint8_t device, uint32_t lba, uint8_t count, uint8_t *buffer)
{
    if (device >= (sizeof(g_devices) / sizeof(g_devices[0])) || count == 0 || buffer == nullptr)
        return -1;
    return storage_read_backend(&g_devices[device], lba, count, buffer);
}

int storage_write(uint8_t device, uint32_t lba, uint8_t count, const uint8_t *buffer)
{
    if (device >= (sizeof(g_devices) / sizeof(g_devices[0])) || count == 0 || buffer == nullptr)
        return -1;
    return storage_write_backend(&g_devices[device], lba, count, buffer);
}
