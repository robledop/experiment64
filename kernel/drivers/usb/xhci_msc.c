#include <drivers/usb/xhci_internal.h>
#include <drivers/terminal.h>
#include <lib/string.h>

static struct xhci_msc_device g_xhci_msc;

uint8_t xhci_msc_config_value(void)
{
    return g_xhci_msc.config_value;
}

bool xhci_msc_parse_config(const struct xhci_device *dev,
                           const void *cfg_buf,
                           const uint16_t total_len)
{
    if (!dev || !cfg_buf || total_len < sizeof(struct usb_config_descriptor)) {
        return false;
    }

    g_xhci_msc.dev                 = (struct xhci_device *)dev;
    g_xhci_msc.config_value        = ((const struct usb_config_descriptor *)cfg_buf)->b_configuration_value;
    g_xhci_msc.interface_number    = 0;
    g_xhci_msc.bulk_in_ep          = 0;
    g_xhci_msc.bulk_out_ep         = 0;
    g_xhci_msc.bulk_in_id          = 0;
    g_xhci_msc.bulk_out_id         = 0;
    g_xhci_msc.bulk_in_max_packet  = 0;
    g_xhci_msc.bulk_out_max_packet = 0;
    g_xhci_msc.bulk_in_max_burst   = 0;
    g_xhci_msc.bulk_out_max_burst  = 0;
    g_xhci_msc.cbw_buf             = nullptr;
    g_xhci_msc.cbw_phys            = 0;
    g_xhci_msc.csw_buf             = nullptr;
    g_xhci_msc.csw_phys            = 0;
    g_xhci_msc.data_buf            = nullptr;
    g_xhci_msc.data_phys           = 0;
    g_xhci_msc.data_bytes          = 0;
    g_xhci_msc.tag                 = 0;

    auto buf               = (const uint8_t *)cfg_buf;
    const uint8_t *end     = buf + total_len;
    bool in_msc_interface  = false;
    bool saw_msc_interface = false;
    uint8_t last_ep        = 0;

    while (buf + 2 <= end) {
        const uint8_t len  = buf[0];
        const uint8_t type = buf[1];
        if (len < 2 || buf + len > end) {
            break;
        }

        if (type == USB_DESC_TYPE_INTERFACE) {
            auto iface       = (const struct usb_interface_descriptor *)buf;
            in_msc_interface = iface->b_interface_class == USB_CLASS_MASS_STORAGE &&
                iface->b_interface_subclass == USB_SUBCLASS_SCSI &&
                iface->b_interface_protocol == USB_PROTOCOL_BOT &&
                iface->b_alternate_setting == 0;
            if (in_msc_interface) {
                saw_msc_interface           = true;
                g_xhci_msc.interface_number = iface->b_interface_number;
            }
        } else if (type == USB_DESC_TYPE_ENDPOINT && in_msc_interface) {
            auto ep = (const struct usb_endpoint_descriptor *)buf;
            if ((ep->bm_attributes & USB_EP_ATTR_TYPE_MASK) == USB_EP_ATTR_TYPE_BULK) {
                if (ep->b_endpoint_address & USB_EP_DIR_IN) {
                    g_xhci_msc.bulk_in_ep         = ep->b_endpoint_address;
                    g_xhci_msc.bulk_in_max_packet = ep->w_max_packet_size;
                } else {
                    g_xhci_msc.bulk_out_ep         = ep->b_endpoint_address;
                    g_xhci_msc.bulk_out_max_packet = ep->w_max_packet_size;
                }
                last_ep = ep->b_endpoint_address;
            }
        } else if (type == USB_DESC_TYPE_SS_ENDPOINT_COMP && in_msc_interface && last_ep != 0) {
            auto comp = (const struct usb_ss_ep_comp_descriptor *)buf;
            if (last_ep == g_xhci_msc.bulk_in_ep) {
                g_xhci_msc.bulk_in_max_burst = comp->b_max_burst;
            } else if (last_ep == g_xhci_msc.bulk_out_ep) {
                g_xhci_msc.bulk_out_max_burst = comp->b_max_burst;
            }
            last_ep = 0;
        }

        buf += len;
    }

    if (!saw_msc_interface) {
        return false;
    }

    if (g_xhci_msc.bulk_in_ep == 0 || g_xhci_msc.bulk_out_ep == 0) {
        boot_message(WARNING, "[xHCI] Slot %u no BOT bulk endpoints found", dev->slot_id);
        return false;
    }

    g_xhci_msc.bulk_in_id  = xhci_endpoint_id(g_xhci_msc.bulk_in_ep);
    g_xhci_msc.bulk_out_id = xhci_endpoint_id(g_xhci_msc.bulk_out_ep);

    boot_message(INFO,
                 "[xHCI] Slot %u BOT iface=%u in=0x%02x maxp=%u burst=%u out=0x%02x maxp=%u burst=%u",
                 dev->slot_id,
                 g_xhci_msc.interface_number,
                 g_xhci_msc.bulk_in_ep,
                 g_xhci_msc.bulk_in_max_packet,
                 g_xhci_msc.bulk_in_max_burst,
                 g_xhci_msc.bulk_out_ep,
                 g_xhci_msc.bulk_out_max_packet,
                 g_xhci_msc.bulk_out_max_burst);
    return true;
}

bool xhci_msc_configure_endpoints(struct xhci_controller *xhci)
{
    struct xhci_msc_device *msc = &g_xhci_msc;
    if (!xhci || !msc->dev || !msc->bulk_in_id || !msc->bulk_out_id) {
        return false;
    }

    if (!msc->bulk_in_ring.trbs) {
        if (!xhci_ring_init(&msc->bulk_in_ring, XHCI_EP0_RING_TRBS)) {
            return false;
        }
    }

    if (!msc->bulk_out_ring.trbs) {
        if (!xhci_ring_init(&msc->bulk_out_ring, XHCI_EP0_RING_TRBS)) {
            return false;
        }
    }

    const size_t ctx_size = xhci->context_size;
    auto ctrl             = (struct xhci_input_control_ctx *)msc->dev->input_ctx;
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->add_flags = (1u << 0) | (1u << msc->bulk_in_id) | (1u << msc->bulk_out_id);

    auto slot_in  = (struct xhci_slot_ctx *)xhci_input_context_ptr(msc->dev->input_ctx, 0, (uint32_t)ctx_size);
    auto slot_out = (struct xhci_slot_ctx *)xhci_device_context_ptr(msc->dev->device_ctx, 0, (uint32_t)ctx_size);
    *slot_in      = *slot_out;

    const uint8_t max_ep_id = msc->bulk_in_id > msc->bulk_out_id ? msc->bulk_in_id : msc->bulk_out_id;
    slot_in->dev_info       &= ~(0x1Fu << XHCI_SLOT_CTX_CTX_ENTRIES_SHIFT);
    slot_in->dev_info       |= ((uint32_t)max_ep_id << XHCI_SLOT_CTX_CTX_ENTRIES_SHIFT);

    auto ep0_in  = (struct xhci_ep_ctx *)xhci_input_context_ptr(msc->dev->input_ctx, 1, (uint32_t)ctx_size);
    auto ep0_out = (struct xhci_ep_ctx *)xhci_device_context_ptr(msc->dev->device_ctx, 1, (uint32_t)ctx_size);
    *ep0_in      = *ep0_out;

    auto bulk_out = (struct xhci_ep_ctx *)xhci_input_context_ptr(msc->dev->input_ctx,
                                                                 msc->bulk_out_id,
                                                                 (uint32_t)ctx_size);
    memset(bulk_out, 0, sizeof(*bulk_out));
    bulk_out->ep_info2 = (3u << XHCI_EP_ERROR_COUNT_SHIFT) |
        (XHCI_EP_TYPE_BULK_OUT << XHCI_EP_TYPE_SHIFT) |
        ((uint32_t)msc->bulk_out_max_burst << XHCI_EP_MAX_BURST_SHIFT) |
        ((uint32_t)msc->bulk_out_max_packet << XHCI_EP_MAX_PACKET_SHIFT);
    const uintptr_t out_deq = msc->bulk_out_ring.phys + (msc->bulk_out_ring.enqueue * sizeof(struct xhci_trb));
    bulk_out->deq           = out_deq | (msc->bulk_out_ring.cycle ? 1u : 0u);
    bulk_out->tx_info       = 0;

    auto bulk_in = (struct xhci_ep_ctx *)xhci_input_context_ptr(msc->dev->input_ctx,
                                                                msc->bulk_in_id,
                                                                (uint32_t)ctx_size);
    memset(bulk_in, 0, sizeof(*bulk_in));
    bulk_in->ep_info2 = (3u << XHCI_EP_ERROR_COUNT_SHIFT) |
        (XHCI_EP_TYPE_BULK_IN << XHCI_EP_TYPE_SHIFT) |
        ((uint32_t)msc->bulk_in_max_burst << XHCI_EP_MAX_BURST_SHIFT) |
        ((uint32_t)msc->bulk_in_max_packet << XHCI_EP_MAX_PACKET_SHIFT);
    const uintptr_t in_deq = msc->bulk_in_ring.phys + (msc->bulk_in_ring.enqueue * sizeof(struct xhci_trb));
    bulk_in->deq           = in_deq | (msc->bulk_in_ring.cycle ? 1u : 0u);
    bulk_in->tx_info       = 0;

    struct xhci_trb cmd = {0};
    cmd.dword0          = (uint32_t)msc->dev->input_ctx_phys;
    cmd.dword1          = (uint32_t)(msc->dev->input_ctx_phys >> 32);
    cmd.dword3          = (XHCI_TRB_TYPE_CONFIGURE_ENDPOINT << XHCI_TRB_TYPE_SHIFT) |
        ((uint32_t)msc->dev->slot_id << 24);
    const uintptr_t cmd_phys = xhci_ring_enqueue(&xhci->cmd_ring, &cmd);

    xhci_ring_doorbell(xhci, 0, 0);
    if (!xhci_wait_for_cmd_completion(xhci, cmd_phys, nullptr)) {
        boot_message(WARNING, "[xHCI] Slot %u configure endpoints failed", msc->dev->slot_id);
        return false;
    }

    return true;
}

static uintptr_t xhci_bulk_queue(struct xhci_ring *ring,
                                 const uintptr_t data_phys,
                                 const uint32_t data_len,
                                 const uint16_t max_packet)
{
    if (!ring || !ring->trbs || data_len == 0) {
        return 0;
    }

    uintptr_t phys               = data_phys;
    uint32_t remaining           = data_len;
    uintptr_t last_trb_phys      = 0;
    const uint32_t total_packets = xhci_packet_count(data_len, max_packet);
    uint32_t packets_done        = 0;

    while (remaining > 0) {
        uint32_t chunk = remaining > XHCI_TRB_LEN_MASK ? XHCI_TRB_LEN_MASK : remaining;
        if (remaining > chunk && max_packet != 0) {
            const uint32_t aligned = (chunk / max_packet) * max_packet;
            if (aligned != 0) {
                chunk = aligned;
            }
        }
        const uint32_t trb_packets = xhci_packet_count(chunk, max_packet);
        uint32_t remaining_packets = 0;
        if (total_packets > packets_done + trb_packets) {
            remaining_packets = total_packets - (packets_done + trb_packets);
        }
        const uint32_t td_size = remaining_packets > 31u ? 31u : remaining_packets;
        struct xhci_trb trb    = {0};
        trb.dword0             = (uint32_t)phys;
        trb.dword1             = (uint32_t)(phys >> 32);
        trb.dword2             = XHCI_TRB_LEN(chunk) | XHCI_TRB_TD_SIZE(td_size) | XHCI_TRB_INTR_TARGET(0);
        trb.dword3             = (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT);
        if (remaining > chunk) {
            trb.dword3 |= XHCI_TRB_CHAIN;
        } else {
            trb.dword3 |= XHCI_TRB_IOC;
        }

        last_trb_phys = xhci_ring_enqueue(ring, &trb);
        phys          += chunk;
        remaining     -= chunk;
        packets_done  += trb_packets;
    }

    return last_trb_phys;
}

static struct usb_msc_cbw *xhci_msc_prepare_cbw(struct xhci_msc_device *msc,
                                                const uint8_t *cb,
                                                const uint8_t cb_len,
                                                const bool data_in,
                                                const uint32_t data_len)
{
    auto cbw = (struct usb_msc_cbw *)msc->cbw_buf;
    memset(cbw, 0, sizeof(*cbw));
    cbw->signature            = USB_MSC_CBW_SIGNATURE;
    cbw->tag                  = ++msc->tag;
    cbw->data_transfer_length = data_len;
    cbw->flags                = data_in ? 0x80u : 0x00u;
    cbw->lun                  = 0;
    cbw->cb_length            = cb_len;
    memcpy(cbw->cb, cb, cb_len);
    return cbw;
}

static bool xhci_msc_bulk_wait(struct xhci_controller *xhci,
                               struct xhci_ring *ring,
                               const uintptr_t data_phys,
                               const uint32_t data_len,
                               const uint16_t max_packet,
                               const uint8_t slot_id,
                               const uint8_t ep_id)
{
    const uintptr_t trb = xhci_bulk_queue(ring, data_phys, data_len, max_packet);
    if (trb == 0) {
        return false;
    }

    xhci_ring_doorbell(xhci, slot_id, ep_id);
    return xhci_wait_for_transfer_event(xhci,
                                        trb,
                                        slot_id,
                                        ep_id,
                                        true);
}

static bool xhci_msc_transfer(struct xhci_controller *xhci,
                              struct xhci_msc_device *msc,
                              const uint8_t *cb,
                              const uint8_t cb_len,
                              const bool data_in,
                              const uint32_t data_len)
{
    if (!msc || !msc->dev || !msc->cbw_buf || !msc->csw_buf || (data_len > 0 && !msc->data_buf)) {
        return false;
    }

    auto cbw = xhci_msc_prepare_cbw(msc, cb, cb_len, data_in, data_len);

    if (!xhci_msc_bulk_wait(xhci,
                            &msc->bulk_out_ring,
                            msc->cbw_phys,
                            sizeof(*cbw),
                            msc->bulk_out_max_packet,
                            msc->dev->slot_id,
                            msc->bulk_out_id)) {
        boot_message(WARNING, "[xHCI] MSC CBW transfer failed");
        return false;
    }

    if (data_len > 0) {
        const uintptr_t data_phys = msc->data_phys;

        if (data_in) {
            if (!xhci_msc_bulk_wait(xhci,
                                    &msc->bulk_in_ring,
                                    data_phys,
                                    data_len,
                                    msc->bulk_in_max_packet,
                                    msc->dev->slot_id,
                                    msc->bulk_in_id)) {
                boot_message(WARNING, "[xHCI] MSC data IN failed");
                return false;
            }
        } else {
            if (!xhci_msc_bulk_wait(xhci,
                                    &msc->bulk_out_ring,
                                    data_phys,
                                    data_len,
                                    msc->bulk_out_max_packet,
                                    msc->dev->slot_id,
                                    msc->bulk_out_id)) {
                boot_message(WARNING, "[xHCI] MSC data OUT failed");
                return false;
            }
        }
    }

    if (!xhci_msc_bulk_wait(xhci,
                            &msc->bulk_in_ring,
                            msc->csw_phys,
                            sizeof(struct usb_msc_csw),
                            msc->bulk_in_max_packet,
                            msc->dev->slot_id,
                            msc->bulk_in_id)) {
        boot_message(WARNING, "[xHCI] MSC CSW transfer failed");
        return false;
    }

    auto csw = (const struct usb_msc_csw *)msc->csw_buf;
    if (csw->signature != USB_MSC_CSW_SIGNATURE || csw->tag != cbw->tag) {
        boot_message(WARNING, "[xHCI] MSC CSW invalid");
        return false;
    }

    if (csw->status != 0) {
        boot_message(WARNING, "[xHCI] MSC CSW status=%u", csw->status);
        return false;
    }

    return true;
}

static bool xhci_msc_inquiry(struct xhci_controller *xhci, struct xhci_msc_device *msc)
{
    uint8_t cb[6] = {0};
    cb[0]         = 0x12;
    cb[4]         = 36;

    if (!xhci_msc_transfer(xhci, msc, cb, sizeof(cb), true, 36)) {
        return false;
    }

    auto buf         = (const uint8_t *)msc->data_buf;
    char vendor[9]   = {0};
    char product[17] = {0};
    memcpy(vendor, buf + 8, 8);
    memcpy(product, buf + 16, 16);
    boot_message(INFO, "[xHCI] MSC INQUIRY %s %s", vendor, product);
    return true;
}

static bool xhci_msc_test_unit_ready(struct xhci_controller *xhci, struct xhci_msc_device *msc)
{
    uint8_t cb[6] = {0};
    cb[0]         = 0x00;
    return xhci_msc_transfer(xhci, msc, cb, sizeof(cb), false, 0);
}

static bool xhci_msc_read_capacity(struct xhci_controller *xhci, struct xhci_msc_device *msc)
{
    uint8_t cb[10] = {0};
    cb[0]          = 0x25;

    if (!xhci_msc_transfer(xhci, msc, cb, sizeof(cb), true, 8)) {
        return false;
    }

    auto buf                  = (const uint8_t *)msc->data_buf;
    const uint32_t last_lba   = xhci_be32(buf);
    const uint32_t block_size = xhci_be32(buf + 4);
    if (block_size == 0) {
        return false;
    }

    msc->block_size  = block_size;
    msc->block_count = (uint64_t)last_lba + 1u;
    boot_message(INFO,
                 "[xHCI] MSC capacity blocks=%llu block_size=%u",
                 (unsigned long long)msc->block_count,
                 msc->block_size);
    return true;
}

static bool xhci_msc_read10(struct xhci_controller *xhci,
                            struct xhci_msc_device *msc,
                            const uint32_t lba,
                            const uint16_t blocks)
{
    uint8_t cb[10] = {0};
    cb[0]          = 0x28;
    xhci_put_be32(&cb[2], lba);
    xhci_put_be16(&cb[7], blocks);

    const uint32_t data_len = (uint32_t)blocks * msc->block_size;
    return xhci_msc_transfer(xhci, msc, cb, sizeof(cb), true, data_len);
}

static bool xhci_msc_write10(struct xhci_controller *xhci,
                             struct xhci_msc_device *msc,
                             const uint32_t lba,
                             const uint16_t blocks)
{
    uint8_t cb[10] = {0};
    cb[0]          = 0x2A;
    xhci_put_be32(&cb[2], lba);
    xhci_put_be16(&cb[7], blocks);

    const uint32_t data_len = (uint32_t)blocks * msc->block_size;
    return xhci_msc_transfer(xhci, msc, cb, sizeof(cb), false, data_len);
}

static bool xhci_msc_prepare_buffers(struct xhci_msc_device *msc)
{
    if (!msc->cbw_buf) {
        if (!xhci_alloc_pages(sizeof(struct usb_msc_cbw), &msc->cbw_phys, &msc->cbw_buf)) {
            return false;
        }
    }

    if (!msc->csw_buf) {
        if (!xhci_alloc_pages(sizeof(struct usb_msc_csw), &msc->csw_phys, &msc->csw_buf)) {
            return false;
        }
    }

    if (!msc->data_buf) {
        if (!xhci_alloc_pages(XHCI_MSC_DATA_BYTES, &msc->data_phys, &msc->data_buf)) {
            return false;
        }
        msc->data_bytes = XHCI_MSC_DATA_BYTES;
    }

    return true;
}

bool xhci_msc_init(struct xhci_controller *xhci)
{
    struct xhci_msc_device *msc = &g_xhci_msc;
    if (!xhci || !msc->dev || !msc->bulk_in_id || !msc->bulk_out_id) {
        return false;
    }

    if (!msc->bulk_in_ring.trbs || !msc->bulk_out_ring.trbs) {
        return false;
    }

    if (!xhci_msc_prepare_buffers(msc)) {
        return false;
    }

    if (!xhci_msc_inquiry(xhci, msc)) {
        return false;
    }

    bool ready = false;
    for (uint32_t i = 0; i < 5; i++) {
        if (xhci_msc_test_unit_ready(xhci, msc)) {
            ready = true;
            break;
        }
        tsc_sleep_ms(100);
    }

    if (!ready) {
        boot_message(WARNING, "[xHCI] MSC TEST UNIT READY failed");
        return false;
    }

    if (!xhci_msc_read_capacity(xhci, msc)) {
        boot_message(WARNING, "[xHCI] MSC READ CAPACITY failed");
        return false;
    }

    if (msc->block_size != 0 && msc->block_size <= msc->data_bytes) {
        if (!xhci_msc_read10(xhci, msc, 0, 1)) {
            boot_message(WARNING, "[xHCI] MSC READ(10) probe failed");
        }
    }

    const bool allow_write = false;
    if (allow_write) {
        (void)xhci_msc_write10(xhci, msc, 0, 1);
    }

    return true;
}
