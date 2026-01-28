# USB

The kernel currently initializes xHCI controllers only. During bring-up it quiesces EHCI controllers (legacy handoff,
stop, and disable bus mastering/interrupts) and applies Intel USB2/USB3 port routing registers when available to keep
ports owned by xHCI. The xHCI driver maps MMIO, resets the controller, allocates the command and event rings, powers
ports, and scans all root hub ports for connected devices. USB 3.x ports use a warm reset sequence during bring-up.
Devices are initialized by enabling a slot, issuing Address
Device, and fetching descriptors before probing for mass storage.
The driver uses polling for command and transfer completions with xHCI interrupts disabled. USB 2.x devices are not
supported.

USB mass storage is supported for USB 3.x devices that expose a BOT/SCSI interface. The driver configures bulk
endpoints, issues basic SCSI commands (INQUIRY, TEST UNIT READY, READ CAPACITY), and exposes the device as storage
device 2. The ext2 partition on the USB image mounts at `/usb` with read/write support.

If a USB disk provides the ESP and root partitions, the VFS will treat it as the boot device and mount root from USB.

## References

https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf

https://www.youtube.com/playlist?list=PLATP7rOKo3E82tBnMp90B4zejpWeAKlxn
