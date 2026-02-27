#include <drivers/usb/xhci.h>
#include <drivers/usb/xhci_internal.h>
#include <drivers/usb/ehci.h>
#include <drivers/terminal.h>
#include <drivers/tsc.h>
#include <lib/string.h>
#include <mem/vmm.h>
#include <mem/dma.h>

struct xhci_controller g_xhci;
struct xhci_device g_xhci_devices[XHCI_MAX_DEVICES];

static const char *xhci_speed_name(const uint32_t speed)
{
    switch (speed) {
    case 0:
        return "none";
    case 1:
        return "full";
    case 2:
        return "low";
    case 3:
        return "high";
    case 4:
        return "super";
    case 5:
        return "super+";
    default:
        return "unknown";
    }
}

static bool xhci_get_mmio_bar(const struct pci_device *device, uint64_t *base_out)
{
    const uint32_t bar0 = device->bars[0];
    if (bar0 == 0 || (bar0 & PCI_BAR_IO)) {
        // Reject I/O BARs.
        return false;
    }

    uint64_t base           = (uint64_t)(bar0 & ~0xFULL); // Mask off BAR flags (16-byte alignment).
    const uint32_t mem_type = (bar0 >> 1) & 0x3;          // BAR memory type bits.
    if (mem_type == PCI_BAR_MEMORY_TYPE_64) {
        const uint32_t bar1 = device->bars[1];
        base                |= (uint64_t)bar1 << 32; // Upper 32 bits for 64-bit BAR.
    }

    if (base == 0) {
        return false;
    }

    *base_out = base;
    return true;
}

static bool xhci_setup_scratchpads(const struct xhci_controller *xhci)
{
    if (xhci->max_scratchpad == 0) {
        return true;
    }

    const size_t array_bytes = xhci->max_scratchpad * sizeof(uint64_t);
    uintptr_t array_phys     = 0;
    void *array_virt         = nullptr;
    if (!dma_alloc_pages(array_bytes, PAGE_SIZE, 0, &array_phys, &array_virt)) {
        return false;
    }

    auto array = (uint64_t *)array_virt;
    for (uint32_t i = 0; i < xhci->max_scratchpad; i++) {
        void *scratch_phys = pmm_alloc_page();
        if (!scratch_phys) {
            return false;
        }
        auto scratch_virt = (void *)((uintptr_t)scratch_phys + g_hhdm_offset);
        memset(scratch_virt, 0, PAGE_SIZE);
        array[i] = (uint64_t)(uintptr_t)scratch_phys;
    }

    xhci->dcbaa[0] = array_phys;
    return true;
}

static void xhci_log_port_state(const struct xhci_controller *xhci, const uint32_t port)
{
    const uint32_t offset = XHCI_OP_PORTSC_BASE + ((port - 1u) * XHCI_OP_PORTSC_STRIDE);
    const uint32_t portsc = xhci_read32(xhci->op_base, offset);
    const uint32_t speed  = (portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT; // PORTSC speed field.
    const uint32_t pls    = (portsc & XHCI_PORTSC_PLS_MASK) >> XHCI_PORTSC_PLS_SHIFT;     // PORTSC PLS field.

    // Report PORTSC status bits: CCS/PP/PED/PR.
    boot_message(INFO,
                 "[xHCI] Port %u status=0x%08x ccs=%u pp=%u ped=%u pr=%u pls=0x%x speed=%s",
                 port,
                 portsc,
                 (portsc & XHCI_PORTSC_CCS) ? 1u : 0u,
                 (portsc & XHCI_PORTSC_PP) ? 1u : 0u,
                 (portsc & XHCI_PORTSC_PED) ? 1u : 0u,
                 (portsc & XHCI_PORTSC_PR) ? 1u : 0u,
                 pls,
                 xhci_speed_name(speed));
}

static void xhci_dump_ports(const struct xhci_controller *xhci)
{
    for (uint32_t port = 1; port <= xhci->max_ports; port++) {
        xhci_log_port_state(xhci, port);
    }
}

static void xhci_intel_port_routing(const struct xhci_controller *xhci)
{
    if (xhci->pci.vendor_id != PCI_VENDOR_INTEL) {
        return;
    }

    const uint32_t usb3_pssen = xhci_pci_read32(&xhci->pci, XHCI_INTEL_USB3_PSSEN);
    const uint32_t xusb2pr    = xhci_pci_read32(&xhci->pci, XHCI_INTEL_XUSB2PR);

    if (usb3_pssen == 0xFFFFFFFFu && xusb2pr == 0xFFFFFFFFu) {
        boot_message(INFO, "[xHCI] Intel port routing registers not present");
        return;
    }

    const uint32_t mask =
        (xhci->max_ports >= 32u) ? 0xFFFFFFFFu : ((1u << xhci->max_ports) - 1u); // Mask for present ports.
    if (usb3_pssen != 0xFFFFFFFFu) {
        xhci_pci_write32(&xhci->pci, XHCI_INTEL_USB3_PSSEN, usb3_pssen | mask); // Enable USB3 ports.
    }
    if (xusb2pr != 0xFFFFFFFFu) {
        xhci_pci_write32(&xhci->pci, XHCI_INTEL_XUSB2PR, xusb2pr | mask); // Enable USB2 ports.
    }

    const uint32_t usb3_after  = xhci_pci_read32(&xhci->pci, XHCI_INTEL_USB3_PSSEN);
    const uint32_t xusb2_after = xhci_pci_read32(&xhci->pci, XHCI_INTEL_XUSB2PR);
    boot_message(INFO,
                 "[xHCI] Intel port routing USB3_PSSEN=0x%08x XUSB2PR=0x%08x",
                 usb3_after,
                 xusb2_after);
}

static void xhci_power_ports(const struct xhci_controller *xhci)
{
    for (uint32_t port = 1; port <= xhci->max_ports; port++) {
        const uint32_t offset = XHCI_OP_PORTSC_BASE + ((port - 1u) * XHCI_OP_PORTSC_STRIDE);
        uint32_t portsc       = xhci_read32(xhci->op_base, offset);
        if ((portsc & XHCI_PORTSC_PP) == 0) {
            // Power bit clear.
            portsc |= XHCI_PORTSC_PP; // Set port power.
            xhci_write32(xhci->op_base, offset, portsc);
        }
    }

    tsc_sleep_ms(XHCI_PORT_POWER_DELAY_MS);
}

static bool xhci_enumerate_device(struct xhci_controller *xhci, struct xhci_device *dev)
{
    xhci_ring_reset(&dev->ep0_ring);

    if (!xhci_address_device(xhci, dev)) {
        return false;
    }

    tsc_sleep_ms(XHCI_ADDRESS_SETTLE_MS);

    if (!xhci_get_device_descriptor(xhci, dev)) {
        return false;
    }

    tsc_sleep_ms(XHCI_ADDRESS_SETTLE_MS);

    if (!xhci_get_config_descriptor(xhci, dev)) {
        return false;
    }

    return true;
}

static bool xhci_setup_slot_for_port(struct xhci_controller *xhci,
                                     const uint32_t port,
                                     const uint32_t speed,
                                     const uint32_t portsc_after,
                                     uint8_t *slot_id_out,
                                     struct xhci_device **dev_out)
{
    uint8_t slot_id = 0;
    if (!xhci_enable_slot(xhci, &slot_id)) {
        boot_message(WARNING, "[xHCI] Port %u enable slot failed", port);
        return false;
    }

    boot_message(INFO, "[xHCI] Port %u slot %u enabled", port, slot_id);
    struct xhci_device *dev = xhci_device_from_slot(slot_id);
    if (!dev) {
        boot_message(WARNING, "[xHCI] Slot %u out of range", slot_id);
        xhci_disable_slot(xhci, slot_id);
        return false;
    }

    memset(dev, 0, sizeof(*dev));
    dev->active        = true;
    dev->slot_id       = slot_id;
    dev->port_id       = (uint8_t)port;
    uint32_t new_speed =
        (portsc_after & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT; // Extract PORTSC speed.
    if (new_speed == 0) {
        new_speed = speed;
    }
    dev->speed = new_speed;

    if (slot_id_out) {
        *slot_id_out = slot_id;
    }
    if (dev_out) {
        *dev_out = dev;
    }
    return true;
}

static bool xhci_attempt_enumeration(struct xhci_controller *xhci,
                                     const uint32_t port,
                                     const uint32_t speed)
{
    uint32_t portsc_after = 0;
    if (!xhci_port_reset(xhci, port, speed, &portsc_after)) {
        boot_message(WARNING, "[xHCI] Port %u reset failed", port);
        return false;
    }

    tsc_sleep_ms(XHCI_USB3_RESET_RECOVERY_MS);

    struct xhci_device *dev = nullptr;
    uint8_t slot_id         = 0;
    if (!xhci_setup_slot_for_port(xhci, port, speed, portsc_after, &slot_id, &dev)) {
        return false;
    }

    if (!xhci_alloc_device_context(xhci, dev)) {
        boot_message(WARNING, "[xHCI] Slot %u device context alloc failed", slot_id);
        dev->active = false;
        xhci_disable_slot(xhci, slot_id);
        return false;
    }
    if (!xhci_prepare_slot_context(xhci, dev)) {
        boot_message(WARNING, "[xHCI] Slot %u input context prep failed", slot_id);
        dev->active = false;
        xhci_disable_slot(xhci, slot_id);
        return false;
    }

    bool ok = xhci_enumerate_device(xhci, dev);
    if (!ok) {
        dev->active = false;
        xhci_disable_slot(xhci, slot_id);
    }

    return ok;
}

static bool xhci_enumerate_port(struct xhci_controller *xhci,
                                const uint32_t port,
                                const uint32_t speed)
{
    return xhci_attempt_enumeration(xhci, port, speed);
}

static void xhci_scan_ports(struct xhci_controller *xhci)
{
    uint32_t found = 0;

    xhci_dump_ports(xhci);

    for (uint32_t port = 1; port <= xhci->max_ports; port++) {
        const uint32_t offset = XHCI_OP_PORTSC_BASE + ((port - 1u) * XHCI_OP_PORTSC_STRIDE);
        uint32_t portsc       = xhci_read32(xhci->op_base, offset);
        if ((portsc & XHCI_PORTSC_CCS) == 0) {
            // No device connected.
            continue;
        }

        const uint32_t speed =
            (portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT; // PORTSC speed field.

        boot_message(INFO, "[xHCI] Port %u connected speed=%s", port, xhci_speed_name(speed));

        if (xhci_enumerate_port(xhci, port, speed)) {
            found++;
        }
    }

    if (found == 0) {
        boot_message(ERROR, "[xHCI] No connected ports detected");
    }
}

static void xhci_init_registers(struct xhci_controller *xhci,
                                const struct pci_device *device,
                                const uint64_t mmio_phys,
                                uint32_t *hcs_out,
                                uint32_t *dboff_out,
                                uint32_t *rtsoff_out)
{
    memset(xhci, 0, sizeof(*xhci));
    xhci->pci  = *device;
    xhci->mmio = (volatile uint8_t *)(mmio_phys + g_hhdm_offset);

    const uint32_t cap  = xhci_read32(xhci->mmio, XHCI_CAPLENGTH);
    const uint32_t hcs  = xhci_read32(xhci->mmio, XHCI_HCSPARAMS1);
    const uint32_t hcs2 = xhci_read32(xhci->mmio, XHCI_HCSPARAMS2);
    const uint32_t hcc1 = xhci_read32(xhci->mmio, XHCI_HCCPARAMS1);
    // Clear reserved low bits (32-bit alignment).
    const uint32_t dboff = xhci_read32(xhci->mmio, XHCI_DBOFF) & ~0x3u;
    // Clear reserved low bits (32-byte alignment).
    const uint32_t rtsoff = xhci_read32(xhci->mmio, XHCI_RTSOFF) & ~0x1Fu;

    xhci->cap_len      = (uint8_t)(cap & 0xFFu); // CAPLENGTH is low 8 bits.
    xhci->op_base      = xhci->mmio + xhci->cap_len;
    xhci->max_slots    = hcs & 0xFFu;                    // Max device slots (bits 7:0).
    xhci->max_ports    = (hcs >> 24) & 0xFFu;            // Max ports (bits 31:24).
    xhci->context_size = (hcc1 & (1u << 2)) ? 64u : 32u; // CSZ selects 64-byte vs 32-byte contexts.

    // Scratchpad count is hi/lo 5-bit fields.
    xhci->max_scratchpad = (((hcs2 >> 27) & 0x1Fu) << 5) | ((hcs2 >> 21) & 0x1Fu);
    xhci->db_base        = xhci->mmio + dboff;
    xhci->rt_base        = xhci->mmio + rtsoff;

    if (hcs_out) {
        *hcs_out = hcs;
    }
    if (dboff_out) {
        *dboff_out = dboff;
    }
    if (rtsoff_out) {
        *rtsoff_out = rtsoff;
    }
}

static void xhci_log_controller(const struct xhci_controller *xhci,
                                const uint64_t mmio_phys,
                                const uint32_t hcs,
                                const uint32_t dboff,
                                const uint32_t rtsoff)
{
    boot_message(INFO,
                 "[xHCI] PCI %04x:%04x bus=%u slot=%u func=%u MMIO=0x%lx",
                 xhci->pci.vendor_id,
                 xhci->pci.device_id,
                 xhci->pci.bus,
                 xhci->pci.slot,
                 xhci->pci.function,
                 (unsigned long)mmio_phys);
    boot_message(INFO,
                 "[xHCI] caplen=%u hcsparams1=0x%08x slots=%u ports=%u ctx=%u scratchpads=%u",
                 xhci->cap_len,
                 hcs,
                 xhci->max_slots,
                 xhci->max_ports,
                 xhci->context_size,
                 xhci->max_scratchpad);
    boot_message(INFO, "[xHCI] dboff=0x%08x rtsoff=0x%08x", dboff, rtsoff);
}

static bool xhci_setup_command_ring(struct xhci_controller *xhci)
{
    if (!xhci_ring_init(&xhci->cmd_ring, XHCI_CMD_RING_TRBS)) {
        boot_message(ERROR, "[xHCI] Command ring alloc failed");
        return false;
    }

    return true;
}

static bool xhci_reset_controller(const struct xhci_controller *xhci)
{
    uint32_t cmd = xhci_read32(xhci->op_base, XHCI_OP_USBCMD);
    cmd          &= ~XHCI_USBCMD_RS; // Clear Run/Stop to halt controller.
    xhci_write32(xhci->op_base, XHCI_OP_USBCMD, cmd);
    if (!xhci_wait_for(xhci->op_base, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, true, XHCI_TIMEOUT_MS)) {
        boot_message(WARNING, "[xHCI] Stop timeout");
    }

    cmd = xhci_read32(xhci->op_base, XHCI_OP_USBCMD);
    cmd |= XHCI_USBCMD_HCRST; // Request host controller reset.
    xhci_write32(xhci->op_base, XHCI_OP_USBCMD, cmd);

    if (!xhci_wait_for(xhci->op_base,
                       XHCI_OP_USBCMD,
                       XHCI_USBCMD_HCRST,
                       false,
                       XHCI_RESET_TIMEOUT_MS)) {
        boot_message(ERROR, "[xHCI] Reset timeout");
        return false;
    }

    if (!xhci_wait_for(xhci->op_base,
                       XHCI_OP_USBSTS,
                       XHCI_USBSTS_CNR,
                       false,
                       XHCI_RESET_TIMEOUT_MS)) {
        boot_message(ERROR, "[xHCI] Controller not ready");
        return false;
    }

    return true;
}

static bool xhci_setup_context_arrays(struct xhci_controller *xhci)
{
    const size_t dcbaa_bytes = (xhci->max_slots + 1u) * sizeof(uint64_t);
    if (!dma_alloc_pages(dcbaa_bytes, PAGE_SIZE, 0, &xhci->dcbaa_phys, (void **)&xhci->dcbaa)) {
        boot_message(ERROR, "[xHCI] DCBAA alloc failed");
        return false;
    }

    if (!xhci_setup_scratchpads(xhci)) {
        boot_message(ERROR, "[xHCI] Scratchpad alloc failed");
        return false;
    }

    xhci_write32(xhci->op_base, XHCI_OP_PAGESIZE, 1u);
    xhci_write64(xhci->op_base, XHCI_OP_DCBAAP, xhci->dcbaa_phys);
    return true;
}

static bool xhci_setup_event_ring(struct xhci_controller *xhci)
{
    if (!xhci_event_ring_init(&xhci->event_ring, XHCI_EVENT_RING_TRBS)) {
        boot_message(ERROR, "[xHCI] Event ring alloc failed");
        return false;
    }

    auto ir_base = (volatile uint8_t *)(xhci->rt_base + XHCI_RT_IR_BASE);
    xhci_write32(ir_base, XHCI_IMAN, 0x1u);
    xhci_write32(ir_base, XHCI_IMOD, 0);
    xhci_write32(ir_base, XHCI_ERSTSZ, 1u);
    xhci_write64(ir_base, XHCI_ERSTBA, xhci->event_ring.erst_phys);
    xhci_write64(ir_base, XHCI_ERDP, xhci->event_ring.phys | XHCI_ERDP_EHB); // Set EHB when updating ERDP.

    return true;
}

static void xhci_configure_slots(const struct xhci_controller *xhci)
{
    const uint32_t slots = xhci->max_slots ? xhci->max_slots : 1u;
    xhci_write32(xhci->op_base, XHCI_OP_CONFIG, slots);
}

static void xhci_start_controller(const struct xhci_controller *xhci)
{
    xhci_write64(xhci->op_base, XHCI_OP_CRCR, xhci->cmd_ring.phys | XHCI_TRB_CYCLE); // CRCR with cycle bit.

    uint32_t cmd = xhci_read32(xhci->op_base, XHCI_OP_USBCMD);
    cmd          |= XHCI_USBCMD_RS;    // Start controller.
    cmd          &= ~XHCI_USBCMD_INTE; // Disable interrupts (polled).
    xhci_write32(xhci->op_base, XHCI_OP_USBCMD, cmd);
    if (!xhci_wait_for(xhci->op_base, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, false, XHCI_TIMEOUT_MS)) {
        boot_message(WARNING, "[xHCI] Start timeout");
    }

    const uint32_t usbcmd = xhci_read32(xhci->op_base, XHCI_OP_USBCMD);
    const uint32_t usbsts = xhci_read32(xhci->op_base, XHCI_OP_USBSTS);
    boot_message(INFO, "[xHCI] started usbcmd=0x%08x usbsts=0x%08x", usbcmd, usbsts);
}

void xhci_init(struct pci_device device)
{
    if (device.prog_if != 0x30) {
        return;
    }

    ehci_quiesce_all();

    uint64_t mmio_phys = 0;
    if (!xhci_get_mmio_bar(&device, &mmio_phys)) {
        boot_message(ERROR, "[xHCI] No MMIO BAR");
        return;
    }

    pci_enable_bus_mastering(device);
    xhci_map_mmio_range(mmio_phys, XHCI_MMIO_MAP_BYTES);
    uint32_t hcs    = 0;
    uint32_t dboff  = 0;
    uint32_t rtsoff = 0;
    xhci_init_registers(&g_xhci, &device, mmio_phys, &hcs, &dboff, &rtsoff);
    if (!xhci_setup_command_ring(&g_xhci)) {
        return;
    }

    xhci_log_controller(&g_xhci, mmio_phys, hcs, dboff, rtsoff);

    xhci_intel_port_routing(&g_xhci);

    if (!xhci_reset_controller(&g_xhci)) {
        return;
    }
    if (!xhci_setup_context_arrays(&g_xhci)) {
        return;
    }
    if (!xhci_setup_event_ring(&g_xhci)) {
        return;
    }
    xhci_configure_slots(&g_xhci);
    xhci_start_controller(&g_xhci);
    xhci_power_ports(&g_xhci);
    tsc_sleep_ms(XHCI_PORT_CONNECT_DELAY_MS);
    xhci_scan_ports(&g_xhci);
}