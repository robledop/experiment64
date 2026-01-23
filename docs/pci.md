# PCI instrumentation

The PCI scan can dump detailed configuration information for a specific device during boot.
This is intended for collecting PCI config space details from real hardware.

## Current target

- Vendor ID: 0x1969
- Device ID: 0x1090

## Output

- Summary fields (class, revision, command/status, subsystem IDs, IRQ/pin)
- BAR decoding (IO vs MEM, 32-bit vs 64-bit, base addresses)
- Capability list entries
- Raw 256-byte config space dump

## Changing the target

Update `PCI_TRACE_VENDOR_ID` and `PCI_TRACE_DEVICE_ID` in `kernel/drivers/pci.c`.
