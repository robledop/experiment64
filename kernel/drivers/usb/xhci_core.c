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
#define XHCI_OP_PORTSC_BASE 0x400u // Port status/control base offset.
#define XHCI_OP_PORTSC_STRIDE 0x10u // Port status/control register stride.
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

#define XHCI_PORTSC_CCS (1u << 0) // Port current connect status bit.
#define XHCI_PORTSC_PED (1u << 1) // Port enabled/disabled bit.
#define XHCI_PORTSC_PR (1u << 4) // Port reset bit.
#define XHCI_PORTSC_PLS_SHIFT 5u // Port link state field shift.
#define XHCI_PORTSC_PLS_MASK (0xFu << XHCI_PORTSC_PLS_SHIFT) // Port link state field mask.
#define XHCI_PORTSC_PP (1u << 9) // Port power bit.
#define XHCI_PORTSC_INDICATOR_MASK (0x3u << 14) // Port indicator control mask.
#define XHCI_PORTSC_WRC (1u << 19) // Port warm reset change bit.
#define XHCI_PORTSC_WK_MASK (0x7u << 25) // Port wake event mask.
#define XHCI_PORTSC_WR (1u << 31) // Port warm reset bit.
#define XHCI_PORTSC_SPEED_SHIFT 10u // Port speed field shift.
#define XHCI_PORTSC_SPEED_MASK (0xFu << XHCI_PORTSC_SPEED_SHIFT) // Port speed field mask.
#define XHCI_PORTSC_RWS_MASK (XHCI_PORTSC_PLS_MASK | XHCI_PORTSC_PP | XHCI_PORTSC_INDICATOR_MASK | XHCI_PORTSC_WK_MASK) // Port read/write settable bits.


#define XHCI_SLOT_CTX_SPEED_SHIFT 20u // Slot context speed field shift.
#define XHCI_SLOT_CTX_ROOT_PORT_SHIFT 16u // Slot context root port field shift.
#define XHCI_MAX_DEVICES 256u // Max device slots tracked.

#define XHCI_TRB_CYCLE 0x1u // TRB cycle bit.
#define XHCI_TRB_TC (1u << 1) // Link TRB toggle cycle bit.
#define XHCI_TRB_ISP (1u << 2) // Interrupt on short packet bit.
#define XHCI_TRB_IOC (1u << 5) // Interrupt on completion bit.
#define XHCI_TRB_IDT (1u << 6) // Immediate data bit.
#define XHCI_TRB_DIR_IN (1u << 16) // Data-in direction bit.
#define XHCI_TRB_TYPE_MASK (0x3Fu << XHCI_TRB_TYPE_SHIFT) // TRB type field mask.
#define XHCI_TRB_TRT_SHIFT 16u // Transfer type field shift for setup stage.
#define XHCI_TRB_TRT_DATA_OUT 2u // Setup stage transfer type: data out.
#define XHCI_TRB_TRT_DATA_IN 3u // Setup stage transfer type: data in.
#define XHCI_TRB_LEN_MASK 0x1FFFFu // TRB transfer length mask.
#define XHCI_TRB_INTR_TARGET_SHIFT 22u // TRB interrupter target shift.
#define XHCI_TRB_LEN(value) ((value) & XHCI_TRB_LEN_MASK) // TRB transfer length field.
#define XHCI_TRB_INTR_TARGET(value) ((value) << XHCI_TRB_INTR_TARGET_SHIFT) // TRB interrupter target field.
#define XHCI_TRB_TYPE_SETUP_STAGE 2u // Setup stage TRB type.
#define XHCI_TRB_TYPE_DATA_STAGE 3u // Data stage TRB type.
#define XHCI_TRB_TYPE_STATUS_STAGE 4u // Status stage TRB type.
#define XHCI_TRB_TYPE_LINK 6u // Link TRB type.
#define XHCI_TRB_TYPE_ENABLE_SLOT 9u // Enable Slot command TRB type.
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11u // Address Device command TRB type.
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32u // Transfer event TRB type.
#define XHCI_TRB_TYPE_CMD_COMPLETION 33u // Command completion event TRB type.
#define XHCI_TRB_TYPE_PORT_STATUS 34u // Port status change event TRB type.
#define XHCI_COMPLETION_SUCCESS 1u // Command completion code for success.
#define XHCI_COMPLETION_SHORT_PACKET 13u // Transfer completion short packet code.
#define XHCI_ERDP_EHB (1ull << 3) // Event handler busy bit in ERDP.
#define XHCI_CMD_RING_TRBS 256u // Command ring TRB count.
#define XHCI_EVENT_RING_TRBS 256u // Event ring TRB count.
#define XHCI_MAX_CONTEXTS 32u // Max context entries per device context.

#define XHCI_TIMEOUT_MS 200u // Generic timeout in ms.
#define XHCI_TRANSFER_TIMEOUT_MS 1000u // Transfer timeout in ms.
#define XHCI_WAIT_SPIN_COUNT 256u // Spin count before sleeping in wait loops.
#define XHCI_WAIT_SLEEP_NS 50000ull // Sleep duration in ns after spin budget.
#define XHCI_RESET_TIMEOUT_MS 1000u // Reset timeout in ms.
#define XHCI_PORT_RESET_TIMEOUT_MS 500u // Port reset timeout in ms.
#define XHCI_PORT_POWER_DELAY_MS 20u // Port power settle delay in ms.
#define XHCI_PORT_CONNECT_DELAY_MS 100u // Port connect debounce delay in ms.
#define XHCI_ADDRESS_SETTLE_MS 50u // Address settle delay in ms.

#define USB_DESC_TYPE_DEVICE 0x01u // USB device descriptor type.
#define USB_DESC_TYPE_CONFIGURATION 0x02u // USB configuration descriptor type.
#define USB_REQ_GET_DESCRIPTOR 0x06u // USB standard GET_DESCRIPTOR request.
#define USB_REQ_SET_CONFIGURATION 0x09u // USB standard SET_CONFIGURATION request.

static struct xhci_controller g_xhci;
static struct xhci_device g_xhci_devices[XHCI_MAX_DEVICES];

static bool xhci_port_reset(const struct xhci_controller *xhci,
                            const uint32_t port,
                            uint32_t *portsc_out);

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

uintptr_t xhci_ring_enqueue(struct xhci_ring *ring, const struct xhci_trb *trb)
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

bool xhci_ring_init(struct xhci_ring *ring, const uint32_t trb_count)
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
    ring->enqueue = 0;
    ring->cycle   = true;

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

static bool xhci_alloc_device_context(struct xhci_controller *xhci, struct xhci_device *dev)
{
    if (!dev) {
        return false;
    }

    if (dev->device_ctx) {
        return true;
    }

    const size_t bytes = (size_t)XHCI_MAX_CONTEXTS * xhci->context_size;
    uintptr_t phys     = 0;
    void *virt         = nullptr;
    if (!xhci_alloc_pages(bytes, &phys, &virt)) {
        return false;
    }

    dev->device_ctx      = virt;
    dev->device_ctx_phys = phys;
    memset(dev->device_ctx, 0, bytes);
    xhci->dcbaa[dev->slot_id] = phys;
    return true;
}

static bool xhci_find_connected_port(const struct xhci_controller *xhci, uint32_t *port_out, uint32_t *speed_out)
{
    for (uint32_t port = 1; port <= xhci->max_ports; port++) {
        const uint32_t offset = XHCI_OP_PORTSC_BASE + ((port - 1u) * XHCI_OP_PORTSC_STRIDE);
        const uint32_t portsc = xhci_read32(xhci->op_base, offset);
        if ((portsc & XHCI_PORTSC_CCS) == 0) {
            continue;
        }

        if (port_out) {
            *port_out = port;
        }
        if (speed_out) {
            *speed_out = (portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
        }
        return true;
    }

    return false;
}

static bool xhci_prepare_slot_context(struct xhci_controller *xhci, struct xhci_device *dev)
{
    if (!dev) {
        return false;
    }

    uint32_t port  = 0;
    uint32_t speed = 0;
    if (!xhci_find_connected_port(xhci, &port, &speed)) {
        boot_message(WARNING, "[xHCI] No connected ports for slot %u", dev->slot_id);
        return false;
    }

    uint32_t portsc_after = 0;
    if (!xhci_port_reset(xhci, port, &portsc_after)) {
        return false;
    }
    speed = (portsc_after & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;

    const size_t ctx_size     = xhci->context_size;
    const size_t input_offset = (ctx_size == 64u) ? 64u : 32u;
    const size_t input_bytes  = input_offset + (XHCI_MAX_CONTEXTS * ctx_size);
    if (!dev->input_ctx) {
        if (!xhci_alloc_pages(input_bytes, &dev->input_ctx_phys, &dev->input_ctx)) {
            return false;
        }
    }

    memset(dev->input_ctx, 0, input_bytes);
    dev->port_id = (uint8_t)port;
    dev->speed   = speed;

    auto ctrl       = (struct xhci_input_control_ctx *)dev->input_ctx;
    ctrl->add_flags = 0x3u;

    auto slot_ctx      = (struct xhci_slot_ctx *)xhci_input_context_ptr(dev->input_ctx, 0, (uint32_t)ctx_size);
    slot_ctx->dev_info = (speed << XHCI_SLOT_CTX_SPEED_SHIFT) |
        (1u << XHCI_SLOT_CTX_CTX_ENTRIES_SHIFT);
    slot_ctx->dev_info2 = (port << XHCI_SLOT_CTX_ROOT_PORT_SHIFT);

    if (!dev->ep0_ring.trbs) {
        if (!xhci_ring_init(&dev->ep0_ring, XHCI_EP0_RING_TRBS)) {
            return false;
        }
    }

    auto ep0_ctx                  = (struct xhci_ep_ctx *)xhci_input_context_ptr(dev->input_ctx, 1, (uint32_t)ctx_size);
    constexpr uint16_t max_packet = 512u;
    ep0_ctx->ep_info              = 0;
    ep0_ctx->ep_info2             = (3u << XHCI_EP_ERROR_COUNT_SHIFT) |
        (XHCI_EP_TYPE_CONTROL << XHCI_EP_TYPE_SHIFT) |
        ((uint32_t)max_packet << XHCI_EP_MAX_PACKET_SHIFT);
    const uintptr_t deq = dev->ep0_ring.phys + (dev->ep0_ring.enqueue * sizeof(struct xhci_trb));
    ep0_ctx->deq        = deq | (dev->ep0_ring.cycle ? 1u : 0u);
    ep0_ctx->tx_info    = 8u;

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

bool xhci_wait_for_cmd_completion(struct xhci_controller *xhci,
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

static bool xhci_wait_for_transfer_event(struct xhci_controller *xhci,
                                         const uintptr_t trb_phys,
                                         const uint8_t slot_id,
                                         const uint8_t ep_id,
                                         const bool require_ptr_match)
{
    constexpr uint64_t timeout_ns = (uint64_t)XHCI_TRANSFER_TIMEOUT_MS * 1000000ull;
    const uint64_t start          = tsc_nanos();
    uint32_t spins                = 0;
    while (tsc_nanos() - start < timeout_ns) {
        struct xhci_event_ring *ring = &xhci->event_ring;
        struct xhci_trb *trb         = &ring->trbs[ring->dequeue];
        const bool cycle             = (trb->dword3 & XHCI_TRB_CYCLE) != 0;
        if (cycle == ring->cycle) {
            const uint32_t type = xhci_trb_type(trb);
            if (type == XHCI_TRB_TYPE_TRANSFER_EVENT) {
                const uint64_t ptr        = ((uint64_t)trb->dword1 << 32) | trb->dword0;
                const uint32_t completion = trb->dword2 >> 24;
                const uint8_t event_slot  = (uint8_t)(trb->dword3 >> 24);
                const uint8_t event_ep    = (uint8_t)((trb->dword3 >> 16) & 0x1Fu);
                xhci_event_ring_advance(xhci);

                const bool ep_match = event_ep == ep_id || (ep_id == 1u && event_ep == 0u);
                if (event_slot != slot_id || !ep_match) {
                    spins = 0;
                    continue;
                }

                if (require_ptr_match && trb_phys != 0 && ptr != trb_phys) {
                    spins = 0;
                    continue;
                }

                if (completion != XHCI_COMPLETION_SUCCESS && completion != XHCI_COMPLETION_SHORT_PACKET) {
                    return false;
                }

                return true;
            }

            xhci_event_ring_advance(xhci);
            spins = 0;
        } else {
            xhci_wait_relax(&spins);
        }
    }

    boot_message(WARNING, "[xHCI] Transfer completion timed out");
    return false;
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

static bool xhci_wait_port_ready(const struct xhci_controller *xhci,
                                 const uint32_t port,
                                 const uint32_t timeout_ms,
                                 uint32_t *portsc_out)
{
    const uint32_t offset = XHCI_OP_PORTSC_BASE + ((port - 1u) * XHCI_OP_PORTSC_STRIDE);
    uint32_t portsc       = 0;

    for (uint32_t i = 0; i < timeout_ms; i++) {
        portsc             = xhci_read32(xhci->op_base, offset);
        const uint32_t pls = (portsc & XHCI_PORTSC_PLS_MASK) >> 5u;
        if ((portsc & XHCI_PORTSC_PED) != 0 && pls == 0) {
            if (portsc_out) {
                *portsc_out = portsc;
            }
            return true;
        }
        tsc_sleep_ms(1);
    }

    if (portsc_out) {
        *portsc_out = portsc;
    }
    return false;
}

static bool xhci_port_reset(const struct xhci_controller *xhci,
                            const uint32_t port,
                            uint32_t *portsc_out)
{
    const uint32_t offset = XHCI_OP_PORTSC_BASE + ((port - 1u) * XHCI_OP_PORTSC_STRIDE);
    uint32_t portsc       = xhci_read32(xhci->op_base, offset);

    const uint32_t preserve = (portsc & XHCI_PORTSC_RWS_MASK) & ~XHCI_PORTSC_PLS_MASK;
    uint32_t write          = preserve | XHCI_PORTSC_PP | XHCI_PORTSC_WR;
    xhci_write32(xhci->op_base, offset, write);

    if (!xhci_wait_for(xhci->op_base, offset, XHCI_PORTSC_WRC, true, XHCI_PORT_RESET_TIMEOUT_MS)) {
        portsc = xhci_read32(xhci->op_base, offset);
        boot_message(WARNING, "[xHCI] Port %u warm reset timeout status=0x%08x", port, portsc);
        return false;
    }

    portsc               = xhci_read32(xhci->op_base, offset);
    uint32_t clear_reset = (portsc & XHCI_PORTSC_RWS_MASK) | XHCI_PORTSC_PP | XHCI_PORTSC_WRC;
    xhci_write32(xhci->op_base, offset, clear_reset);

    if (!xhci_wait_port_ready(xhci, port, XHCI_PORT_RESET_TIMEOUT_MS, &portsc)) {
        boot_message(WARNING, "[xHCI] Port %u reset incomplete status=0x%08x", port, portsc);
        if (portsc_out) {
            *portsc_out = portsc;
        }
        return false;
    }

    if (portsc_out) {
        *portsc_out = portsc;
    }
    return true;
}

void xhci_ring_doorbell(const struct xhci_controller *xhci, const uint8_t doorbell, const uint32_t value)
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

static bool xhci_address_device(struct xhci_controller *xhci, struct xhci_device *dev)
{
    if (!xhci || !dev) {
        return false;
    }

    struct xhci_trb cmd = {0};
    cmd.dword0          = (uint32_t)dev->input_ctx_phys;
    cmd.dword1          = (uint32_t)(dev->input_ctx_phys >> 32);
    cmd.dword3          = (XHCI_TRB_TYPE_ADDRESS_DEVICE << XHCI_TRB_TYPE_SHIFT) |
        ((uint32_t)dev->slot_id << 24);
    if (!xhci_cmd_submit(xhci, &cmd, nullptr)) {
        boot_message(WARNING, "[xHCI] Slot %u address device failed", dev->slot_id);
        return false;
    }

    boot_message(INFO, "[xHCI] Slot %u addressed", dev->slot_id);
    return true;
}

static bool xhci_control_transfer(struct xhci_controller *xhci,
                                  struct xhci_device *dev,
                                  const struct usb_setup_packet *setup,
                                  const uintptr_t data_phys,
                                  const uint32_t data_len,
                                  const bool data_in)
{
    if (!xhci || !dev || !setup || !dev->ep0_ring.trbs) {
        return false;
    }

    struct xhci_trb setup_trb = {0};
    uint64_t setup_data       = 0;
    memcpy(&setup_data, setup, sizeof(*setup));
    setup_trb.dword0 = (uint32_t)setup_data;
    setup_trb.dword1 = (uint32_t)(setup_data >> 32);
    setup_trb.dword2 = XHCI_TRB_LEN(sizeof(*setup)) | XHCI_TRB_INTR_TARGET(0);
    setup_trb.dword3 = (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IDT;

    struct xhci_trb data_trb = {0};
    if (data_len > 0) {
        setup_trb.dword3 |= (data_in ? XHCI_TRB_TRT_DATA_IN : XHCI_TRB_TRT_DATA_OUT) << XHCI_TRB_TRT_SHIFT;

        data_trb.dword0 = (uint32_t)data_phys;
        data_trb.dword1 = (uint32_t)(data_phys >> 32);
        data_trb.dword2 = XHCI_TRB_LEN(data_len) | XHCI_TRB_INTR_TARGET(0);
        data_trb.dword3 = (XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT);
        if (data_in) {
            data_trb.dword3 |= XHCI_TRB_DIR_IN | XHCI_TRB_ISP;
        }
    }

    struct xhci_trb status_trb = {0};
    status_trb.dword3          = (XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC;
    const bool status_in       = data_len == 0 ? true : !data_in;
    if (status_in) {
        status_trb.dword3 |= XHCI_TRB_DIR_IN;
    }

    (void)xhci_ring_enqueue(&dev->ep0_ring, &setup_trb);
    if (data_len > 0) {
        (void)xhci_ring_enqueue(&dev->ep0_ring, &data_trb);
    }
    const uintptr_t status_phys = xhci_ring_enqueue(&dev->ep0_ring, &status_trb);

    xhci_ring_doorbell(xhci, dev->slot_id, 1u);

    return xhci_wait_for_transfer_event(xhci,
                                        status_phys,
                                        dev->slot_id,
                                        1u,
                                        false);
}

static bool xhci_set_configuration(struct xhci_controller *xhci,
                                   struct xhci_device *dev,
                                   const uint8_t config_value)
{
    struct usb_setup_packet setup = {
        .bm_request_type = 0x00,
        .b_request = USB_REQ_SET_CONFIGURATION,
        .w_value = config_value,
        .w_index = 0,
        .w_length = 0,
    };

    return xhci_control_transfer(xhci, dev, &setup, 0, 0, false);
}

static bool xhci_get_device_descriptor(struct xhci_controller *xhci, struct xhci_device *dev)
{
    uintptr_t desc_phys = 0;
    void *desc_buf      = nullptr;
    if (!xhci_alloc_pages(64u, &desc_phys, &desc_buf)) {
        boot_message(ERROR, "[xHCI] Descriptor buffer alloc failed");
        return false;
    }

    memset(desc_buf, 0, 64u);
    struct usb_setup_packet setup = {
        .bm_request_type = 0x80,
        .b_request = USB_REQ_GET_DESCRIPTOR,
        .w_value = (USB_DESC_TYPE_DEVICE << 8),
        .w_index = 0,
        .w_length = 18,
    };

    if (!xhci_control_transfer(xhci, dev, &setup, desc_phys, setup.w_length, true)) {
        boot_message(WARNING, "[xHCI] Slot %u GET_DESCRIPTOR failed", dev->slot_id);
        return false;
    }

    auto desc = (const struct usb_device_descriptor *)desc_buf;

    boot_message(INFO,
                 "[xHCI] Slot %u USB %x.%02x VID=%04x (%s) PID=%04x class=%02x",
                 dev->slot_id,
                 (unsigned)((desc->bcd_usb >> 8) & 0xFFu),
                 (unsigned)(desc->bcd_usb & 0xFFu),
                 (unsigned)desc->id_vendor,
                 pci_find_vendor(desc->id_vendor),
                 (unsigned)desc->id_product,
                 (unsigned)desc->b_device_class);
    return true;
}

static bool xhci_get_config_descriptor(struct xhci_controller *xhci, struct xhci_device *dev)
{
    uintptr_t desc_phys = 0;
    void *desc_buf      = nullptr;
    if (!xhci_alloc_pages(64u, &desc_phys, &desc_buf)) {
        boot_message(ERROR, "[xHCI] Config descriptor buffer alloc failed");
        return false;
    }

    memset(desc_buf, 0, 64u);
    struct usb_setup_packet setup = {
        .bm_request_type = 0x80,
        .b_request = USB_REQ_GET_DESCRIPTOR,
        .w_value = (USB_DESC_TYPE_CONFIGURATION << 8),
        .w_index = 0,
        .w_length = sizeof(struct usb_config_descriptor),
    };

    if (!xhci_control_transfer(xhci, dev, &setup, desc_phys, setup.w_length, true)) {
        boot_message(WARNING, "[xHCI] Slot %u GET_DESCRIPTOR(CONFIG) failed", dev->slot_id);
        return false;
    }

    auto cfg = (const struct usb_config_descriptor *)desc_buf;
    if (cfg->b_length < sizeof(struct usb_config_descriptor) ||
        cfg->b_descriptor_type != USB_DESC_TYPE_CONFIGURATION ||
        cfg->w_total_length < sizeof(struct usb_config_descriptor)) {
        boot_message(WARNING,
                     "[xHCI] Slot %u invalid config descriptor header len=%u type=%u total=%u",
                     dev->slot_id,
                     cfg->b_length,
                     cfg->b_descriptor_type,
                     cfg->w_total_length);
        return false;
    }

    const uint16_t total_len = cfg->w_total_length;
    boot_message(INFO,
                 "[xHCI] Slot %u config total=%u interfaces=%u value=%u",
                 dev->slot_id,
                 total_len,
                 cfg->b_num_interfaces,
                 cfg->b_configuration_value);

    uintptr_t full_phys = desc_phys;
    void *full_buf      = desc_buf;
    if (total_len > 64u) {
        if (!xhci_alloc_pages(total_len, &full_phys, &full_buf)) {
            boot_message(ERROR, "[xHCI] Config descriptor full alloc failed");
            return false;
        }
    }

    memset(full_buf, 0, total_len);
    setup.w_length = total_len;
    if (!xhci_control_transfer(xhci, dev, &setup, full_phys, total_len, true)) {
        boot_message(WARNING, "[xHCI] Slot %u GET_DESCRIPTOR(CONFIG) full failed", dev->slot_id);
        return false;
    }

    const bool msc_ok = xhci_msc_parse_config(dev, full_buf, total_len);
    if (!msc_ok) {
        return true;
    }

    if (!xhci_set_configuration(xhci, dev, xhci_msc_config_value())) {
        boot_message(WARNING, "[xHCI] Slot %u SET_CONFIGURATION failed", dev->slot_id);
        return false;
    }
    tsc_sleep_ms(20);

    if (!xhci_msc_configure_endpoints(xhci)) {
        return false;
    }
    return true;
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
    xhci_write32(g_xhci.op_base, XHCI_OP_USBCMD, cmd);
    if (!xhci_wait_for(g_xhci.op_base, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, false, XHCI_TIMEOUT_MS)) {
        boot_message(WARNING, "[xHCI] Start timeout");
    }
    const uint32_t usbcmd = xhci_read32(g_xhci.op_base, XHCI_OP_USBCMD);
    const uint32_t usbsts = xhci_read32(g_xhci.op_base, XHCI_OP_USBSTS);
    boot_message(INFO, "[xHCI] started usbcmd=0x%08x usbsts=0x%08x", usbcmd, usbsts);
    xhci_power_ports(&g_xhci);
    tsc_sleep_ms(XHCI_PORT_CONNECT_DELAY_MS);
    xhci_dump_ports(&g_xhci);
    struct xhci_trb cmd_trb = {0};
    cmd_trb.dword3          = (XHCI_TRB_TYPE_ENABLE_SLOT << XHCI_TRB_TYPE_SHIFT);
    uint8_t slot_id         = 0;
    if (xhci_cmd_submit(&g_xhci, &cmd_trb, &slot_id)) {
        boot_message(INFO, "[xHCI] Enable slot completed slot=%u", slot_id);
        struct xhci_device *dev = xhci_device_from_slot(slot_id);
        if (!dev) {
            boot_message(WARNING, "[xHCI] Slot %u out of range", slot_id);
            return;
        }

        memset(dev, 0, sizeof(*dev));
        dev->active  = true;
        dev->slot_id = slot_id;

        const bool ctx_ok = xhci_alloc_device_context(&g_xhci, dev);
        if (!ctx_ok) {
            boot_message(WARNING, "[xHCI] Slot %u device context alloc failed", slot_id);
        }
        const bool input_ok = xhci_prepare_slot_context(&g_xhci, dev);
        if (!input_ok) {
            boot_message(WARNING, "[xHCI] Slot %u input context prep failed", slot_id);
        }
        if (ctx_ok && input_ok) {
            if (xhci_address_device(&g_xhci, dev)) {
                tsc_sleep_ms(XHCI_ADDRESS_SETTLE_MS);
                if (xhci_get_device_descriptor(&g_xhci, dev)) {
                    (void)xhci_get_config_descriptor(&g_xhci, dev);
                }
            }
        }
    } else {
        boot_message(WARNING, "[xHCI] Enable slot command failed");
    }
}
