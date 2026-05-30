#include <drivers/usb/xhci.h>
#include <drivers/usb/xhci_internal.h>
#include <drivers/mouse.h>
#include <drivers/framebuffer.h>
#include <drivers/terminal.h>
#include <task/process.h>
#include <lib/string.h>
#include <mem/dma.h>

static struct xhci_hid_mouse_device g_xhci_hid_mouse;

/** @brief Return the USB configuration value for the detected HID mouse device. */
uint8_t xhci_hid_mouse_config_value(void)
{
    return g_xhci_hid_mouse.config_value;
}

/** @brief Check whether a USB HID mouse or tablet device has been initialized. */
bool xhci_usb_mouse_present(void)
{
    return g_xhci_hid_mouse.active;
}

/** @brief Parse a configuration descriptor for a HID mouse or tablet interface. */
bool xhci_hid_mouse_parse_config(const struct xhci_device *dev,
                                 const void *cfg_buf,
                                 const uint16_t total_len)
{
    if (!dev || !cfg_buf || total_len < sizeof(struct usb_config_descriptor))
        return false;

    g_xhci_hid_mouse.dev               = (struct xhci_device *)dev;
    g_xhci_hid_mouse.active            = false;
    g_xhci_hid_mouse.is_tablet         = false;
    g_xhci_hid_mouse.config_value      = ((const struct usb_config_descriptor *)cfg_buf)->b_configuration_value;
    g_xhci_hid_mouse.interface_number  = 0;
    g_xhci_hid_mouse.int_in_ep         = 0;
    g_xhci_hid_mouse.int_in_id         = 0;
    g_xhci_hid_mouse.int_in_max_packet = 0;
    g_xhci_hid_mouse.int_in_max_burst  = 0;
    g_xhci_hid_mouse.interval          = 0;
    g_xhci_hid_mouse.report_buf        = nullptr;
    g_xhci_hid_mouse.report_phys       = 0;

    auto buf              = (const uint8_t *)cfg_buf;
    const uint8_t *end    = buf + total_len;
    bool in_hid_interface = false;
    bool saw_hid          = false;
    bool is_boot_mouse    = false;
    uint8_t last_ep       = 0;

    while (buf + 2 <= end) {
        const uint8_t len  = buf[0];
        const uint8_t type = buf[1];
        if (len < 2 || buf + len > end)
            break;

        if (type == USB_DESC_TYPE_INTERFACE) {
            auto iface = (const struct usb_interface_descriptor *)buf;
            if (iface->b_interface_class == USB_CLASS_HID && iface->b_alternate_setting == 0) {
                is_boot_mouse = iface->b_interface_subclass == USB_SUBCLASS_BOOT &&
                    iface->b_interface_protocol == USB_PROTOCOL_MOUSE;
                in_hid_interface = true;
                saw_hid          = true;
                g_xhci_hid_mouse.interface_number = iface->b_interface_number;
            } else {
                in_hid_interface = false;
            }
        } else if (type == USB_DESC_TYPE_ENDPOINT && in_hid_interface) {
            auto ep = (const struct usb_endpoint_descriptor *)buf;
            if ((ep->bm_attributes & USB_EP_ATTR_TYPE_MASK) == USB_EP_ATTR_TYPE_INTERRUPT &&
                (ep->b_endpoint_address & USB_EP_DIR_IN)) {
                g_xhci_hid_mouse.int_in_ep         = ep->b_endpoint_address;
                g_xhci_hid_mouse.int_in_max_packet  = ep->w_max_packet_size;
                g_xhci_hid_mouse.interval           = ep->b_interval;
                last_ep = ep->b_endpoint_address;
            }
        } else if (type == USB_DESC_TYPE_SS_ENDPOINT_COMP && in_hid_interface && last_ep != 0) {
            auto comp = (const struct usb_ss_ep_comp_descriptor *)buf;
            if (last_ep == g_xhci_hid_mouse.int_in_ep)
                g_xhci_hid_mouse.int_in_max_burst = comp->b_max_burst;
            last_ep = 0;
        }

        buf += len;
    }

    if (!saw_hid || g_xhci_hid_mouse.int_in_ep == 0)
        return false;

    g_xhci_hid_mouse.is_tablet = !is_boot_mouse;
    g_xhci_hid_mouse.int_in_id = xhci_endpoint_id(g_xhci_hid_mouse.int_in_ep);

    boot_message(INFO,
                 "[xHCI] Slot %u HID %s iface=%u ep=0x%02x maxp=%u interval=%u",
                 dev->slot_id,
                 g_xhci_hid_mouse.is_tablet ? "tablet" : "mouse",
                 g_xhci_hid_mouse.interface_number,
                 g_xhci_hid_mouse.int_in_ep,
                 g_xhci_hid_mouse.int_in_max_packet,
                 g_xhci_hid_mouse.interval);
    return true;
}

/** @brief Configure the interrupt IN endpoint for the HID mouse device. */
bool xhci_hid_mouse_configure_endpoints(struct xhci_controller *xhci)
{
    struct xhci_hid_mouse_device *hid = &g_xhci_hid_mouse;
    if (!xhci || !hid->dev || !hid->int_in_id)
        return false;

    if (!hid->int_in_ring.trbs) {
        if (!xhci_ring_init(&hid->int_in_ring, XHCI_EP0_RING_TRBS))
            return false;
    }

    const size_t ctx_size = xhci->context_size;
    auto ctrl             = (struct xhci_input_control_ctx *)hid->dev->input_ctx;
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->add_flags = (1u << 0) | (1u << hid->int_in_id);

    auto slot_in  = (struct xhci_slot_ctx *)xhci_input_context_ptr(hid->dev->input_ctx, 0, (uint32_t)ctx_size);
    auto slot_out = (struct xhci_slot_ctx *)xhci_device_context_ptr(hid->dev->device_ctx, 0, (uint32_t)ctx_size);
    *slot_in      = *slot_out;
    slot_in->dev_info_bits.ctx_entries = hid->int_in_id;

    auto ep0_in  = (struct xhci_ep_ctx *)xhci_input_context_ptr(hid->dev->input_ctx, 1, (uint32_t)ctx_size);
    auto ep0_out = (struct xhci_ep_ctx *)xhci_device_context_ptr(hid->dev->device_ctx, 1, (uint32_t)ctx_size);
    *ep0_in      = *ep0_out;

    auto int_in = (struct xhci_ep_ctx *)xhci_input_context_ptr(hid->dev->input_ctx,
                                                                hid->int_in_id,
                                                                (uint32_t)ctx_size);
    memset(int_in, 0, sizeof(*int_in));
    int_in->ep_info2_bits.error_count = 3u;
    int_in->ep_info2_bits.ep_type     = XHCI_EP_TYPE_INTERRUPT_IN;
    int_in->ep_info2_bits.max_burst   = hid->int_in_max_burst;
    int_in->ep_info2_bits.max_packet  = hid->int_in_max_packet;

    uint8_t xhci_interval = 0;
    if (hid->dev->speed >= 3u) {
        xhci_interval = hid->interval;
    } else {
        uint32_t microframes = (uint32_t)hid->interval * 8u;
        xhci_interval = 1;
        while ((1u << xhci_interval) < microframes && xhci_interval < 15u)
            xhci_interval++;
    }
    int_in->ep_info = ((uint32_t)xhci_interval << 16);

    const uintptr_t deq = hid->int_in_ring.phys + (hid->int_in_ring.enqueue * sizeof(struct xhci_trb));
    int_in->deq         = deq | (hid->int_in_ring.cycle ? 1u : 0u);
    int_in->tx_info     = 0;

    struct xhci_trb cmd  = {0};
    cmd.dword0           = (uint32_t)hid->dev->input_ctx_phys;
    cmd.dword1           = (uint32_t)(hid->dev->input_ctx_phys >> 32);
    cmd.control.trb_type = XHCI_TRB_TYPE_CONFIGURE_ENDPOINT;
    cmd.event.slot_id    = hid->dev->slot_id;
    const uintptr_t cmd_phys = xhci_ring_enqueue(&xhci->cmd_ring, &cmd);

    xhci_ring_doorbell(xhci, 0, 0);
    if (!xhci_wait_for_cmd_completion(xhci, cmd_phys, nullptr)) {
        boot_message(WARNING, "[xHCI] Slot %u HID configure endpoints failed", hid->dev->slot_id);
        return false;
    }

    return true;
}

/**
 * @brief Send a HID SET_IDLE class request to suppress repeated idle reports.
 *
 * @param xhci xHCI controller state.
 * @param hid  HID mouse device state.
 * @return true if the control transfer succeeded.
 */
static bool xhci_hid_set_idle(struct xhci_controller *xhci, const struct xhci_hid_mouse_device *hid)
{
    struct usb_setup_packet setup = {
        .bm_request_type = 0x21,
        .b_request       = USB_REQ_SET_IDLE,
        .w_value         = 0,
        .w_index         = hid->interface_number,
        .w_length        = 0,
    };
    return xhci_control_transfer(xhci, hid->dev, &setup, 0, 0, false);
}

/**
 * @brief Decode a USB tablet HID report and inject an absolute mouse event.
 *
 * Expects a 5-byte boot-style tablet report: buttons, abs_x (LE16), abs_y (LE16).
 * Coordinates are scaled from the tablet range to the current framebuffer dimensions.
 *
 * @param report Raw HID report bytes.
 */
static void xhci_hid_process_tablet_report(const uint8_t *report)
{
    const uint8_t buttons  = report[0];
    const uint16_t abs_x   = (uint16_t)report[1] | ((uint16_t)report[2] << 8);
    const uint16_t abs_y   = (uint16_t)report[3] | ((uint16_t)report[4] << 8);

    struct limine_framebuffer *fb = framebuffer_current();
    if (!fb)
        return;

    auto x = (int16_t)((uint32_t)abs_x * fb->width / (USB_HID_TABLET_ABS_MAX + 1u));
    auto y = (int16_t)((uint32_t)abs_y * fb->height / (USB_HID_TABLET_ABS_MAX + 1u));

    mouse_set_absolute(x, y, buttons);
}

/**
 * @brief Decode a USB boot mouse HID report and inject a relative mouse event.
 *
 * Expects a 3-byte boot protocol report: buttons, dx (int8), dy (int8).
 *
 * @param report Raw HID report bytes.
 */
static void xhci_hid_process_mouse_report(const uint8_t *report)
{
    const uint8_t buttons = report[0];
    auto dx         = (int16_t)(int8_t)report[1];
    auto dy         = (int16_t)(int8_t)report[2];

    if (dx != 0 || dy != 0 || buttons != 0)
        mouse_inject_event(dx, dy, buttons);
}

/**
 * @brief Kernel thread that continuously polls the HID mouse interrupt IN endpoint.
 *
 * Enqueues a Normal TRB on each iteration, waits for the transfer event, then
 * dispatches the report to the appropriate tablet or mouse handler. Sleeps for
 * XHCI_HID_POLL_INTERVAL_MS between polls.
 */
static void xhci_hid_mouse_poll_thread(void)
{
    struct xhci_hid_mouse_device *hid = &g_xhci_hid_mouse;
    struct xhci_controller *xhci      = &g_xhci;

    boot_message(INFO, "[xHCI] HID %s poll thread started",
                 hid->is_tablet ? "tablet" : "mouse");

    while (hid->active) {
        struct xhci_trb trb  = {0};
        trb.dword0           = (uint32_t)hid->report_phys;
        trb.dword1           = (uint32_t)(hid->report_phys >> 32);
        trb.dword2           = XHCI_TRB_LEN(hid->int_in_max_packet) | XHCI_TRB_INTR_TARGET(0);
        trb.control.trb_type = XHCI_TRB_TYPE_NORMAL;
        trb.control.ioc      = 1;
        trb.control.isp      = 1;

        // Hold io_lock across enqueue + doorbell + drain so the poll does not
        // consume MSC transfer-completion events from the shared event ring.
        sleeplock_acquire(&xhci->io_lock);
        const uintptr_t trb_phys = xhci_ring_enqueue(&hid->int_in_ring, &trb);
        xhci_ring_doorbell(xhci, hid->dev->slot_id, hid->int_in_id);
        const bool got = xhci_wait_for_transfer_event(xhci, trb_phys, hid->dev->slot_id, hid->int_in_id, true, false);
        sleeplock_release(&xhci->io_lock);

        if (!got) {
            tsc_sleep_ms(XHCI_HID_POLL_INTERVAL_MS);
            continue;
        }

        auto report = (const uint8_t *)hid->report_buf;
        if (hid->is_tablet)
            xhci_hid_process_tablet_report(report);
        else
            xhci_hid_process_mouse_report(report);
    }
}

/** @brief Initialize the HID mouse device and start its polling thread. */
bool xhci_hid_mouse_init(struct xhci_controller *xhci)
{
    struct xhci_hid_mouse_device *hid = &g_xhci_hid_mouse;
    if (!xhci || !hid->dev || !hid->int_in_id)
        return false;

    if (!hid->int_in_ring.trbs)
        return false;

    if (!hid->report_buf) {
        if (!dma_alloc_pages(hid->int_in_max_packet, PAGE_SIZE, 0, &hid->report_phys, &hid->report_buf))
            return false;
    }
    memset(hid->report_buf, 0, hid->int_in_max_packet);

    // Boot mice need SET_PROTOCOL; tablets don't support boot protocol.
    if (!hid->is_tablet) {
        struct usb_setup_packet setup = {
            .bm_request_type = 0x21,
            .b_request       = USB_REQ_SET_PROTOCOL,
            .w_value         = USB_HID_PROTOCOL_BOOT,
            .w_index         = hid->interface_number,
            .w_length        = 0,
        };
        if (!xhci_control_transfer(xhci, hid->dev, &setup, 0, 0, false))
            boot_message(WARNING, "[xHCI] Slot %u SET_PROTOCOL(boot) failed", hid->dev->slot_id);
    }

    if (!xhci_hid_set_idle(xhci, hid))
        boot_message(WARNING, "[xHCI] Slot %u SET_IDLE failed", hid->dev->slot_id);

    hid->active = true;

    thread_t *t = thread_create(kernel_process, xhci_hid_mouse_poll_thread, false);
    if (!t) {
        boot_message(WARNING, "[xHCI] HID poll thread creation failed");
        hid->active = false;
        return false;
    }

    boot_message(INFO, "[xHCI] Slot %u HID %s initialized",
                 hid->dev->slot_id,
                 hid->is_tablet ? "tablet" : "mouse");
    return true;
}
