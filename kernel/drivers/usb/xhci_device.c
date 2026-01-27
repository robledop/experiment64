#include <drivers/usb/xhci_internal.h>
#include <drivers/terminal.h>

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