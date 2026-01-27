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