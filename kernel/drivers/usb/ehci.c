#include <drivers/usb/ehci.h>
#include <drivers/pci.h>
#include <drivers/terminal.h>
#include <drivers/tsc.h>
#include <lib/string.h>
#include <mem/vmm.h>
#include <stdint.h>

#define EHCI_CAPLENGTH 0x00u // Capability register length offset.
#define EHCI_HCCPARAMS 0x08u // Host controller capability parameters offset.

#define EHCI_OP_USBCMD 0x00u // USBCMD operational register offset.
#define EHCI_OP_USBSTS 0x04u // USBSTS operational register offset.
#define EHCI_OP_USBINTR 0x08u // USBINTR operational register offset.

#define EHCI_USBCMD_RUN (1u << 0) // Run/Stop command bit.
#define EHCI_USBSTS_HCH (1u << 12) // Host controller halted status bit.

#define EHCI_LEGACY_CAP_ID 0x01u // USB legacy support capability ID.
#define EHCI_LEGSUP_BIOS_OWNED (1u << 16) // BIOS owned semaphore bit.
#define EHCI_LEGSUP_OS_OWNED (1u << 24) // OS owned semaphore bit.

static bool g_ehci_quiesced = false;

static inline uint32_t ehci_mmio_read32(const volatile uint8_t *base, const uint32_t offset)
{
    auto reg = (const volatile uint32_t *)(base + offset);
    uint32_t value;
    __asm__ volatile ("mov %0, %1" : "=r"(value) : "m"(*reg));
    return value;
}

static inline void ehci_mmio_write32(volatile uint8_t *base, const uint32_t offset, const uint32_t value)
{
    auto reg = (volatile uint32_t *)(base + offset);
    __asm__ volatile ("mov %0, %1" : "=m"(*reg) : "r"(value) : "memory");
}

static uint32_t ehci_pci_read32(const struct pci_device *device, const uint8_t offset)
{
    const uint16_t low  = pci_config_read_word(device->bus, device->slot, device->function, offset);
    const uint16_t high = pci_config_read_word(device->bus, device->slot, device->function, offset + 2u);
    return (uint32_t)low | ((uint32_t)high << 16);
}

static void ehci_pci_write32(const struct pci_device *device, const uint8_t offset, const uint32_t value)
{
    pci_config_write_word(device->bus, device->slot, device->function, offset, (uint16_t)value);
    pci_config_write_word(device->bus, device->slot, device->function, offset + 2u, (uint16_t)(value >> 16));
}

static void ehci_legacy_handoff(const struct pci_device *device, const uint8_t eecp)
{
    uint8_t offset = eecp;
    for (uint32_t i = 0; offset != 0 && i < 32; i++) {
        const uint32_t cap   = ehci_pci_read32(device, offset);
        const uint8_t cap_id = (uint8_t)(cap & 0xFFu);
        const uint8_t next   = (uint8_t)((cap >> 8) & 0xFFu);

        if (cap_id == EHCI_LEGACY_CAP_ID) {
            uint32_t legsup = cap;
            if (legsup & EHCI_LEGSUP_BIOS_OWNED) {
                ehci_pci_write32(device, offset, legsup | EHCI_LEGSUP_OS_OWNED);
                for (uint32_t wait = 0; wait < 100; wait++) {
                    legsup = ehci_pci_read32(device, offset);
                    if ((legsup & EHCI_LEGSUP_BIOS_OWNED) == 0) {
                        break;
                    }
                    tsc_sleep_ms(1);
                }

                if (legsup & EHCI_LEGSUP_BIOS_OWNED) {
                    boot_message(WARNING,
                                 "[EHCI] BIOS handoff timeout bus=%u slot=%u func=%u",
                                 device->bus,
                                 device->slot,
                                 device->function);
                }
            }

            ehci_pci_write32(device, offset + 4u, 0);
            return;
        }

        offset = next;
    }
}

static void ehci_quiesce_device(const struct pci_device *device)
{
    const uint32_t bar0 = device->bars[0];
    if (bar0 == 0 || (bar0 & PCI_BAR_IO)) {
        return;
    }

    uint64_t base = (uint64_t)(bar0 & ~0xFULL);
    if (((bar0 >> 1) & 0x3u) == PCI_BAR_MEMORY_TYPE_64) {
        const uint32_t bar1 = device->bars[1];
        base                |= (uint64_t)bar1 << 32;
    }

    if (base == 0)
        return;

    auto mmio                = (volatile uint8_t *)(base + g_hhdm_offset);
    const uint32_t cap       = ehci_mmio_read32(mmio, EHCI_CAPLENGTH);
    const uint8_t cap_length = (uint8_t)(cap & 0xFFu);
    const uint32_t hccparams = ehci_mmio_read32(mmio, EHCI_HCCPARAMS);
    const uint8_t eecp       = (uint8_t)((hccparams >> 8) & 0xFFu);

    if (eecp != 0) {
        ehci_legacy_handoff(device, eecp);
    }

    auto op_base    = (volatile uint8_t *)(mmio + cap_length);
    uint32_t usbcmd = ehci_mmio_read32(op_base, EHCI_OP_USBCMD);
    if (usbcmd & EHCI_USBCMD_RUN) {
        usbcmd &= ~EHCI_USBCMD_RUN;
        ehci_mmio_write32(op_base, EHCI_OP_USBCMD, usbcmd);
        for (uint32_t wait = 0; wait < 200; wait++) {
            const uint32_t usbsts = ehci_mmio_read32(op_base, EHCI_OP_USBSTS);
            if (usbsts & EHCI_USBSTS_HCH) {
                break;
            }
            tsc_sleep_ms(1);
        }
    }

    ehci_mmio_write32(op_base, EHCI_OP_USBINTR, 0);

    uint16_t cmd = pci_config_read_word(device->bus, device->slot, device->function, 0x04);
    cmd          &= ~(PCI_COMMAND_BUS_MASTER | PCI_COMMAND_MEMORY | PCI_COMMAND_IO);
    pci_config_write_word(device->bus, device->slot, device->function, 0x04, cmd);
}

static bool ehci_probe_device(const uint8_t bus, const uint8_t slot, const uint8_t function, struct pci_device *dev)
{
    const uint16_t vendor_id = pci_config_read_word(bus, slot, function, 0x00);
    if (vendor_id == 0xFFFF) {
        return false;
    }

    const uint16_t device_id = pci_config_read_word(bus, slot, function, 0x02);
    const uint16_t class_sub = pci_config_read_word(bus, slot, function, 0x0A);
    const uint16_t rev_prog  = pci_config_read_word(bus, slot, function, 0x08);
    const uint8_t class_code = (uint8_t)(class_sub >> 8);
    const uint8_t subclass   = (uint8_t)(class_sub & 0xFFu);
    const uint8_t prog_if    = (uint8_t)(rev_prog >> 8);

    if (class_code != 0x0C || subclass != 0x03 || prog_if != 0x20) {
        return false;
    }

    if (dev) {
        memset(dev, 0, sizeof(*dev));
        dev->bus       = bus;
        dev->slot      = slot;
        dev->function  = function;
        dev->vendor_id = vendor_id;
        dev->device_id = device_id;
        dev->class     = class_code;
        dev->subclass  = subclass;
        dev->prog_if   = prog_if;

        for (uint8_t i = 0; i < 6; i++) {
            dev->bars[i] = ehci_pci_read32(dev, (uint8_t)(0x10u + (i * 4u)));
        }
    }

    return true;
}

void ehci_quiesce_all(void)
{
    if (g_ehci_quiesced) {
        return;
    }

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t function = 0; function < 8; function++) {
                struct pci_device dev = {0};
                if (ehci_probe_device((uint8_t)bus, slot, function, &dev)) {
                    boot_message(INFO,
                                 "[EHCI] Quiescing controller %04x:%04x bus=%u slot=%u func=%u",
                                 dev.vendor_id,
                                 dev.device_id,
                                 dev.bus,
                                 dev.slot,
                                 dev.function);
                    ehci_quiesce_device(&dev);
                }
            }
        }
    }

    g_ehci_quiesced = true;
}
