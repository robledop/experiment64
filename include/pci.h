#pragma once

#include <stdint.h>

// https://wiki.osdev.org/PCI
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

//  Bit 0, RW: I/O Space. If set to 1 the device can respond to I/O Space accesses; otherwise, the device's
// response is disabled.
#define PCI_COMMAND_IO 0x1
// Bit 1, RW: Memory Space. If set to 1 the device can respond to Memory Space accesses; otherwise, the device's
// response is disabled.
#define PCI_COMMAND_MEMORY 0x2
// Bit 2, RW: Bus Master. If set to 1 the device can behave as a bus master; otherwise, the device can not
// generate PCI accesses.
#define PCI_COMMAND_BUS_MASTER 0x4
// Bit 3, RO: Special Cycle. If set to 1 the device can monitor Special Cycle operations; otherwise,
// the device will ignore them.
#define PCI_COMMAND_SPECIAL_CYCLES 0x8
// Bit 4, RW: Memory Write and Invalidate enable. If set to 1 the device can generate the Memory Write and
// Invalidate command; otherwise, the Memory Write command must be used.
#define PCI_COMMAND_MEM_WRITE_INVALIDATE 0x10
// Bit 5, RW: VGA Palette Snooping. If set to 1 the device does not respond to palette register writes and will
// snoop the data; otherwise, the device will treat palette write accesses like all other accesses.
#define PCI_COMMAND_VGA_PALETTE_SNOOP 0x20
// Bit 6, RW: Parity Error Response. If set to 1 the device will take its normal action when a parity error is
// detected; otherwise, when an error is detected, the device will set bit 15 of the Status register
// (Detected Parity Error Status Bit), but will not assert the PERR# (Parity Error) pin and will continue
// operation as normal.
#define PCI_COMMAND_PARITY_ERROR_RESPONSE 0x40
// Bit 8, RW: SERR# Enable. If set to 1 the SERR# driver is enabled; otherwise, the driver is disabled.
#define PCI_COMMAND_SERR 0x100
// Bit 9, RO: Fast Back-to-Back Enable. If set to 1 indicates a device is allowed to generate fast back-to-back
// transactions; otherwise, fast back-to-back transactions are only allowed to the same agent.
#define PCI_COMMAND_FAST_BACK 0x200
// Bit 10, RW: Interrupt Disable. If set to 1 the assertion of the devices INTx# signal is disabled; otherwise,
// assertion of the signal is enabled.
#define PCI_COMMAND_INTERRUPT_DISABLE 0x400

// Represents the state of the device's INTx# signal. If set to 1 and bit 10 of the Command register
//(Interrupt Disable bit) is set to 0 the signal will be asserted; otherwise, the signal will be ignored.
#define PCI_STATUS_INTERRUPT 0x8
// If set to 1 the device implements the pointer for a New Capabilities Linked list at offset 0x34; otherwise,
// the linked list is not available.
#define PCI_STATUS_CAPABILITIES_LIST 0x10
// If set to 1 the device is capable of running at 66 MHz; otherwise, the device runs at 33 MHz.
#define PCI_STATUS_66MHZ 0x20
// If set to 1 the device can accept fast back-to-back transactions that are not from the same agent; otherwise,
// transactions can only be accepted from the same agent.
#define PCI_STATUS_FAST_BACK 0x80
// This bit is only set when the following conditions are met. The bus agent asserted PERR# on a read or observed an
// assertion of PERR# on a write, the agent setting the bit acted as the bus master for the operation in which the error
// occurred, and bit 6 of the Command register (Parity Error Response bit) is set to 1.
#define PCI_STATUS_PARITY_ERROR 0x100
// DEVSEL Timing - Read only bits that represent the slowest time that a device will assert DEVSEL# for any bus command
// except Configuration Space read and writes. Where a value of 0x0 represents fast timing, a value of 0x1 represents
// medium timing, and a value of 0x2 represents slow timing.
#define PCI_STATUS_DEVSEL_MEDIUM 0x600
#define PCI_STATUS_DEVSEL_FAST 0x200
#define PCI_STATUS_DEVSEL_SLOW 0x400
#define PCI_STATUS_DEVSEL_MASK 0x600
// This bit will be set to 1 whenever a target device terminates a transaction with Target-Abort.
#define PCI_STATUS_SIG_TARGET_ABORT 0x800
// This bit will be set to 1, by a master device, whenever its transaction is terminated with Target-Abort.
#define PCI_STATUS_REC_TARGET_ABORT 0x1000
// This bit will be set to 1, by a master device, whenever its transaction (except for Special Cycle transactions) is
// terminated with Master-Abort.
#define PCI_STATUS_REC_MASTER_ABORT 0x2000
// This bit will be set to 1 whenever the device asserts SERR#
#define PCI_STATUS_SIG_SYSTEM_ERROR 0x4000
// This bit will be set to 1 whenever the device detects a parity error, even if parity error handling is disabled.
#define PCI_STATUS_DETECTED_PARITY 0x8000

// Type 0x00: A general device
struct pci_header {
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

#define PCI_BAR_MEM 0x0
#define PCI_BAR_IO 0x1
#define PCI_BAR_NONE 0x3

#define PCI_BAR_MEMORY_TYPE_32 0x0
#define PCI_BAR_MEMORY_TYPE_64 0x2

struct pci_device {
    uint32_t bus;
    uint32_t slot;
    uint32_t function;
    struct pci_header header;
};

struct pci_driver {
    uint8_t class;
    uint8_t subclass;
    uint16_t vendor_id;
    uint16_t device_id;
    /// Initialize the driver for the device.
    void (*init)(struct pci_device device);
};

struct pci_class {
    uint8_t class;
    uint8_t subclass;
    const char *name;
};

struct pci_vendor {
    uint16_t id;
    const char *name;
};

#define PCI_ANY_ID 0xFFFF

void pci_scan(void);
void pci_enable_bus_mastering(struct pci_device device);
uint32_t pci_get_bar(struct pci_device dev, uint8_t type);
