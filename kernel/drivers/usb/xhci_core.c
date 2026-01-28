#include <drivers/usb/xhci.h>
#include <drivers/usb/xhci_internal.h>
#include <drivers/usb/ehci.h>
#include <drivers/terminal.h>
#include <drivers/tsc.h>
#include <lib/string.h>
#include <mem/vmm.h>

#define XHCI_CAPLENGTH 0x00u // Capability register length offset.
#define XHCI_HCSPARAMS1 0x04u // HCS parameters 1 offset.
#define XHCI_HCSPARAMS2 0x08u // HCS parameters 2 offset.
#define XHCI_HCCPARAMS1 0x10u // HCC parameters 1 offset.
#define XHCI_DBOFF 0x14u // Doorbell array offset register.
#define XHCI_RTSOFF 0x18u // Runtime register space offset.
#define XHCI_OP_USBCMD 0x00u // USBCMD operational register offset.
#define XHCI_OP_USBSTS 0x04u // USBSTS operational register offset.
#define XHCI_OP_PAGESIZE 0x08u // Page size register offset.
#define XHCI_OP_CRCR 0x18u // Command ring control register offset.
#define XHCI_OP_DCBAAP 0x30u // Device context base array pointer offset.
#define XHCI_OP_CONFIG 0x38u // Configure register offset.
#define XHCI_IMAN 0x00u // Interrupter management offset.
#define XHCI_IMOD 0x04u // Interrupter moderation offset.
#define XHCI_ERSTSZ 0x08u // Event ring segment table size offset.
#define XHCI_ERSTBA 0x10u // Event ring segment table base address offset.
#define XHCI_MMIO_MAP_BYTES 0x100000u // MMIO mapping size.

#define XHCI_USBCMD_RS (1u << 0) // Run/Stop bit.
#define XHCI_USBCMD_HCRST (1u << 1) // Host controller reset bit.
#define XHCI_USBCMD_INTE (1u << 2) // Interrupt enable bit.
#define XHCI_USBSTS_HCH (1u << 0) // Host controller halted bit.
#define XHCI_USBSTS_CNR (1u << 11) // Controller not ready bit.



#define XHCI_MAX_DEVICES 256u // Max device slots tracked.

#define XHCI_CMD_RING_TRBS 256u // Command ring TRB count.
#define XHCI_EVENT_RING_TRBS 256u // Event ring TRB count.

#define XHCI_RESET_TIMEOUT_MS 1000u // Reset timeout in ms.
#define XHCI_PORT_POWER_DELAY_MS 20u // Port power settle delay in ms.
#define XHCI_PORT_CONNECT_DELAY_MS 100u // Port connect debounce delay in ms.
#define XHCI_ADDRESS_SETTLE_MS 50u // Address settle delay in ms.

struct xhci_controller g_xhci;
static struct xhci_device g_xhci_devices[XHCI_MAX_DEVICES];

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

static struct xhci_device *xhci_device_from_slot(const uint8_t slot_id)
{
    if (slot_id == 0 || slot_id >= XHCI_MAX_DEVICES) {
        return nullptr;
    }

    return &g_xhci_devices[slot_id];
}

static bool xhci_get_mmio_bar(const struct pci_device *device, uint64_t *base_out)
{
    const uint32_t bar0 = device->bars[0];
    if (bar0 == 0 || (bar0 & PCI_BAR_IO)) {
        return false;
    }

    uint64_t base           = (uint64_t)(bar0 & ~0xFULL);
    const uint32_t mem_type = (bar0 >> 1) & 0x3;
    if (mem_type == PCI_BAR_MEMORY_TYPE_64) {
        const uint32_t bar1 = device->bars[1];
        base                |= (uint64_t)bar1 << 32;
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
    if (!xhci_alloc_pages(array_bytes, &array_phys, &array_virt)) {
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

static void xhci_log_port_state(struct xhci_controller *xhci, const uint32_t port)
{
    const uint32_t offset = XHCI_OP_PORTSC_BASE + ((port - 1u) * XHCI_OP_PORTSC_STRIDE);
    const uint32_t portsc = xhci_read32(xhci->op_base, offset);
    const uint32_t speed  = (portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
    const uint32_t pls    = (portsc & XHCI_PORTSC_PLS_MASK) >> XHCI_PORTSC_PLS_SHIFT;

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

static void xhci_dump_ports(struct xhci_controller *xhci)
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

    const uint32_t mask = (xhci->max_ports >= 32u) ? 0xFFFFFFFFu : ((1u << xhci->max_ports) - 1u);
    if (usb3_pssen != 0xFFFFFFFFu) {
        xhci_pci_write32(&xhci->pci, XHCI_INTEL_USB3_PSSEN, usb3_pssen | mask);
    }
    if (xusb2pr != 0xFFFFFFFFu) {
        xhci_pci_write32(&xhci->pci, XHCI_INTEL_XUSB2PR, xusb2pr | mask);
    }

    const uint32_t usb3_after  = xhci_pci_read32(&xhci->pci, XHCI_INTEL_USB3_PSSEN);
    const uint32_t xusb2_after = xhci_pci_read32(&xhci->pci, XHCI_INTEL_XUSB2PR);
    boot_message(INFO,
                 "[xHCI] Intel port routing USB3_PSSEN=0x%08x XUSB2PR=0x%08x",
                 usb3_after,
                 xusb2_after);
}

static void xhci_power_ports(struct xhci_controller *xhci)
{
    for (uint32_t port = 1; port <= xhci->max_ports; port++) {
        const uint32_t offset = XHCI_OP_PORTSC_BASE + ((port - 1u) * XHCI_OP_PORTSC_STRIDE);
        uint32_t portsc       = xhci_read32(xhci->op_base, offset);
        if ((portsc & XHCI_PORTSC_PP) == 0) {
            portsc |= XHCI_PORTSC_PP;
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
    uint32_t new_speed = (portsc_after & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
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
    if (!xhci_port_reset(xhci, port, &portsc_after)) {
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
            continue;
        }

        const uint32_t speed = (portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
        if (speed < 4u) {
            boot_message(WARNING,
                         "[xHCI] Port %u connected speed=%s portsc=0x%08x; unsupported",
                         port,
                         xhci_speed_name(speed),
                         portsc);
            continue;
        }

        boot_message(INFO, "[xHCI] Port %u connected speed=%s", port, xhci_speed_name(speed));

        if (xhci_enumerate_port(xhci, port, speed)) {
            found++;
        }
    }

    if (found == 0) {
        boot_message(ERROR, "[xHCI] No connected ports detected");
    }
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
    memset(&g_xhci, 0, sizeof(g_xhci));
    g_xhci.pci            = device;
    g_xhci.mmio           = (volatile uint8_t *)(mmio_phys + g_hhdm_offset);
    const uint32_t cap    = xhci_read32(g_xhci.mmio, XHCI_CAPLENGTH);
    g_xhci.cap_len        = (uint8_t)(cap & 0xFFu);
    const uint32_t hcs    = xhci_read32(g_xhci.mmio, XHCI_HCSPARAMS1);
    const uint32_t hcs2   = xhci_read32(g_xhci.mmio, XHCI_HCSPARAMS2);
    const uint32_t hcc1   = xhci_read32(g_xhci.mmio, XHCI_HCCPARAMS1);
    const uint32_t dboff  = xhci_read32(g_xhci.mmio, XHCI_DBOFF) & ~0x3u;
    const uint32_t rtsoff = xhci_read32(g_xhci.mmio, XHCI_RTSOFF) & ~0x1Fu;
    g_xhci.op_base        = g_xhci.mmio + g_xhci.cap_len;
    g_xhci.max_slots      = hcs & 0xFFu;
    g_xhci.max_ports      = (hcs >> 24) & 0xFFu;
    g_xhci.context_size   = (hcc1 & (1u << 2)) ? 64u : 32u;
    g_xhci.max_scratchpad = (((hcs2 >> 27) & 0x1Fu) << 5) | ((hcs2 >> 21) & 0x1Fu);
    g_xhci.db_base        = g_xhci.mmio + dboff;
    g_xhci.rt_base        = g_xhci.mmio + rtsoff;
    if (!xhci_ring_init(&g_xhci.cmd_ring, XHCI_CMD_RING_TRBS)) {
        boot_message(ERROR, "[xHCI] Command ring alloc failed");
        return;
    }

    boot_message(INFO,
                 "[xHCI] PCI %04x:%04x bus=%u slot=%u func=%u MMIO=0x%lx",
                 device.vendor_id,
                 device.device_id,
                 device.bus,
                 device.slot,
                 device.function,
                 (unsigned long)mmio_phys);
    boot_message(INFO,
                 "[xHCI] caplen=%u hcsparams1=0x%08x slots=%u ports=%u ctx=%u scratchpads=%u",
                 g_xhci.cap_len,
                 hcs,
                 g_xhci.max_slots,
                 g_xhci.max_ports,
                 g_xhci.context_size,
                 g_xhci.max_scratchpad);
    boot_message(INFO,
                 "[xHCI] dboff=0x%08x rtsoff=0x%08x",
                 dboff,
                 rtsoff);

    xhci_intel_port_routing(&g_xhci);

    // Reset sequence for the xHCI controller.
    // Set the RUN/STOP bit to 0.
    uint32_t cmd = xhci_read32(g_xhci.op_base, XHCI_OP_USBCMD);
    cmd          &= ~XHCI_USBCMD_RS;
    xhci_write32(g_xhci.op_base, XHCI_OP_USBCMD, cmd);
    if (!xhci_wait_for(g_xhci.op_base, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, true, XHCI_TIMEOUT_MS)) {
        boot_message(WARNING, "[xHCI] Stop timeout");
    }
    // Set the HCRST bit to 1.
    cmd = xhci_read32(g_xhci.op_base, XHCI_OP_USBCMD);
    cmd |= XHCI_USBCMD_HCRST;
    xhci_write32(g_xhci.op_base, XHCI_OP_USBCMD, cmd);

    if (!xhci_wait_for(g_xhci.op_base,
                       XHCI_OP_USBCMD,
                       XHCI_USBCMD_HCRST,
                       false,
                       XHCI_RESET_TIMEOUT_MS)) {
        boot_message(ERROR, "[xHCI] Reset timeout");
        return;
    }

    // Wait for the controller to be ready.
    if (!xhci_wait_for(g_xhci.op_base,
                       XHCI_OP_USBSTS,
                       XHCI_USBSTS_CNR,
                       false,
                       XHCI_RESET_TIMEOUT_MS)) {
        boot_message(ERROR, "[xHCI] Controller not ready");
        return;
    }

    const size_t dcbaa_bytes = (g_xhci.max_slots + 1u) * sizeof(uint64_t);
    if (!xhci_alloc_pages(dcbaa_bytes, &g_xhci.dcbaa_phys, (void **)&g_xhci.dcbaa)) {
        boot_message(ERROR, "[xHCI] DCBAA alloc failed");
        return;
    }

    if (!xhci_setup_scratchpads(&g_xhci)) {
        boot_message(ERROR, "[xHCI] Scratchpad alloc failed");
        return;
    }

    xhci_write32(g_xhci.op_base, XHCI_OP_PAGESIZE, 1u);
    xhci_write64(g_xhci.op_base, XHCI_OP_DCBAAP, g_xhci.dcbaa_phys);

    if (!xhci_event_ring_init(&g_xhci.event_ring, XHCI_EVENT_RING_TRBS)) {
        boot_message(ERROR, "[xHCI] Event ring alloc failed");
        return;
    }

    auto ir_base = (volatile uint8_t *)(g_xhci.rt_base + XHCI_RT_IR_BASE);
    xhci_write32(ir_base, XHCI_IMAN, 0x1u);
    xhci_write32(ir_base, XHCI_IMOD, 0);
    xhci_write32(ir_base, XHCI_ERSTSZ, 1u);
    xhci_write64(ir_base, XHCI_ERSTBA, g_xhci.event_ring.erst_phys);
    xhci_write64(ir_base, XHCI_ERDP, g_xhci.event_ring.phys | XHCI_ERDP_EHB);

    const uint32_t slots = g_xhci.max_slots ? g_xhci.max_slots : 1u;
    xhci_write32(g_xhci.op_base, XHCI_OP_CONFIG, slots);

    xhci_write64(g_xhci.op_base, XHCI_OP_CRCR, g_xhci.cmd_ring.phys | XHCI_TRB_CYCLE);
    cmd = xhci_read32(g_xhci.op_base, XHCI_OP_USBCMD);
    cmd |= XHCI_USBCMD_RS;
    cmd &= ~XHCI_USBCMD_INTE;
    xhci_write32(g_xhci.op_base, XHCI_OP_USBCMD, cmd);
    if (!xhci_wait_for(g_xhci.op_base, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, false, XHCI_TIMEOUT_MS)) {
        boot_message(WARNING, "[xHCI] Start timeout");
    }
    const uint32_t usbcmd = xhci_read32(g_xhci.op_base, XHCI_OP_USBCMD);
    const uint32_t usbsts = xhci_read32(g_xhci.op_base, XHCI_OP_USBSTS);
    boot_message(INFO, "[xHCI] started usbcmd=0x%08x usbsts=0x%08x", usbcmd, usbsts);
    xhci_power_ports(&g_xhci);
    tsc_sleep_ms(XHCI_PORT_CONNECT_DELAY_MS);
    xhci_scan_ports(&g_xhci);
}
