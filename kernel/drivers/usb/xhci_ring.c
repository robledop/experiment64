#include <drivers/usb/xhci_internal.h>
#include <drivers/terminal.h>

static const char *xhci_completion_name(const uint32_t code)
{
    switch (code) {
    case XHCI_COMPLETION_SUCCESS:
        return "success";
    case XHCI_COMPLETION_SHORT_PACKET:
        return "short";
    case XHCI_COMPLETION_USB_TRANSACTION_ERROR:
        return "usb-tx";
    case XHCI_COMPLETION_STALL_ERROR:
        return "stall";
    case XHCI_COMPLETION_DATA_BUFFER_ERROR:
        return "data";
    case XHCI_COMPLETION_CONTEXT_STATE_ERROR:
        return "context";
    default:
        return "other";
    }
}

void xhci_ring_reset(struct xhci_ring *ring)
{
    if (!ring->trbs || ring->trb_count < 2) {
        return;
    }

    memset(ring->trbs, 0, ring->trb_count * sizeof(struct xhci_trb));
    ring->enqueue = 0;
    ring->cycle   = true;

    struct xhci_trb *link = &ring->trbs[ring->trb_count - 1u];
    link->dword0          = (uint32_t)ring->phys;
    link->dword1          = (uint32_t)(ring->phys >> 32);
    link->dword2          = 0;
    link->dword3          = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_TC | XHCI_TRB_CYCLE;
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

    struct xhci_trb *link = &ring->trbs[trb_count - 1u];
    link->dword0          = (uint32_t)phys;
    link->dword1          = (uint32_t)(phys >> 32);
    link->dword2          = 0;
    link->dword3          = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_TC | XHCI_TRB_CYCLE;

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

bool xhci_event_ring_init(struct xhci_event_ring *ring, const uint32_t trb_count)
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
                                 "[xHCI] Command completion failed code=%u(%s)",
                                 completion,
                                 xhci_completion_name(completion));
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

bool xhci_wait_for_transfer_event(struct xhci_controller *xhci,
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

void xhci_ring_doorbell(const struct xhci_controller *xhci, const uint8_t doorbell, const uint32_t value)
{
    auto db = (volatile uint8_t *)(xhci->db_base + ((uint32_t)doorbell * 4u));
    xhci_mb();
    xhci_write32(db, 0, value);
}
