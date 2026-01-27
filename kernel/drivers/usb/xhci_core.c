#include <drivers/usb/xhci.h>
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
#define XHCI_RT_IR_BASE 0x20u // Interrupter 0 base in runtime space.
#define XHCI_IMAN 0x00u // Interrupter management offset.
#define XHCI_IMOD 0x04u // Interrupter moderation offset.
#define XHCI_ERSTSZ 0x08u // Event ring segment table size offset.
#define XHCI_ERSTBA 0x10u // Event ring segment table base address offset.
#define XHCI_ERDP 0x18u // Event ring dequeue pointer offset.
#define XHCI_MMIO_MAP_BYTES 0x100000u // MMIO mapping size.

#define XHCI_USBCMD_RS (1u << 0) // Run/Stop bit.
#define XHCI_USBCMD_HCRST (1u << 1) // Host controller reset bit.
#define XHCI_USBSTS_HCH (1u << 0) // Host controller halted bit.
#define XHCI_USBSTS_CNR (1u << 11) // Controller not ready bit.

#define XHCI_TRB_CYCLE 0x1u // TRB cycle bit.
#define XHCI_TRB_TC (1u << 1) // Link TRB toggle cycle bit.
#define XHCI_TRB_TYPE_SHIFT 10u // TRB type field shift.
#define XHCI_TRB_TYPE_MASK (0x3Fu << XHCI_TRB_TYPE_SHIFT) // TRB type field mask.
#define XHCI_TRB_TYPE_LINK 6u // Link TRB type.
#define XHCI_TRB_TYPE_ENABLE_SLOT 9u // Enable Slot command TRB type.
#define XHCI_TRB_TYPE_CMD_COMPLETION 33u // Command completion event TRB type.
#define XHCI_TRB_TYPE_PORT_STATUS 34u // Port status change event TRB type.
#define XHCI_COMPLETION_SUCCESS 1u // Command completion code for success.
#define XHCI_ERDP_EHB (1ull << 3) // Event handler busy bit in ERDP.
#define XHCI_CMD_RING_TRBS 256u // Command ring TRB count.
#define XHCI_EVENT_RING_TRBS 256u // Event ring TRB count.

#define XHCI_TIMEOUT_MS 200u // Generic timeout in ms.
#define XHCI_WAIT_SPIN_COUNT 256u // Spin count before sleeping in wait loops.
#define XHCI_WAIT_SLEEP_NS 50000ull // Sleep duration in ns after spin budget.
#define XHCI_RESET_TIMEOUT_MS 1000u // Reset timeout in ms.

struct xhci_trb
{
    uint32_t dword0; // TRB payload dword 0.
    uint32_t dword1; // TRB payload dword 1.
    uint32_t dword2; // TRB payload dword 2.
    uint32_t dword3; // TRB control and type dword.
} __attribute__((packed));

struct xhci_erst_entry
{
    uint64_t addr;     // Physical base of event ring segment.
    uint32_t size;     // TRB count in this segment.
    uint32_t reserved; // Reserved, must be zero.
} __attribute__((packed));

struct xhci_ring
{
    struct xhci_trb *trbs; // Virtual base of TRB ring.
    uint32_t trb_count;    // Number of TRBs in the ring.
    uint32_t enqueue;      // Producer index.
    bool cycle;            // Producer cycle state.
    uintptr_t phys;        // Physical base address of ring.
};

struct xhci_event_ring
{
    struct xhci_trb *trbs;        // Virtual base of event TRBs.
    uint32_t trb_count;           // TRB count in the segment.
    uint32_t dequeue;             // Consumer index.
    bool cycle;                   // Consumer cycle state.
    uintptr_t phys;               // Physical base address of TRBs.
    struct xhci_erst_entry *erst; // Virtual ERST base.
    uintptr_t erst_phys;          // Physical ERST base.
};

struct xhci_controller
{
    volatile uint8_t *mmio;            // MMIO base.
    volatile uint8_t *op_base;         // Operational registers base.
    uint8_t cap_len;                   // Capability length in bytes.
    uint32_t max_slots;                // Max device slots.
    uint32_t max_ports;                // Max root hub ports.
    uint32_t context_size;             // Context size in bytes.
    uint32_t max_scratchpad;           // Scratchpad buffer count.
    uint64_t *dcbaa;                   // Virtual DCBAA base.
    uintptr_t dcbaa_phys;              // Physical DCBAA base.
    volatile uint8_t *db_base;         // Doorbell array base.
    volatile uint8_t *rt_base;         // Runtime registers base.
    struct xhci_ring cmd_ring;         // Command ring state.
    struct xhci_event_ring event_ring; // Event ring state.
};

static struct xhci_controller g_xhci;

static inline uint32_t xhci_read32(const volatile uint8_t *base, const uint32_t offset)
{
    auto reg = (const volatile uint32_t *)(base + offset);
    uint32_t value;
    __asm__ volatile("mov %0, %1" : "=r"(value) : "m"(*reg));
    return value;
}

static inline void xhci_write32(volatile uint8_t *base, const uint32_t offset, const uint32_t value)
{
    auto reg = (volatile uint32_t *)(base + offset);
    __asm__ volatile("mov %0, %1" : "=m"(*reg) : "r"(value) : "memory");
}

static inline void xhci_write64(volatile uint8_t *base, const uint32_t offset, const uint64_t value)
{
    auto reg = (volatile uint64_t *)(base + offset);
    __asm__ volatile("mov %0, %1" : "=m"(*reg) : "r"(value) : "memory");
}

static inline void xhci_mb(void)
{
    __asm__ volatile("" ::: "memory");
}

static inline void xhci_wait_relax(uint32_t *spins)
{
    if (*spins < XHCI_WAIT_SPIN_COUNT) {
        __asm__ volatile("pause");
        (*spins)++;
    } else {
        tsc_sleep_ns(XHCI_WAIT_SLEEP_NS);
    }
}

static bool xhci_wait_for(const volatile uint8_t *base,
                          const uint32_t offset,
                          const uint32_t mask,
                          const bool set,
                          const uint32_t timeout_ms)
{
    for (uint32_t i = 0; i < timeout_ms; i++) {
        const uint32_t value = xhci_read32(base, offset);
        if (set) {
            if ((value & mask) == mask) {
                return true;
            }
        } else {
            if ((value & mask) == 0) {
                return true;
            }
        }
        tsc_sleep_ms(1);
    }

    return false;
}

static inline pml4_t xhci_current_pml4(void)
{
    uint64_t cr3 = 0;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    return (pml4_t)(cr3 & 0x000FFFFFFFFFF000ull);
}

static void xhci_map_mmio_range(const uint64_t phys_base, const uint64_t bytes)
{
    const uint64_t start = phys_base & ~(uint64_t)(PAGE_SIZE - 1u);
    const uint64_t end   = (phys_base + bytes + PAGE_SIZE - 1u) & ~(uint64_t)(PAGE_SIZE - 1u);
    pml4_t pml4          = xhci_current_pml4();
    for (uint64_t phys = start; phys < end; phys += PAGE_SIZE) {
        const uint64_t virt = phys + g_hhdm_offset;
        if (vmm_virt_to_phys(pml4, virt) == 0) {
            vmm_map_page(pml4, virt, phys, PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT);
        }
    }
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

static bool xhci_alloc_pages(const size_t bytes, uintptr_t *phys_out, void **virt_out)
{
    const size_t pages = (bytes + PAGE_SIZE - 1u) / PAGE_SIZE;
    void *phys         = pmm_alloc_pages(pages);
    if (!phys) {
        return false;
    }

    auto virt = (void *)((uintptr_t)phys + g_hhdm_offset);
    memset(virt, 0, pages * PAGE_SIZE);

    *phys_out = (uintptr_t)phys;
    *virt_out = virt;
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

static uintptr_t xhci_ring_enqueue(struct xhci_ring *ring, const struct xhci_trb *trb)
{
    const uint32_t index  = ring->enqueue;
    struct xhci_trb *dest = &ring->trbs[index];
    *dest                 = *trb;
    dest->dword3          |= ring->cycle ? XHCI_TRB_CYCLE : 0u;

    ring->enqueue++;
    if (ring->enqueue >= ring->trb_count - 1u) {
        struct xhci_trb *link = &ring->trbs[ring->trb_count - 1u];
        link->dword3          = (link->dword3 & ~XHCI_TRB_CYCLE) | (ring->cycle ? XHCI_TRB_CYCLE : 0u);
        ring->enqueue         = 0;
        ring->cycle           = !ring->cycle;
    }

    return ring->phys + (index * sizeof(struct xhci_trb));
}

static bool xhci_ring_init(struct xhci_ring *ring, const uint32_t trb_count)
{
    const size_t bytes = trb_count * sizeof(struct xhci_trb);
    uintptr_t phys     = 0;
    void *virt         = nullptr;
    if (!xhci_alloc_pages(bytes, &phys, &virt)) {
        return false;
    }

    ring->trbs      = (struct xhci_trb *)virt;
    ring->trb_count = trb_count;
    ring->enqueue   = 0;
    ring->cycle     = true;
    ring->phys      = phys;

    struct xhci_trb link = {0};
    link.dword0          = (uint32_t)phys;
    link.dword1          = (uint32_t)(phys >> 32);
    link.dword3          = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_TC;
    ring->enqueue        = trb_count - 1u;
    (void)xhci_ring_enqueue(ring, &link);
    ring->enqueue        = 0;
    ring->cycle          = true;

    return true;
}

static bool xhci_event_ring_init(struct xhci_event_ring *ring, const uint32_t trb_count)
{
    const size_t bytes = trb_count * sizeof(struct xhci_trb);
    uintptr_t phys     = 0;
    void *virt         = nullptr;
    if (!xhci_alloc_pages(bytes, &phys, &virt)) {
        return false;
    }

    uintptr_t erst_phys = 0;
    void *erst_virt     = nullptr;
    if (!xhci_alloc_pages(sizeof(struct xhci_erst_entry), &erst_phys, &erst_virt)) {
        return false;
    }

    ring->trbs      = (struct xhci_trb *)virt;
    ring->trb_count = trb_count;
    ring->dequeue   = 0;
    ring->cycle     = true;
    ring->phys      = phys;
    ring->erst      = (struct xhci_erst_entry *)erst_virt;
    ring->erst_phys = erst_phys;

    ring->erst[0].addr     = phys;
    ring->erst[0].size     = trb_count;
    ring->erst[0].reserved = 0;

    return true;
}

static uint32_t xhci_trb_type(const struct xhci_trb *trb)
{
    return (trb->dword3 & XHCI_TRB_TYPE_MASK) >> XHCI_TRB_TYPE_SHIFT;
}

static void xhci_event_ring_advance(struct xhci_controller *xhci)
{
    struct xhci_event_ring *ring = &xhci->event_ring;
    ring->dequeue++;
    if (ring->dequeue >= ring->trb_count) {
        ring->dequeue = 0;
        ring->cycle   = !ring->cycle;
    }

    const uintptr_t erdp  = ring->phys + (ring->dequeue * sizeof(struct xhci_trb));
    const uintptr_t value = erdp | XHCI_ERDP_EHB;
    auto ir_base          = (volatile uint8_t *)(xhci->rt_base + XHCI_RT_IR_BASE);
    xhci_write64(ir_base, XHCI_ERDP, value);
}

static bool xhci_wait_for_cmd_completion(struct xhci_controller *xhci,
                                         const uintptr_t cmd_phys,
                                         uint8_t *slot_id_out)
{
    constexpr uint64_t timeout_ns = (uint64_t)XHCI_TIMEOUT_MS * 1000000ull;
    const uint64_t start          = tsc_nanos();
    uint32_t spins                = 0;
    while (tsc_nanos() - start < timeout_ns) {
        struct xhci_event_ring *ring = &xhci->event_ring;
        struct xhci_trb *trb         = &ring->trbs[ring->dequeue];
        const bool cycle             = (trb->dword3 & XHCI_TRB_CYCLE) != 0;
        if (cycle == ring->cycle) {
            const uint32_t type = xhci_trb_type(trb);
            if (type == XHCI_TRB_TYPE_CMD_COMPLETION) {
                const uint64_t ptr        = ((uint64_t)trb->dword1 << 32) | trb->dword0;
                const uint32_t completion = trb->dword2 >> 24;
                const uint8_t slot_id     = (uint8_t)(trb->dword3 >> 24);
                xhci_event_ring_advance(xhci);

                if (cmd_phys != 0 && ptr != cmd_phys) {
                    boot_message(WARNING,
                                 "[xHCI] Command completion mismatch ptr=0x%lx expected=0x%lx",
                                 (unsigned long)ptr,
                                 (unsigned long)cmd_phys);
                    spins = 0;
                    continue;
                }

                if (completion != XHCI_COMPLETION_SUCCESS) {
                    boot_message(WARNING,
                                 "[xHCI] Command completion failed code=%u",
                                 completion);
                    return false;
                }

                if (slot_id_out) {
                    *slot_id_out = slot_id;
                }
                return true;
            }

            if (type == XHCI_TRB_TYPE_PORT_STATUS) {
                xhci_event_ring_advance(xhci);
                spins = 0;
                continue;
            }

            xhci_event_ring_advance(xhci);
            spins = 0;
        } else {
            xhci_wait_relax(&spins);
        }
    }

    boot_message(WARNING, "[xHCI] Command completion timed out");
    return false;
}

static void xhci_ring_doorbell(const struct xhci_controller *xhci, const uint8_t doorbell, const uint32_t value)
{
    auto db = (volatile uint8_t *)(xhci->db_base + ((uint32_t)doorbell * 4u));
    xhci_mb();
    xhci_write32(db, 0, value);
}

static bool xhci_cmd_submit(struct xhci_controller *xhci, const struct xhci_trb *trb, uint8_t *slot_id_out)
{
    if (!trb) {
        return false;
    }

    const uintptr_t phys = xhci_ring_enqueue(&xhci->cmd_ring, trb);
    xhci_ring_doorbell(xhci, 0, 0);
    return xhci_wait_for_cmd_completion(xhci, phys, slot_id_out);
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

    xhci_map_mmio_range(mmio_phys, XHCI_MMIO_MAP_BYTES);
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

    uint32_t cmd = xhci_read32(g_xhci.op_base, XHCI_OP_USBCMD);
    cmd          &= ~XHCI_USBCMD_RS;
    xhci_write32(g_xhci.op_base, XHCI_OP_USBCMD, cmd);
    if (!xhci_wait_for(g_xhci.op_base, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, true, XHCI_TIMEOUT_MS)) {
        boot_message(WARNING, "[xHCI] Stop timeout");
    }

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
    xhci_write32(g_xhci.op_base, XHCI_OP_USBCMD, cmd);
    if (!xhci_wait_for(g_xhci.op_base, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, false, XHCI_TIMEOUT_MS)) {
        boot_message(WARNING, "[xHCI] Start timeout");
    }
    const uint32_t usbcmd = xhci_read32(g_xhci.op_base, XHCI_OP_USBCMD);
    const uint32_t usbsts = xhci_read32(g_xhci.op_base, XHCI_OP_USBSTS);
    boot_message(INFO, "[xHCI] started usbcmd=0x%08x usbsts=0x%08x", usbcmd, usbsts);
    struct xhci_trb cmd_trb = {0};
    cmd_trb.dword3          = (XHCI_TRB_TYPE_ENABLE_SLOT << XHCI_TRB_TYPE_SHIFT);
    uint8_t slot_id         = 0;
    if (xhci_cmd_submit(&g_xhci, &cmd_trb, &slot_id)) {
        boot_message(INFO, "[xHCI] Enable slot completed slot=%u", slot_id);
    } else {
        boot_message(WARNING, "[xHCI] Enable slot command failed");
    }
}
