# USB

The kernel currently initializes xHCI controllers only. During bring-up it quiesces EHCI controllers (legacy handoff,
stop, and disable bus mastering/interrupts) and applies Intel USB2/USB3 port routing registers when available to keep
ports owned by xHCI. The xHCI driver maps MMIO, allocates the command ring, then resets the controller. After reset it
sets up the DCBAA and scratchpad arrays, allocates the event ring, configures slot count, starts the controller, powers
ports, and scans all root hub ports for connected devices. USB 3.x ports use a warm reset sequence during bring-up.
Device initialization enables a slot, allocates a device context, prepares the slot and endpoint 0 contexts, issues
Address Device, fetches device and configuration descriptors, issues SET_CONFIGURATION, configures endpoints, and
runs SCSI commands (INQUIRY, TEST UNIT READY, READ CAPACITY) or HID initialization before registering the device.
The driver uses polling for command and transfer completions with xHCI interrupts disabled. USB 2.0/1.x devices are
supported via a standard port reset path, and enumeration is speed-agnostic.

USB mass storage is supported for any USB device speed (1.x, 2.0, 3.x) that exposes a BOT/SCSI interface.
Enumeration and MSC initialization happen regardless of device speed. The driver configures bulk
endpoints, issues basic SCSI commands (INQUIRY, TEST UNIT READY, READ CAPACITY), and exposes the device as storage
device 2. The ext2 partition on the USB image mounts at `/usb` with read/write support.

If a USB disk provides the ESP and root partitions, the VFS will treat it as the boot device and mount root from USB.

## HID Mouse and Tablet

USB HID mouse and tablet devices are also supported via the xHCI driver. During device enumeration, if no mass storage
interface is found, the driver checks for a HID interface with an interrupt IN endpoint. Both boot protocol mice
(subclass 1, protocol 2) and tablet-style absolute pointing devices are recognized. The driver parses the configuration
descriptor to locate the interrupt endpoint, configures it with a Configure Endpoint command, allocates a DMA report
buffer, and starts a kernel polling thread. Boot mice receive a SET_PROTOCOL(boot) request; both types receive
SET_IDLE to suppress idle reports. The polling thread continuously enqueues Normal TRBs on the interrupt IN endpoint,
waits for transfer events, and dispatches decoded reports to the kernel mouse subsystem. Mouse reports produce relative
motion events; tablet reports produce absolute coordinates scaled to the current framebuffer dimensions.

## References

https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf

https://www.youtube.com/playlist?list=PLATP7rOKo3E82tBnMp90B4zejpWeAKlxn
