# From PCI Probe to a USB Disk Sector

This walkthrough follows one boot-time chain in this tree: how `pci_scan()`
discovers an xHCI controller, how the xHCI driver brings the controller up and
enumerates a USB device, how a mass-storage device is recognized and configured,
and how a SCSI `READ(10)` eventually answers a `storage_read()` call from the
VFS. It matters because it is the only path in this kernel where a high-level
block read (`/usb`) bottoms out in raw Transfer Request Blocks (TRBs) posted to
a hardware ring and a doorbell write. `docs/usb.md` describes this in prose but
has no code anchors; this doc supplies them.

## Scope

This traces the *happy path* for a single USB BOT/SCSI mass-storage device,
boot-time only:

- PCI config-space scan and class-based driver binding
- xHCI controller init (MMIO, rings, contexts, start)
- Slot enable + Address Device + descriptor fetch (enumeration)
- BOT (Bulk-Only Transport) endpoint configuration and SCSI `READ(10)`
- The `storage.c` shim that lets the VFS read USB sectors

It does *not* cover HID mice/tablets (a sibling branch of the same dispatch),
hot-plug, interrupts (the driver polls), or write/flush paths beyond noting they
exist. Limits are called out inline and in **Gotchas**.

## Files in play

- `kernel/kernel.c` — boot order: `pci_scan()` then `storage_init()` then
  `vfs_init()`/`vfs_mount_root()` (lines 148-155).
- `kernel/drivers/pci.c` — config-space accessors, the bus scan, and the
  class/subclass/vendor driver table that binds `xhci_init`.
- `kernel/drivers/usb/xhci_core.c` — `xhci_init()` and all controller bring-up
  (reset, DCBAA, scratchpads, event ring, start, port scan).
- `include/drivers/usb/xhci_internal.h` — every MMIO offset, every TRB/context
  struct, and several inline helpers (`xhci_endpoint_id`, `xhci_read32`, the
  MMIO mapping helper). Most "constants" referenced below live here.
- `kernel/drivers/usb/xhci_ring.c` — the TRB ring model: producer ring with a
  Link TRB, the event-ring consumer, and the doorbell. Introduced at the first
  doorbell below.
- `kernel/drivers/usb/xhci_device.c` — enumeration commands and control
  transfers. Note: `xhci_get_config_descriptor()` is where device-*class*
  dispatch happens, despite its name.
- `kernel/drivers/usb/xhci_msc.c` — BOT/SCSI: config parse, Configure Endpoint,
  CBW/CSW transactions, and the `xhci_usb_storage_read()` entry point.
- `kernel/io/storage.c` — the backend-agnostic shim that maps storage device 2
  to the USB driver, closing the loop back to the VFS.

## The walk

### 1. The scan finds a USB controller

Boot calls `pci_scan()` at `kernel/kernel.c:148`. The scan
(`pci.c:584`) walks all 256 buses × 32 slots × 8 functions, reading the full
256-byte config header for each with `pci_read_header()` (`pci.c:564`), which is
itself 32 word reads through `pci_config_read_word()` (`pci.c:257`) over the
legacy `0xCF8`/`0xCFC` I/O ports. A vendor ID of `0xFFFF` means "no device" and
is skipped (`pci.c:577`, `pci.c:590`).

For every present function, `pci_bind_driver()` (`pci.c:545`) builds a
`struct pci_device` from the header and walks the static `pci_drivers[]` table
(`pci.c:220`). Matching is by class/subclass plus optional vendor/device
(`pci_driver_matches()`, `pci.c:535`). The USB entry is:

```c
{.class = PCI_CLASS_SERIAL_BUS, .subclass = PCI_SUBCLASS_USB,
 .vendor_id = PCI_ANY_ID, .device_id = PCI_ANY_ID, .init = &xhci_init}
```

(`pci.c:231`). `PCI_CLASS_SERIAL_BUS` is `0x0C` and `PCI_SUBCLASS_USB` is `0x03`
(`pci.c:28-29`). On the first match, `driver->init(dev)` is called and the loop
returns (`pci.c:554-557`) — one driver per function.

### 2. `xhci_init` rejects non-xHCI USB controllers

`xhci_init()` (`xhci_core.c:472`) is handed the whole `struct pci_device`. The
PCI table matched on class `0x0C`/`0x03` alone, which also covers UHCI/OHCI/EHCI.
The driver narrows that immediately: `if (device.prog_if != 0x30) return;`
(`xhci_core.c:474`). `0x30` is the programming-interface byte for xHCI. So the
*real* binding key is class + subclass + `prog_if`, and the `prog_if` half is
enforced inside the driver rather than in the table.

It then quiesces any EHCI controllers (`ehci_quiesce_all()`, `xhci_core.c:478`)
so USB2 ports stay routed to xHCI, and pulls the MMIO base out of BAR0/BAR1 with
`xhci_get_mmio_bar()` (`xhci_core.c:33`), rejecting I/O-type BARs and combining
the high dword for a 64-bit BAR.

### 3. Map MMIO, read capability registers, lay out the controller

`pci_enable_bus_mastering()` (`pci.c:609`) sets the bus-master + memory bits in
the PCI command register so the controller may DMA. `xhci_map_mmio_range()`
(`xhci_internal.h:630`) maps the BAR into the HHDM with cache-disable
(`PTE_PCD | PTE_PWT`) — it walks the current PML4 by hand and only maps pages not
already present.

`xhci_init_registers()` (`xhci_core.c:303`) reads the capability block: it
derives `op_base = mmio + CAPLENGTH` (`xhci_core.c:324`), `max_slots` and
`max_ports` from HCSPARAMS1, `context_size` (32 or 64 bytes) from the CSZ bit of
HCCPARAMS1 (`xhci_core.c:327`), the scratchpad count from the split 5-bit fields
of HCSPARAMS2 (`xhci_core.c:330`), and the doorbell-array and runtime-register
bases from DBOFF/RTSOFF. The register offsets are the `XHCI_*` defines in
`xhci_internal.h:24-41`.

### 4. The command ring — first look at the TRB model

`xhci_setup_command_ring()` (`xhci_core.c:370`) calls `xhci_ring_init()`
(`xhci_ring.c:46`). This is the producer ring used for *commands*; the same
`struct xhci_ring` type is later reused for control (EP0) and bulk transfer
rings. A ring is a contiguous DMA-allocated array of `struct xhci_trb`
(16 bytes each, `xhci_internal.h:141-204`). The key design points, all visible in
`xhci_ring_init()`:

- The **last** TRB is a **Link TRB** (`XHCI_TRB_TYPE_LINK`) pointing back at the
  ring's own physical base with the Toggle-Cycle bit set (`xhci_ring.c:61-68`).
  That makes the ring circular for the hardware without the producer ever
  writing past the array.
- The driver tracks a software `enqueue` index and a **cycle bit** (`cycle`,
  initially `true`). Hardware owns a TRB when its cycle bit equals the
  consumer's current cycle; the producer flips its own cycle each time it wraps
  through the Link TRB.

`xhci_ring_enqueue()` (`xhci_ring.c:73`) copies a caller-built TRB into the ring,
stamps the current cycle bit, advances `enqueue`, and on reaching the slot before
the Link TRB it stamps the Link TRB's cycle bit, resets `enqueue` to 0, and
flips `cycle` (`xhci_ring.c:81-86`). It returns the *physical* address of the TRB
it just wrote — that address is how completions are later matched.

### 5. Reset, contexts, event ring, start

Back in `xhci_init()` the controller is reset (`xhci_reset_controller()`,
`xhci_core.c:380`): clear Run/Stop, wait for Halted, set HCRST, wait for it to
self-clear, then wait for the Controller-Not-Ready bit to clear.

`xhci_setup_context_arrays()` (`xhci_core.c:414`) allocates the **DCBAA** (Device
Context Base Address Array), one 8-byte slot per device slot, and programs its
physical address into `DCBAAP` (`xhci_core.c:428`). `xhci_setup_scratchpads()`
(`xhci_core.c:56`) allocates the controller's private scratchpad pages and stores
their physical addresses in DCBAA entry 0 — slot 0 is reserved for exactly that.

`xhci_setup_event_ring()` (`xhci_core.c:432`) initializes the **event ring** via
`xhci_event_ring_init()` (`xhci_ring.c:91`). Unlike the producer rings, the event
ring is a *consumer* ring: hardware writes events, the driver reads them. It is
described to the controller through a one-entry **ERST** (Event Ring Segment
Table); the driver programs ERSTSZ=1, ERSTBA, and the initial ERDP with the
Event-Handler-Busy bit (`xhci_core.c:442-444`). Interrupts are enabled in the
interrupter (`IMAN=1`) but — crucially — disabled globally at the controller
(see step 7), so this driver is poll-only.

`xhci_configure_slots()` (`xhci_core.c:449`) writes `max_slots` into CONFIG, then
`xhci_start_controller()` (`xhci_core.c:455`) programs CRCR with the command-ring
physical base + cycle bit, sets Run/Stop, and **clears `XHCI_USBCMD_INTE`**
(`xhci_core.c:461`) — interrupts off by design.

### 6. Power and scan the ports

`xhci_power_ports()` (`xhci_core.c:146`) sets Port Power on every root port and
waits `XHCI_PORT_POWER_DELAY_MS`. After a connect-debounce sleep,
`xhci_scan_ports()` (`xhci_core.c:274`) reads each PORTSC register
(base `0x400`, stride `0x10`, `xhci_internal.h:92-93`) and, where the
Current-Connect-Status bit is set (`xhci_core.c:283`), extracts the speed and
calls `xhci_enumerate_port()` → `xhci_attempt_enumeration()` (`xhci_core.c:225`).

### 7. Reset the port, enable a slot, build the input context

`xhci_attempt_enumeration()` first resets the port. `xhci_port_reset()`
(`xhci_device.c:45`) branches on speed: USB 3.x (`speed >= 4`) gets a *warm*
reset (set `PORTSC_WR`, wait for the warm-reset-change bit); USB 2.0/1.x gets a
standard reset (set `PORTSC_PR`, wait for it to clear). Either way it then waits
for Port-Enabled + link-state U0 via `xhci_wait_port_ready()` (`xhci_device.c:18`).

Then `xhci_setup_slot_for_port()` (`xhci_core.c:184`) issues the first command:
`xhci_enable_slot()` (`xhci_device.c:95`) enqueues an Enable Slot command TRB and
rings doorbell 0. The slot ID comes back *in the command-completion event*
(`slot_id_out`). The driver indexes its `g_xhci_devices[]` array directly by slot
ID (`xhci_device_from_slot()`, `xhci_internal.h:682`).

`xhci_alloc_device_context()` (`xhci_device.c:116`) allocates the per-device
output context and writes its physical address into `dcbaa[slot_id]`
(`xhci_device.c:136`). `xhci_prepare_slot_context()` (`xhci_device.c:169`) builds
the **input context**: an input-control context with `add_flags = 0x3` (add the
slot context and EP0), a slot context carrying speed + root port + one context
entry, and an EP0 control-endpoint context whose dequeue pointer is the freshly
created EP0 ring with its cycle bit folded into bit 0 (`xhci_device.c:213-214`).
Note the hardcoded `max_packet = 512` for EP0 (`xhci_device.c:208`).

### 8. Command submission and the doorbell

Every command goes through `xhci_cmd_submit()` (`xhci_device.c:7`): enqueue the
TRB on the command ring, then `xhci_ring_doorbell(xhci, 0, 0)`
(`xhci_ring.c:255`). **Doorbell 0 is the host-controller doorbell** (commands);
per-device doorbells use the slot ID as the index (see step 9). The doorbell
write is the only "go" signal — there is no MMIO command otherwise. A memory
barrier precedes the write (`xhci_mb()`, `xhci_ring.c:258`).

Completion is harvested by polling, not interrupt:
`xhci_wait_for_cmd_completion()` (`xhci_ring.c:141`) spins on the event ring's
current dequeue TRB, comparing the TRB's cycle bit against the consumer's
expected `cycle` (`xhci_ring.c:151-152`). When they match, hardware has produced
an event. It matches `XHCI_TRB_TYPE_CMD_COMPLETION`, optionally checks the
returned TRB pointer against the command's physical address, reads the completion
code (`dword2 >> 24`), advances the dequeue via `xhci_event_ring_advance()`
(which writes the new ERDP back to the controller, `xhci_ring.c:126-139`), and
returns the slot ID. Stray Port-Status events are consumed and skipped
(`xhci_ring.c:184`).

### 9. Address Device and the first control transfer

`xhci_address_device()` (`xhci_device.c:220`) enqueues an Address Device command
carrying the input-context physical address and the slot ID, via the same
`xhci_cmd_submit()`. After this the device has a USB address and EP0 is live.

Control transfers now run over EP0 with `xhci_control_transfer()`
(`xhci_device.c:240`). This is the canonical xHCI control TD: a **Setup Stage**
TRB (the 8-byte setup packet carried as immediate data, `idt=1`,
`xhci_device.c:251-258`), an optional **Data Stage** TRB pointing at a DMA buffer
(`xhci_device.c:260-272`), and a **Status Stage** TRB with Interrupt-On-Completion
(`xhci_device.c:274-280`). All three are enqueued on `dev->ep0_ring`, then
`xhci_ring_doorbell(xhci, dev->slot_id, 1u)` rings the device's doorbell with
target value `1` — **doorbell index = slot ID, value = endpoint ID, and EP0's
endpoint ID is 1** (`xhci_device.c:288`). Completion is awaited on the
status-stage TRB via `xhci_wait_for_transfer_event()` (`xhci_ring.c:201`).

### 10. Fetch descriptors

`xhci_enumerate_device()` (`xhci_core.c:161`) drives three steps with settle
delays: Address Device, `xhci_get_device_descriptor()`, then
`xhci_get_config_descriptor()`. The device-descriptor fetch (`xhci_device.c:313`)
issues `GET_DESCRIPTOR(DEVICE)` for 18 bytes into a DMA buffer and logs VID/PID.

`xhci_get_config_descriptor()` (`xhci_device.c:358`) first reads the 9-byte
configuration descriptor header to learn `wTotalLength`, validates it
(`xhci_device.c:383-394`), then re-reads the *full* configuration block (all
interface/endpoint descriptors) into a buffer sized to `wTotalLength`
(`xhci_device.c:407-423`).

### 11. The misleadingly-named class dispatch

Despite being named `xhci_get_config_descriptor`, this function is where the
device's **class is decided and the driver branch is chosen**. After fetching the
full config it calls, in order (`xhci_device.c:425-465`):

1. `xhci_msc_parse_config()` — if it returns true, this is a BOT/SCSI mass-storage
   device. Run `SET_CONFIGURATION`, `xhci_msc_configure_endpoints()`, then
   `xhci_msc_init()`, and return.
2. Otherwise `xhci_hid_mouse_parse_config()` — the HID branch (out of scope here).

So enumeration and class dispatch are fused into one function; there is no
separate "match a driver" step for USB devices the way there is for PCI. A
first-time reader expecting a getter will instead find the entire mass-storage
bring-up hanging off this call.

### 12. Recognizing the BOT interface

`xhci_msc_parse_config()` (`xhci_msc.c:13`) linearly walks the descriptor blob
(`xhci_msc.c:54-96`). It looks for an interface descriptor with class
`USB_CLASS_MASS_STORAGE` (0x08), subclass `USB_SUBCLASS_SCSI` (0x06), protocol
`USB_PROTOCOL_BOT` (0x50), alt-setting 0 (`xhci_msc.c:63-66`), and within it the
two bulk endpoints (IN and OUT) plus, for SuperSpeed, the companion descriptor's
max-burst (`xhci_msc.c:85-91`). It maps each endpoint address to its xHCI
endpoint ID with `xhci_endpoint_id()` (`xhci_internal.h:688`): `ep_num*2` for OUT,
`ep_num*2 + 1` for IN, with EP0 mapping to 1. The parsed state lives in a single
`static struct xhci_msc_device g_xhci_msc` (`xhci_msc.c:6`) — there is exactly
one MSC device.

Importantly, this routine **saves and restores prior MSC state** when the config
is *not* an MSC config (`xhci_msc.c:23`, `xhci_msc.c:99`), so probing a HID mouse
on another slot does not wipe an already-working USB disk.

### 13. Configure the bulk endpoints

`xhci_msc_configure_endpoints()` (`xhci_msc.c:124`) allocates the bulk IN/OUT
transfer rings, then rebuilds the input context for a **Configure Endpoint**
command: it copies the live slot context and EP0 context out of the *output*
device context (`xhci_msc.c:149-158`), sets `add_flags` to include the slot bit
plus the two bulk endpoint bits (`xhci_msc.c:146`), bumps `ctx_entries` to the
highest endpoint ID, and fills the bulk endpoint contexts (type BULK_IN/BULK_OUT,
error count 3, max packet/burst, and dequeue pointer = ring base + cycle bit).
The command goes on the command ring + doorbell 0 + poll, exactly like step 8.

### 14. SCSI over BOT: the CBW / data / CSW dance

`xhci_msc_init()` (`xhci_msc.c:486`) allocates the CBW, CSW, and a 256 KiB data
buffer (`xhci_msc.c:462`, `XHCI_MSC_DATA_BYTES`), then runs `INQUIRY` (0x12),
`TEST UNIT READY` (0x00, retried up to 5×), `READ CAPACITY` (0x25), and a probe
`READ(10)` of LBA 0. Success sets `msc->active = true` (`xhci_msc.c:530`), which
is what `xhci_usb_storage_present()` (`xhci_msc.c:534`) reports.

Every SCSI command is one BOT transaction in `xhci_msc_transfer_locked()`
(`xhci_msc.c:290`), which is three bulk transfers:

1. **CBW** (Command Block Wrapper): `xhci_msc_prepare_cbw()` (`xhci_msc.c:250`)
   fills signature `USBC`, an incrementing tag, transfer length, direction flag,
   and the SCSI command bytes, then it is sent on the **bulk OUT** ring.
2. **Data** (if any): on the bulk IN ring for reads, bulk OUT for writes.
3. **CSW** (Command Status Wrapper): read back on the **bulk IN** ring; the driver
   checks the signature `USBS`, the matching tag, and zero status
   (`xhci_msc.c:353-362`).

Each leg is `xhci_msc_bulk_wait()` (`xhci_msc.c:268`): `xhci_bulk_queue()`
(`xhci_msc.c:200`) chops the buffer into Normal TRBs (chaining all but the last,
which gets Interrupt-On-Completion, and computing the TD-size field for the
hardware), then doorbell `(slot_id, ep_id)` and poll for the transfer event.

The whole transaction is wrapped in `xhci_msc_transfer()` (`xhci_msc.c:367`),
which takes `xhci->io_lock`. This sleeplock serializes the MSC path against the
HID poll thread because **both drain the single shared event ring** and would
otherwise consume each other's completion events (`xhci_msc.c:375-380`,
lock declared at `xhci_internal.h:493`).

### 15. `READ(10)` and the public read entry point

`xhci_usb_storage_read()` (`xhci_msc.c:539`) is the function the rest of the
kernel calls. It refuses anything but 512-byte blocks (`xhci_msc.c:545`),
bounds-checks `lba + count` against `block_count` (`xhci_msc.c:550`), and loops
issuing `xhci_msc_read10()` (`xhci_msc.c:434`) in chunks no larger than the data
buffer. `xhci_msc_read10()` builds a 10-byte SCSI `READ(10)` CDB — opcode `0x28`,
big-endian LBA at byte 2, big-endian block count at byte 7 (`xhci_msc.c:439-443`)
— and runs the BOT transaction with `data_in = true`. On success the bytes land
in `g_xhci_msc.data_buf`, which the caller `memcpy`s into the caller's buffer
(`xhci_msc.c:568`).

### 16. Closing the loop to the VFS

`storage_init()` runs right after `pci_scan()` (`kernel.c:149`). It maps three
abstract storage devices to backends; device 2 (`STORAGE_DEVICE_USB`) is wired to
USB *only if* `xhci_usb_storage_present()` is true (`storage.c:90-95`). Because
the xHCI driver already enumerated and probed during `pci_scan()`, that flag is
set by the time `storage_init()` runs — the ordering in `kernel.c` is load-bearing.

A VFS read for device 2 flows `storage_read()` (`storage.c:139`) →
`storage_read_backend()` (`storage.c:98`) → `case STORAGE_BACKEND_USB:
xhci_usb_storage_read(...)` (`storage.c:105`). The VFS then mounts the USB ext2
partition: `vfs_mount_root()` scans GPT partitions on each present device
(`vfs_scan_device()`, `vfs.c:623`) and mounts an extra USB root at `/usb`
(`vfs.c:765`). From there, every `read()` of a file under `/usb` ultimately turns
into a SCSI `READ(10)` and a TRB on a bulk-IN ring.

## Gotchas

- **The class dispatcher is named like a getter.**
  `xhci_get_config_descriptor()` (`xhci_device.c:358`) does not merely fetch a
  descriptor — it parses it, decides the device class, and synchronously runs the
  entire MSC (or HID) bring-up including `SET_CONFIGURATION`, Configure Endpoint,
  and SCSI probing. There is no separate USB driver-binding step.

- **Two kinds of doorbell, two meanings of the value.**
  Doorbell index 0 is the host controller (commands), value 0
  (`xhci_ring.c:255`, called from `xhci_device.c:14`). For transfers the doorbell
  index is the *slot ID* and the value is the *endpoint ID* — EP0 is endpoint ID
  1 (`xhci_device.c:288`), bulk endpoints use `xhci_endpoint_id()`
  (`xhci_internal.h:688`). Same function, completely different addressing.

- **Polling, not interrupts.** The driver enables the interrupter
  (`IMAN=1`, `xhci_core.c:440`) but clears the global interrupt-enable
  `XHCI_USBCMD_INTE` (`xhci_core.c:461`). All completions are harvested by
  spinning on the event ring (`xhci_wait_for_cmd_completion` /
  `xhci_wait_for_transfer_event`). The cycle-bit comparison is the only
  ownership protocol.

- **One shared event ring, one lock.** There is a single event ring for the whole
  controller. The MSC path and the HID poll thread both consume it, so MSC
  transactions hold `xhci->io_lock` end-to-end (`xhci_msc.c:367-381`). Without it
  the two consumers would steal each other's completion events.

- **Hardcoded ABI offsets and sizes.** EP0 max packet is hardcoded to 512
  (`xhci_device.c:208`); the storage read path only accepts 512-byte logical
  blocks (`xhci_msc.c:545`); the MSC data buffer is a fixed 256 KiB
  (`XHCI_MSC_DATA_BYTES`, `xhci_internal.h:139`). Larger reads are chunked to fit.

- **One MSC device, by static singleton.** `g_xhci_msc` is a single static
  (`xhci_msc.c:6`), so the kernel supports exactly one USB mass-storage device.
  The save/restore in `xhci_msc_parse_config()` (`xhci_msc.c:23`, `:99`) exists
  only to keep a later non-MSC enumeration from clobbering it.

## See also

- `docs/usb.md` — prose overview of the same flow (no code anchors).
- `docs/address_space.md` — HHDM and how MMIO/DMA physical pages become kernel
  pointers (`xhci_map_mmio_range`, `g_hhdm_offset`).
- `docs/dynamic_linking.md` — the house style this doc follows.
- Acronyms (TRB, DCBAA, ERST, ERDP, CBW/CSW, BOT, LBA, HHDM, MMIO) are collected
  in `docs/glossary.md`.
