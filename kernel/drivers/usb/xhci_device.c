#include <drivers/pci.h>
#include <drivers/usb/xhci_internal.h>
#include <drivers/terminal.h>
#include <lib/string.h>

static bool xhci_cmd_submit(struct xhci_controller *xhci, const struct xhci_trb *trb, uint8_t *slot_id_out)
{
    if (!trb) {
        return false;
    }

    const uintptr_t phys = xhci_ring_enqueue(&xhci->cmd_ring, trb);
    xhci_ring_doorbell(xhci, 0, 0);
    return xhci_wait_for_cmd_completion(xhci, phys, slot_id_out);
}

bool xhci_enable_slot(struct xhci_controller *xhci, uint8_t *slot_id_out)
{
    struct xhci_trb cmd_trb = {0};
    cmd_trb.dword3          = (XHCI_TRB_TYPE_ENABLE_SLOT << XHCI_TRB_TYPE_SHIFT);
    return xhci_cmd_submit(xhci, &cmd_trb, slot_id_out);
}

bool xhci_alloc_device_context(struct xhci_controller *xhci, struct xhci_device *dev)
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

bool xhci_prepare_slot_context(struct xhci_controller *xhci, struct xhci_device *dev)
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

bool xhci_address_device(struct xhci_controller *xhci, struct xhci_device *dev)
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

bool xhci_control_transfer(struct xhci_controller *xhci,
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

bool xhci_set_configuration(struct xhci_controller *xhci,
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

bool xhci_get_device_descriptor(struct xhci_controller *xhci, struct xhci_device *dev)
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

bool xhci_get_config_descriptor(struct xhci_controller *xhci, struct xhci_device *dev)
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
