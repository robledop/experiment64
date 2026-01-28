#include <drivers/pci.h>
#include <arch/x86_64/port_io.h>
#include <drivers/terminal.h>
#include <drivers/ahci.h>
#include <drivers/atl1c.h>
#include <drivers/e1000.h>
#include <drivers/ide.h>
#include <drivers/usb/xhci.h>
#include <stddef.h>
#include <lib/string.h>
#include <attributes.h>

// Type 0x00: A general device
struct pci_header
{
    uint16_t vendor_id;
    uint16_t device_id;
    // Provides control over a device's ability to generate and respond to PCI cycles.
    // Where the only functionality guaranteed to be supported by all devices is, when a 0 is written to this register,
    // the device is disconnected from the PCI bus for all accesses except Configuration Space access.
    uint16_t command;
    uint16_t status;
    uint8_t revision_id;
    // (Programming Interface Byte): A read-only register that specifies a register-level programming interface
    // the device has, if it has any at all.
    uint8_t prog_if;
    // A read-only register that specifies the specific function the device performs.
    uint8_t subclass;
    // A read-only register that specifies the type of function the device performs.
    uint8_t class;
    // Specifies the system cache line size in 32-bit units. A device can limit the number of cache line sizes it can
    // support, if a unsupported value is written to this field, the device will behave as if a value of 0 was written.
    uint8_t cache_line_size;
    // Specifies the latency timer in units of PCI bus clocks
    uint8_t latency_timer;
    // Identifies the layout of the rest of the header beginning at byte 0x10 of the header. If bit 7 of this register
    // is set, the device has multiple functions; otherwise, it is a single function device. Types:
    // 0x0: a general device
    // 0x1: a PCI-to-PCI bridge
    // 0x2: a PCI-to-CardBus bridge.
    uint8_t header_type;
    // Represents that status and allows control of a devices BIST (built-in self test).
    uint8_t BIST;
    uint32_t bars[6];
    uint32_t cardbus_cis_pointer;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    uint32_t expansion_rom_base_address;
    uint8_t capabilities_pointer;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t reserved2;
    uint8_t irq;
    uint8_t interrupt_pin;
    uint8_t min_grant;
    uint8_t max_latency;
} __attribute__((packed));

// https://wiki.osdev.org/PCI

struct pci_class classes[] = {
    {0x00, 0x00, "Non-VGA-Compatible Unclassified Device"},
    {0x00, 0x01, "VGA-Compatible Unclassified Device"},
    {0x01, 0x00, "SCSI Bus Controller"},
    {0x01, 0x01, "IDE Controller"},
    {0x01, 0x02, "Floppy Disk Controller"},
    {0x01, 0x03, "IPI Bus Controller"},
    {0x01, 0x04, "RAID Controller"},
    {0x01, 0x05, "ATA Controller"},
    {0x01, 0x06, "Serial ATA Controller"},
    {0x01, 0x07, "Serial Attached SCSI Controller"},
    {0x01, 0x08, "Non-Volatile Memory Controller"},
    {0x01, 0x80, "Other Mass Storage Controller"},
    {0x02, 0x00, "Ethernet Controller"},
    {0x02, 0x01, "Token Ring Controller"},
    {0x02, 0x02, "FDDI Controller"},
    {0x02, 0x03, "ATM Controller"},
    {0x02, 0x04, "ISDN Controller"},
    {0x02, 0x05, "WorldFip Controller"},
    {0x02, 0x06, "PICMG 2.14 Multi Computing Controller"},
    {0x02, 0x07, "Infiniband Controller"},
    {0x02, 0x08, "Fabric Controller"},
    {0x02, 0x80, "Other Network Controller"},
    {0x03, 0x00, "VGA Compatible Controller"},
    {0x03, 0x01, "XGA Controller"},
    {0x03, 0x02, "3D Controller (Not VGA-Compatible)"},
    {0x03, 0x80, "Other Display Controller"},
    {0x04, 0x00, "Multimedia Video Controller"},
    {0x04, 0x01, "Multimedia Audio Controller"},
    {0x04, 0x02, "Computer Telephony Device"},
    {0x04, 0x03, "Audio Device"},
    {0x04, 0x80, "Other Multimedia Controller"},
    {0x05, 0x00, "RAM Controller"},
    {0x05, 0x01, "Flash Controller"},
    {0x05, 0x80, "Other Memory Controller"},
    {0x06, 0x00, "Host Bridge"},
    {0x06, 0x01, "ISA Bridge"},
    {0x06, 0x02, "EISA Bridge"},
    {0x06, 0x03, "MCA Bridge"},
    {0x06, 0x04, "PCI-to-PCI Bridge"},
    {0x06, 0x05, "PCMCIA Bridge"},
    {0x06, 0x06, "NuBus Bridge"},
    {0x06, 0x07, "CardBus Bridge"},
    {0x06, 0x08, "RACEway Bridge"},
    {0x06, 0x09, "PCI-to-PCI Bridge"},
    {0x06, 0x0A, "Infiniband-to-PCI Host Bridge"},
    {0x06, 0x80, "Other Bridge"},
    {0x07, 0x00, "Serial Controller"},
    {0x07, 0x01, "Parallel Controller"},
    {0x07, 0x02, "Multiport Serial Controller"},
    {0x07, 0x03, "Modem"},
    {0x07, 0x04, "IEEE 488.1/2 (GPIB) Controller"},
    {0x07, 0x05, "Smart Card Controller"},
    {0x07, 0x80, "Other Simple Communication Controller"},
    {0x08, 0x00, "PIC"},
    {0x08, 0x01, "DMA Controller"},
    {0x08, 0x02, "Timer"},
    {0x08, 0x03, "RTC Controller"},
    {0x08, 0x04, "PCI Hot-Plug Controller"},
    {0x08, 0x05, "SD Host Controller"},
    {0x08, 0x07, "IOMMU"},
    {0x08, 0x80, "Other Base System Peripheral"},
    {0x09, 0x00, "Keyboard Controller"},
    {0x09, 0x01, "Digitizer Pen"},
    {0x09, 0x02, "Mouse Controller"},
    {0x09, 0x03, "Scanner Controller"},
    {0x09, 0x04, "Gameport Controller"},
    {0x09, 0x80, "Other Input Device Controller"},
    {0x0A, 0x00, "Generic Docking Station"},
    {0x0A, 0x80, "Other Docking Station"},
    {0x0B, 0x00, "386 Processor"},
    {0x0B, 0x01, "486 Processor"},
    {0x0B, 0x02, "Pentium Processor"},
    {0x0B, 0x03, "Pentium Pro Processor"},
    {0x0B, 0x10, "Alpha Processor"},
    {0x0B, 0x20, "PowerPC Processor"},
    {0x0B, 0x30, "MIPS Processor"},
    {0x0B, 0x40, "Co-Processor"},
    {0x0B, 0x80, "Other Processor"},
    {0x0C, 0x00, "FireWire (IEEE 1394) Controller"},
    {0x0C, 0x01, "ACCESS Bus Controller"},
    {0x0C, 0x02, "SSA"},
    {0x0C, 0x03, "USB Controller"},
    {0x0C, 0x04, "Fibre Channel"},
    {0x0C, 0x05, "SMBus Controller"},
    {0x0C, 0x06, "InfiniBand Controller"},
    {0x0C, 0x07, "IPMI Interface"},
    {0x0C, 0x08, "SERCOS Interface (IEC 61491)"},
    {0x0C, 0x09, "CANbus Controller"},
    {0x0C, 0x80, "Other Serial Bus Controller"},
    {0x0D, 0x00, "iRDA Compatible Controller"},
    {0x0D, 0x00, "Consumer IR Controller"},
    {0x0D, 0x00, "RF Controller"},
    {0x0D, 0x00, "Bluetooth Controller"},
    {0x0D, 0x00, "Broadband Controller"},
    {0x0D, 0x00, "Ethernet Controller (802.1a)"},
    {0x0D, 0x00, "Ethernet Controller (802.1b)"},
    {0x0D, 0x00, "Other Wireless Controller"},
    {0x0E, 0x00, "I20"},
    {0x0F, 0x01, "Satellite TV Controller"},
    {0x0F, 0x02, "Satellite Audio Controller"},
    {0x0F, 0x03, "Satellite Voice Controller"},
    {0x0F, 0x04, "Satellite Data Controller"},
    {0x10, 0x00, "Network and Computing Encryption/Decryption"},
    {0x10, 0x10, "Entertainment Encryption/Decryption"},
    {0x10, 0x80, "Other Encryption Controller"},
    {0x11, 0x00, "DPIO Modules"},
    {0x11, 0x01, "Performance Counters"},
    {0x11, 0x10, "Communication Synchronizer"},
    {0x11, 0x20, "Signal Processing Management"},
    {0x11, 0x80, "Other Signal Processing Controller"},
    {0x12, 0x00, "Processing Accelerator"},
    {0x13, 0x00, "Non-Essential Instrumentation"},
    {0x40, 0x00, "Co-Processor"},
    {0xFF, 0x00, "Vendor Specific"},
};

struct pci_vendor vendors[] = {
    {0x8086, "Intel Corporation"},
    {0x10DE, "NVIDIA Corporation"},
    {0x1022, "Advanced Micro Devices, Inc."},
    {0x1002, "Advanced Micro Devices, Inc."},
    {0x1234, "QEMU"},
    {0x46F4, "QEMU"},
    {0x1AF4, "Red Hat, Inc."},
    {0x1D6B, "Linux Foundation"},
    {0x80EE, "Oracle Corporation"},
    {0x1AB8, "Innotek GmbH"},
    {0x80EE, "Oracle Corporation"},
    {0x1D00, "XenSource"},
    {0x1414, "Microsoft Corporation"},
    {0x10EC, "Realtek Semiconductor Co."},
    {0x1969, "Qualcomm Atheros"},
    {0x0781, "SanDisk Corp"},
};

#define PCI_TRACE_VENDOR_ID 0x1969
#define PCI_TRACE_DEVICE_ID 0x1090

static void pci_ide_init(struct pci_device device)
{
    (void)device;
    ide_init();
}

struct pci_driver pci_drivers[] = {
    {.class = 0x01, .subclass = 0x06, .vendor_id = PCI_ANY_ID, .device_id = PCI_ANY_ID, .init = &ahci_init},
    {.class = 0x01, .subclass = 0x01, .vendor_id = PCI_ANY_ID, .device_id = PCI_ANY_ID, .init = &pci_ide_init},
    {.class = 0x0C, .subclass = 0x03, .vendor_id = PCI_ANY_ID, .device_id = PCI_ANY_ID, .init = &xhci_init},
    // Intel e1000 network controller (QEMU, Bochs, VirtualBox)
    {.class = 0x02, .subclass = 0x00, .vendor_id = INTEL_VEND, .device_id = E1000_DEV, .init = &e1000_init},
    // Qualcomm Atheros AR8162 Fast Ethernet
    {.class = 0x02, .subclass = 0x00, .vendor_id = ATHEROS_VEND, .device_id = AR8162_DEV, .init = &atl1c_init},
};

/**
 * @brief Read a 16-bit word from PCI configuration space.
 *
 * @param bus PCI bus number.
 * @param slot Device slot number.
 * @param func Function number.
 * @param offset Register offset.
 * @return Word read from configuration space.
 */
uint16_t pci_config_read_word(const uint8_t bus, const uint8_t slot, const uint8_t func, const uint8_t offset)
{
    const uint32_t lbus  = bus;
    const uint32_t lslot = slot;
    const uint32_t lfunc = func;
    uint16_t tmp         = 0;

    // Create configuration address
    // Bit 31     | Bits 30-24 | Bits 23-16 | Bits 15-11    | Bits 10-8       | Bits 7-0
    // Enable Bit | Reserved   | Bus Number | Device Number | Function Number | Register Offset1
    const uint32_t address = lbus << 16 | lslot << 11 | lfunc << 8 | (offset & 0xFC) | 0x80000000;

    // Write out the address
    outl(PCI_CONFIG_ADDRESS, address);

    // Read in the data
    // (offset & 2) * 8) = 0 will choose the first word of the 32-bit register
    tmp = (uint16_t)((inl(PCI_CONFIG_DATA) >> ((offset & 2) * 8)) & 0xFFFF);
    return tmp;
}

/**
 * @brief Write a 16-bit word to PCI configuration space.
 */
void pci_config_write_word(const uint8_t bus, const uint8_t slot, const uint8_t func, const uint8_t offset,
                           const uint16_t data)
{
    const uint32_t lbus  = bus;
    const uint32_t lslot = slot;
    const uint32_t lfunc = func;

    const uint32_t address = (0x80000000 | (lbus << 16) | (lslot << 11) | (lfunc << 8) | (offset & 0xFC));

    outl(PCI_CONFIG_ADDRESS, address);

    uint32_t tmp = inl(PCI_CONFIG_DATA);

    if (offset & 2) {
        tmp = (tmp & 0x0000FFFF) | (data << 16); // Modify the upper 16 bits
    } else {
        tmp = (tmp & 0xFFFF0000) | data; // Modify the lower 16 bits
    }

    outl(PCI_CONFIG_DATA, tmp);
}


/**
 * @brief Resolve a PCI class/subclass pair to a descriptive name.
 */
const char *pci_find_name(const uint8_t class, const uint8_t subclass)
{
    for (size_t i = 0; i < sizeof(classes) / sizeof(struct pci_class); i++) {
        if (classes[i].class == class && classes[i].subclass == subclass) {
            return classes[i].name;
        }
    }
    return "Unknown PCI Device";
}

/**
 * @brief Resolve a vendor ID to a descriptive name.
 */
const char *pci_find_vendor(const uint16_t vendor_id)
{
    for (size_t i = 0; i < sizeof(vendors) / sizeof(struct pci_vendor); i++) {
        if (vendors[i].id == vendor_id) {
            return vendors[i].name;
        }
    }
    return "Unknown Vendor";
}

static UNUSED const char *pci_cap_name(const uint8_t cap_id)
{
    switch (cap_id) {
    case 0x01:
        return "Power Management";
    case 0x05:
        return "MSI";
    case 0x10:
        return "PCI Express";
    case 0x11:
        return "MSI-X";
    default:
        return "Unknown Capability";
    }
}

static const char *pci_usb_prog_if_name(const uint8_t prog_if)
{
    switch (prog_if) {
    case 0x30:
        return "XHCI";
    case 0x80:
        return "Unspecified";
    case 0xFE:
        return "USB Device";
    default:
        return "Unsupported";
    }
}

static UNUSED void pci_dump_bars(const struct pci_header *pci)
{
    bool logged = false;
    for (uint8_t i = 0; i < 6; i++) {
        const uint32_t bar = pci->bars[i];
        if (bar == 0) {
            continue;
        }

        logged = true;
        if (bar & PCI_BAR_IO) {
            boot_message(INFO,
                         "  BAR%u: IO base=0x%08x raw=0x%08x",
                         i,
                         bar & ~0x3U,
                         bar);
            continue;
        }

        const uint8_t mem_type = (bar >> 1) & 0x3;
        const bool prefetch    = (bar & 0x8) != 0;
        if (mem_type == PCI_BAR_MEMORY_TYPE_64 && i + 1 < 6) {
            const uint32_t bar_hi = pci->bars[i + 1];
            const uint64_t base   = ((uint64_t)bar_hi << 32) | (bar & ~0xFULL);
            boot_message(INFO,
                         "  BAR%u: MEM64 base=0x%016lx raw=0x%08x%08x prefetch=%u",
                         i,
                         (unsigned long)base,
                         bar_hi,
                         bar,
                         prefetch ? 1U : 0U);
            i++;
            continue;
        }

        boot_message(INFO,
                     "  BAR%u: MEM32 base=0x%08x raw=0x%08x prefetch=%u",
                     i,
                     bar & ~0xFULL,
                     bar,
                     prefetch ? 1U : 0U);
    }

    if (!logged) {
        boot_message(INFO, "  BARs: none");
    }
}

static UNUSED void pci_dump_capabilities(const uint8_t bus,
                                         const uint8_t device,
                                         const uint8_t function,
                                         const struct pci_header *pci)
{
    if ((pci->status & PCI_STATUS_CAPABILITIES_LIST) == 0) {
        boot_message(INFO, "  caps: none");
        return;
    }

    uint8_t cap_ptr = pci->capabilities_pointer;
    if (cap_ptr < 0x40) {
        boot_message(WARNING, "  caps: invalid pointer 0x%02x", cap_ptr);
        return;
    }

    boot_message(INFO, "  caps:");
    for (uint8_t i = 0; cap_ptr != 0 && i < 48; i++) {
        const uint16_t cap_header = pci_config_read_word(bus, device, function, cap_ptr);
        const uint8_t cap_id      = (uint8_t)(cap_header & 0xFF);
        const uint8_t next_ptr    = (uint8_t)((cap_header >> 8) & 0xFF);

        boot_message(INFO,
                     "    0x%02x %s (id=0x%02x)",
                     cap_ptr,
                     pci_cap_name(cap_id),
                     cap_id);

        if (next_ptr == cap_ptr) {
            boot_message(WARNING, "  caps: loop at 0x%02x", cap_ptr);
            break;
        }

        if (next_ptr != 0 && next_ptr < 0x40) {
            boot_message(WARNING, "  caps: invalid next 0x%02x", next_ptr);
            break;
        }

        cap_ptr = next_ptr;
    }
}

static UNUSED void pci_dump_config_space(const uint8_t bus, const uint8_t device, const uint8_t function)
{
    for (uint16_t offset = 0; offset < 0x100; offset += 0x10) {
        const uint16_t w0 = pci_config_read_word(bus, device, function, (uint8_t)(offset + 0x0));
        const uint16_t w1 = pci_config_read_word(bus, device, function, (uint8_t)(offset + 0x2));
        const uint16_t w2 = pci_config_read_word(bus, device, function, (uint8_t)(offset + 0x4));
        const uint16_t w3 = pci_config_read_word(bus, device, function, (uint8_t)(offset + 0x6));
        const uint16_t w4 = pci_config_read_word(bus, device, function, (uint8_t)(offset + 0x8));
        const uint16_t w5 = pci_config_read_word(bus, device, function, (uint8_t)(offset + 0xA));
        const uint16_t w6 = pci_config_read_word(bus, device, function, (uint8_t)(offset + 0xC));
        const uint16_t w7 = pci_config_read_word(bus, device, function, (uint8_t)(offset + 0xE));

        boot_message(INFO,
                     "  cfg[%02x]: %04x %04x %04x %04x %04x %04x %04x %04x",
                     (unsigned)offset,
                     w0,
                     w1,
                     w2,
                     w3,
                     w4,
                     w5,
                     w6,
                     w7);
    }
}

static UNUSED void pci_dump_device_config(const struct pci_header *pci,
                                          const uint8_t bus,
                                          const uint8_t device,
                                          const uint8_t function)
{
    boot_message(INFO,
                 "PCI %02x:%02x.%u vendor=0x%04x device=0x%04x",
                 bus,
                 device,
                 function,
                 pci->vendor_id,
                 pci->device_id);
    boot_message(INFO,
                 "  class=0x%02x subclass=0x%02x prog_if=0x%02x rev=0x%02x",
                 pci->class,
                 pci->subclass,
                 pci->prog_if,
                 pci->revision_id);
    boot_message(INFO,
                 "  command=0x%04x status=0x%04x header=0x%02x",
                 pci->command,
                 pci->status,
                 pci->header_type);
    boot_message(INFO,
                 "  subsystem=0x%04x:0x%04x irq=0x%02x pin=0x%02x",
                 pci->subsystem_vendor_id,
                 pci->subsystem_id,
                 pci->irq,
                 pci->interrupt_pin);
    boot_message(INFO,
                 "  cache_line=%u latency=%u bist=0x%02x",
                 pci->cache_line_size,
                 pci->latency_timer,
                 pci->BIST);

    pci_dump_bars(pci);
    pci_dump_capabilities(bus, device, function, pci);
    pci_dump_config_space(bus, device, function);
}

/**
 * @brief Attempt to load a driver matching the given PCI header.
 */
void load_driver(const struct pci_header pci, const uint8_t bus, const uint8_t device, const uint8_t function)
{
    struct pci_device dev = {
        .bus = bus,
        .slot = device,
        .function = function,
        .vendor_id = pci.vendor_id,
        .device_id = pci.device_id,
        .class = pci.class,
        .subclass = pci.subclass,
        .prog_if = pci.prog_if,
        .header_type = pci.header_type,
        .irq = pci.irq,
    };
    memcpy(dev.bars, pci.bars, sizeof(dev.bars));


    // Uncomment this if you need to dump information about a device
    // if (pci.vendor_id == PCI_TRACE_VENDOR_ID && pci.device_id == PCI_TRACE_DEVICE_ID)
    // {
    //     pci_dump_device_config(&pci, bus, device, function);
    // }

    boot_message(INFO, "%s", pci_find_name(dev.class, dev.subclass));

    // {0x02, 0x00, "Ethernet Controller"},
    if (dev.class == 0x02 && dev.subclass == 0x00) {
        boot_message(INFO,
                     "Ethernet controller Vendor: %s (0x%04X), Device: 0x%04X",
                     pci_find_vendor(dev.vendor_id),
                     dev.vendor_id,
                     dev.device_id);
    }

    // {0x04, 0x03, "Audio Device"},
    if (dev.class == 0x04 && dev.subclass == 0x03) {
        boot_message(INFO,
                     "Audio device Vendor: %s (0x%04X), Device: 0x%04X",
                     pci_find_vendor(dev.vendor_id),
                     dev.vendor_id,
                     dev.device_id);
    }

    // {0x0C, 0x03, "USB Controller"},
    if (dev.class == 0x0C && dev.subclass == 0x03) {
        boot_message(INFO,
                     "USB controller interface: %s (prog_if=0x%02X)",
                     pci_usb_prog_if_name(dev.prog_if),
                     dev.prog_if);
    }

    for (size_t i = 0; i < sizeof(pci_drivers) / sizeof(struct pci_driver); i++) {
        const struct pci_driver *driver = &pci_drivers[i];

        const bool class_match    = driver->class == dev.class;
        const bool subclass_match = driver->subclass == dev.subclass;
        const bool vendor_match   = driver->vendor_id == PCI_ANY_ID || driver->vendor_id == dev.vendor_id;
        const bool device_match   = driver->device_id == PCI_ANY_ID || driver->device_id == dev.device_id;

        if (class_match && subclass_match && vendor_match && device_match) {
            pci_drivers[i].init(dev);
            return;
        }
    }
}

/**
 * @brief Read the full 256-byte PCI configuration header for a device.
 */
struct pci_header get_pci_data(const uint8_t bus, const uint8_t num, const uint8_t function)
{
    struct pci_header pci_data;
    for (uint8_t i = 0; i < 32; i++) {
        uint16_t word = pci_config_read_word(bus, num, function, i * 2);
        memcpy((char *)&pci_data + i * 2, &word, sizeof(word));
    }
    return pci_data;
}

/**
 * @brief Enumerate all buses, devices, and functions, loading matching drivers.
 */
void pci_scan()
{
    boot_message(INFO, "Scanning PCI devices...");

    struct pci_header pci_data;
    for (uint16_t i = 0; i < 256; i++) {
        pci_data = get_pci_data(i, 0, 0);
        if (pci_data.vendor_id != 0xFFFF) {
            for (uint8_t j = 0; j < 32; j++) {
                pci_data = get_pci_data(i, j, 0);
                if (pci_data.vendor_id != 0xFFFF) {
                    load_driver(pci_data, i, j, 0);

                    for (uint8_t k = 1; k < 8; k++) {
                        struct pci_header pci = get_pci_data(i, j, k);
                        if (pci.vendor_id != 0xFFFF) {
                            load_driver(pci, i, j, k);
                        }
                    }
                }
            }
        }
    }
}

/**
 * @brief Enable bus mastering capability for a PCI device.
 */
void pci_enable_bus_mastering(const struct pci_device device)
{
    constexpr uint16_t command_register_offset = 0x04;
    uint16_t dev_command_reg = pci_config_read_word(device.bus, device.slot, device.function, command_register_offset);
    dev_command_reg |= (PCI_COMMAND_BUS_MASTER | PCI_COMMAND_MEMORY);

    pci_config_write_word(device.bus, device.slot, device.function, command_register_offset, dev_command_reg);
}

/**
 * @brief Retrieve a Base Address Register matching the requested type.
 */
uint32_t pci_get_bar(const struct pci_device dev, const uint8_t type)
{
    uint32_t bar = 0;
    for (int i = 0; i < 6; i++) {
        bar = dev.bars[i];
        if ((bar & 0x1) == type) {
            return bar;
        }
    }

    return 0xFFFFFFFF;
}