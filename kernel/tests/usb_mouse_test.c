#include <tests/test.h>
#include <drivers/mouse.h>
#include <drivers/usb/xhci_internal.h>

TEST(test_mouse_inject_event_updates_position)
{
    mouse_set_position(100, 100);
    mouse_flush_pending_events();

    mouse_inject_event(10, 20, 0);

    mouse_t m = {};
    mouse_get_position(&m);
    TEST_ASSERT(m.x == 110);
    TEST_ASSERT(m.y == 120);
    return true;
}

TEST(test_mouse_inject_event_negative_delta)
{
    mouse_set_position(100, 100);
    mouse_flush_pending_events();

    mouse_inject_event(-50, -30, 0);

    mouse_t m = {};
    mouse_get_position(&m);
    TEST_ASSERT(m.x == 50);
    TEST_ASSERT(m.y == 70);
    return true;
}

TEST(test_mouse_inject_event_clamps_to_zero)
{
    mouse_set_position(5, 5);
    mouse_flush_pending_events();

    mouse_inject_event(-100, -100, 0);

    mouse_t m = {};
    mouse_get_position(&m);
    TEST_ASSERT(m.x == 0);
    TEST_ASSERT(m.y == 0);
    return true;
}

TEST(test_mouse_inject_event_buttons)
{
    mouse_set_position(50, 50);
    mouse_flush_pending_events();

    mouse_inject_event(0, 0, MOUSE_LEFT | MOUSE_RIGHT);

    mouse_t m = {};
    mouse_get_position(&m);
    TEST_ASSERT((m.flags & MOUSE_LEFT) != 0);
    TEST_ASSERT((m.flags & MOUSE_RIGHT) != 0);
    TEST_ASSERT((m.flags & MOUSE_MIDDLE) == 0);
    return true;
}

TEST(test_mouse_set_absolute_position)
{
    mouse_flush_pending_events();

    mouse_set_absolute(200, 300, 0);

    mouse_t m = {};
    mouse_get_position(&m);
    TEST_ASSERT(m.x == 200);
    TEST_ASSERT(m.y == 300);
    return true;
}

TEST(test_mouse_set_absolute_clamps_negative)
{
    mouse_flush_pending_events();

    mouse_set_absolute(-10, -20, 0);

    mouse_t m = {};
    mouse_get_position(&m);
    TEST_ASSERT(m.x == 0);
    TEST_ASSERT(m.y == 0);
    return true;
}

TEST(test_mouse_set_absolute_buttons)
{
    mouse_flush_pending_events();

    mouse_set_absolute(50, 50, MOUSE_MIDDLE);

    mouse_t m = {};
    mouse_get_position(&m);
    TEST_ASSERT((m.flags & MOUSE_MIDDLE) != 0);
    TEST_ASSERT((m.flags & MOUSE_LEFT) == 0);
    TEST_ASSERT((m.flags & MOUSE_RIGHT) == 0);
    return true;
}

TEST(test_mouse_inject_masks_extra_bits)
{
    mouse_set_position(50, 50);
    mouse_flush_pending_events();

    mouse_inject_event(0, 0, 0xFF);

    mouse_t m = {};
    mouse_get_position(&m);
    TEST_ASSERT(m.flags == (MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE));
    return true;
}

TEST(test_mouse_set_absolute_masks_extra_bits)
{
    mouse_flush_pending_events();

    mouse_set_absolute(50, 50, 0xFF);

    mouse_t m = {};
    mouse_get_position(&m);
    TEST_ASSERT(m.flags == (MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE));
    return true;
}

TEST(test_hid_mouse_parse_config_rejects_null)
{
    TEST_ASSERT(!xhci_hid_mouse_parse_config(nullptr, nullptr, 0));
    return true;
}

TEST(test_hid_mouse_parse_config_rejects_short_buffer)
{
    struct xhci_device dev = {};
    uint8_t buf[4] = {};
    TEST_ASSERT(!xhci_hid_mouse_parse_config(&dev, buf, sizeof(buf)));
    return true;
}

TEST(test_hid_mouse_parse_config_rejects_no_hid_interface)
{
    struct xhci_device dev = {.slot_id = 1};

    uint8_t buf[9 + 9] = {};
    auto cfg       = (struct usb_config_descriptor *)buf;
    cfg->b_length  = 9;
    cfg->b_descriptor_type     = USB_DESC_TYPE_CONFIGURATION;
    cfg->w_total_length        = sizeof(buf);
    cfg->b_num_interfaces      = 1;
    cfg->b_configuration_value = 1;

    auto iface       = (struct usb_interface_descriptor *)(buf + 9);
    iface->b_length  = 9;
    iface->b_descriptor_type    = USB_DESC_TYPE_INTERFACE;
    iface->b_interface_class    = USB_CLASS_MASS_STORAGE;
    iface->b_interface_subclass = USB_SUBCLASS_SCSI;
    iface->b_interface_protocol = USB_PROTOCOL_BOT;

    TEST_ASSERT(!xhci_hid_mouse_parse_config(&dev, buf, sizeof(buf)));
    return true;
}

TEST(test_hid_mouse_parse_config_accepts_boot_mouse)
{
    struct xhci_device dev = {.slot_id = 1};

    uint8_t buf[9 + 9 + 7] = {};
    auto cfg       = (struct usb_config_descriptor *)buf;
    cfg->b_length  = 9;
    cfg->b_descriptor_type     = USB_DESC_TYPE_CONFIGURATION;
    cfg->w_total_length        = sizeof(buf);
    cfg->b_num_interfaces      = 1;
    cfg->b_configuration_value = 1;

    auto iface       = (struct usb_interface_descriptor *)(buf + 9);
    iface->b_length  = 9;
    iface->b_descriptor_type    = USB_DESC_TYPE_INTERFACE;
    iface->b_interface_class    = USB_CLASS_HID;
    iface->b_interface_subclass = USB_SUBCLASS_BOOT;
    iface->b_interface_protocol = USB_PROTOCOL_MOUSE;
    iface->b_num_endpoints      = 1;

    auto ep       = (struct usb_endpoint_descriptor *)(buf + 18);
    ep->b_length  = 7;
    ep->b_descriptor_type  = USB_DESC_TYPE_ENDPOINT;
    ep->b_endpoint_address = 0x81;
    ep->bm_attributes      = USB_EP_ATTR_TYPE_INTERRUPT;
    ep->w_max_packet_size  = 8;
    ep->b_interval         = 10;

    TEST_ASSERT(xhci_hid_mouse_parse_config(&dev, buf, sizeof(buf)));
    TEST_ASSERT(xhci_hid_mouse_config_value() == 1);
    return true;
}

TEST(test_hid_mouse_parse_config_accepts_tablet)
{
    struct xhci_device dev = {.slot_id = 1};

    uint8_t buf[9 + 9 + 7] = {};
    auto cfg       = (struct usb_config_descriptor *)buf;
    cfg->b_length  = 9;
    cfg->b_descriptor_type     = USB_DESC_TYPE_CONFIGURATION;
    cfg->w_total_length        = sizeof(buf);
    cfg->b_num_interfaces      = 1;
    cfg->b_configuration_value = 2;

    auto iface       = (struct usb_interface_descriptor *)(buf + 9);
    iface->b_length  = 9;
    iface->b_descriptor_type    = USB_DESC_TYPE_INTERFACE;
    iface->b_interface_class    = USB_CLASS_HID;
    iface->b_interface_subclass = 0;
    iface->b_interface_protocol = 0;
    iface->b_num_endpoints      = 1;

    auto ep       = (struct usb_endpoint_descriptor *)(buf + 18);
    ep->b_length  = 7;
    ep->b_descriptor_type  = USB_DESC_TYPE_ENDPOINT;
    ep->b_endpoint_address = 0x81;
    ep->bm_attributes      = USB_EP_ATTR_TYPE_INTERRUPT;
    ep->w_max_packet_size  = 8;
    ep->b_interval         = 10;

    TEST_ASSERT(xhci_hid_mouse_parse_config(&dev, buf, sizeof(buf)));
    TEST_ASSERT(xhci_hid_mouse_config_value() == 2);
    return true;
}

TEST(test_hid_mouse_parse_config_rejects_no_interrupt_ep)
{
    struct xhci_device dev = {.slot_id = 1};

    uint8_t buf[9 + 9 + 7] = {};
    auto cfg       = (struct usb_config_descriptor *)buf;
    cfg->b_length  = 9;
    cfg->b_descriptor_type     = USB_DESC_TYPE_CONFIGURATION;
    cfg->w_total_length        = sizeof(buf);
    cfg->b_num_interfaces      = 1;
    cfg->b_configuration_value = 1;

    auto iface       = (struct usb_interface_descriptor *)(buf + 9);
    iface->b_length  = 9;
    iface->b_descriptor_type    = USB_DESC_TYPE_INTERFACE;
    iface->b_interface_class    = USB_CLASS_HID;
    iface->b_interface_subclass = USB_SUBCLASS_BOOT;
    iface->b_interface_protocol = USB_PROTOCOL_MOUSE;
    iface->b_num_endpoints      = 1;

    auto ep       = (struct usb_endpoint_descriptor *)(buf + 18);
    ep->b_length  = 7;
    ep->b_descriptor_type  = USB_DESC_TYPE_ENDPOINT;
    ep->b_endpoint_address = 0x81;
    ep->bm_attributes      = USB_EP_ATTR_TYPE_BULK;
    ep->w_max_packet_size  = 64;

    TEST_ASSERT(!xhci_hid_mouse_parse_config(&dev, buf, sizeof(buf)));
    return true;
}
