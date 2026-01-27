#pragma once

#include <stddef.h>
#include <stdint.h>

#define XHCI_TRB_TYPE_SHIFT 10u // TRB type field shift.
#define XHCI_TRB_TYPE_CONFIGURE_ENDPOINT 12u // Configure Endpoint command TRB type.
#define XHCI_EP_TYPE_CONTROL 4u // Endpoint type for control transfers.
#define XHCI_EP_TYPE_BULK_OUT 2u // Endpoint type for bulk OUT transfers.
#define XHCI_EP_TYPE_BULK_IN 6u // Endpoint type for bulk IN transfers.
#define XHCI_EP_ERROR_COUNT_SHIFT 1u // Endpoint error count field shift.
#define XHCI_EP_TYPE_SHIFT 3u // Endpoint type field shift.
#define XHCI_EP_MAX_BURST_SHIFT 8u // Endpoint max burst field shift.
#define XHCI_EP_MAX_PACKET_SHIFT 16u // Endpoint max packet size field shift.
#define XHCI_SLOT_CTX_CTX_ENTRIES_SHIFT 27u // Slot context entry count field shift.
#define XHCI_EP0_RING_TRBS 256u // Endpoint 0 ring TRB count.

#define USB_DESC_TYPE_INTERFACE 0x04u // USB interface descriptor type.
#define USB_DESC_TYPE_ENDPOINT 0x05u // USB endpoint descriptor type.
#define USB_DESC_TYPE_SS_ENDPOINT_COMP 0x30u // USB SuperSpeed endpoint companion descriptor type.
#define USB_CLASS_MASS_STORAGE 0x08u // USB mass storage device class code.
#define USB_SUBCLASS_SCSI 0x06u // USB SCSI transparent subclass code.
#define USB_PROTOCOL_BOT 0x50u // USB bulk-only transport protocol code.
#define USB_EP_DIR_IN 0x80u // Endpoint address IN direction bit.
#define USB_EP_ADDR_MASK 0x0Fu // Endpoint number mask.
#define USB_EP_ATTR_TYPE_MASK 0x03u // Endpoint attribute transfer type mask.
#define USB_EP_ATTR_TYPE_BULK 0x02u // Endpoint attribute bulk transfer type.

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

struct xhci_input_control_ctx
{
    uint32_t drop_flags;  // Contexts to drop in this update.
    uint32_t add_flags;   // Contexts to add in this update.
    uint32_t reserved[6]; // Reserved, must be zero.
} __attribute__((packed));

struct xhci_slot_ctx
{
    uint32_t dev_info;    // Slot state, speed, and context count.
    uint32_t dev_info2;   // Root port and hub information.
    uint32_t tt_info;     // Transaction translator info.
    uint32_t dev_state;   // Slot state and device address.
    uint32_t reserved[4]; // Reserved, must be zero.
} __attribute__((packed));

struct xhci_ep_ctx
{
    uint32_t ep_info;     // Endpoint state and properties.
    uint32_t ep_info2;    // Endpoint type and max packet size.
    uint64_t deq;         // Endpoint dequeue pointer.
    uint32_t tx_info;     // Endpoint transfer settings.
    uint32_t reserved[3]; // Reserved, must be zero.
} __attribute__((packed));

struct xhci_device
{
    bool active;               // Device slot is in use.
    uint8_t slot_id;           // Slot ID assigned by controller.
    uint8_t port_id;           // Root hub port number.
    uint32_t speed;            // Port speed code.
    struct xhci_ring ep0_ring; // Control endpoint ring state.
    void *input_ctx;           // Input context virtual base.
    uintptr_t input_ctx_phys;  // Input context physical base.
    void *device_ctx;          // Device context virtual base.
    uintptr_t device_ctx_phys; // Device context physical base.
};

struct usb_setup_packet
{
    uint8_t bm_request_type; // Request type and direction.
    uint8_t b_request;       // Request code.
    uint16_t w_value;        // Request value parameter.
    uint16_t w_index;        // Request index parameter.
    uint16_t w_length;       // Request data length.
} __attribute__((packed));

struct usb_device_descriptor
{
    uint8_t b_length;             // Descriptor size in bytes.
    uint8_t b_descriptor_type;    // Descriptor type code.
    uint16_t bcd_usb;             // USB specification version.
    uint8_t b_device_class;       // Device class code.
    uint8_t b_device_subclass;    // Device subclass code.
    uint8_t b_device_protocol;    // Device protocol code.
    uint8_t b_max_packet_size0;   // EP0 max packet size.
    uint16_t id_vendor;           // Vendor ID.
    uint16_t id_product;          // Product ID.
    uint16_t bcd_device;          // Device release number.
    uint8_t i_manufacturer;       // Manufacturer string index.
    uint8_t i_product;            // Product string index.
    uint8_t i_serial_number;      // Serial number string index.
    uint8_t b_num_configurations; // Number of configurations.
} __attribute__((packed));

struct usb_config_descriptor
{
    uint8_t b_length;              // Descriptor size in bytes.
    uint8_t b_descriptor_type;     // Descriptor type code.
    uint16_t w_total_length;       // Total length of configuration data.
    uint8_t b_num_interfaces;      // Number of interfaces.
    uint8_t b_configuration_value; // Configuration value.
    uint8_t i_configuration;       // Configuration string index.
    uint8_t bm_attributes;         // Configuration attributes.
    uint8_t b_max_power;           // Max power consumption.
} __attribute__((packed));

struct usb_interface_descriptor
{
    uint8_t b_length;             // Descriptor size in bytes.
    uint8_t b_descriptor_type;    // Descriptor type code.
    uint8_t b_interface_number;   // Interface number.
    uint8_t b_alternate_setting;  // Alternate setting index.
    uint8_t b_num_endpoints;      // Number of endpoints.
    uint8_t b_interface_class;    // Interface class code.
    uint8_t b_interface_subclass; // Interface subclass code.
    uint8_t b_interface_protocol; // Interface protocol code.
    uint8_t i_interface;          // Interface string index.
} __attribute__((packed));

struct usb_endpoint_descriptor
{
    uint8_t b_length;           // Descriptor size in bytes.
    uint8_t b_descriptor_type;  // Descriptor type code.
    uint8_t b_endpoint_address; // Endpoint address and direction.
    uint8_t bm_attributes;      // Endpoint attributes bitmap.
    uint16_t w_max_packet_size; // Max packet size.
    uint8_t b_interval;         // Polling interval.
} __attribute__((packed));

struct usb_ss_ep_comp_descriptor
{
    uint8_t b_length;              // Descriptor size in bytes.
    uint8_t b_descriptor_type;     // Descriptor type code.
    uint8_t b_max_burst;           // Max packets per burst minus one.
    uint8_t bm_attributes;         // Companion attributes bitmap.
    uint16_t w_bytes_per_interval; // Bytes per service interval.
} __attribute__((packed));

struct xhci_msc_device
{
    struct xhci_device *dev;         // Associated xHCI device.
    uint8_t config_value;            // Configuration value to select.
    uint8_t interface_number;        // MSC interface number.
    uint8_t bulk_in_ep;              // Bulk IN endpoint address.
    uint8_t bulk_out_ep;             // Bulk OUT endpoint address.
    uint8_t bulk_in_id;              // xHCI endpoint ID for bulk IN.
    uint8_t bulk_out_id;             // xHCI endpoint ID for bulk OUT.
    uint16_t bulk_in_max_packet;     // Bulk IN max packet size.
    uint16_t bulk_out_max_packet;    // Bulk OUT max packet size.
    uint8_t bulk_in_max_burst;       // Bulk IN max burst.
    uint8_t bulk_out_max_burst;      // Bulk OUT max burst.
    struct xhci_ring bulk_in_ring;   // Bulk IN transfer ring state.
    struct xhci_ring bulk_out_ring;  // Bulk OUT transfer ring state.
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

static inline void *xhci_input_context_ptr(void *base, const uint32_t index, const uint32_t ctx_size)
{
    const size_t offset = (ctx_size == 64u) ? 64u : 32u;
    return (void *)((uint8_t *)base + offset + (index * ctx_size));
}

static inline void *xhci_device_context_ptr(void *base, const uint32_t index, const uint32_t ctx_size)
{
    return (void *)((uint8_t *)base + (index * ctx_size));
}

static inline uint8_t xhci_endpoint_id(const uint8_t ep_addr)
{
    const uint8_t ep_num = ep_addr & USB_EP_ADDR_MASK;
    if (ep_num == 0) {
        return 1u;
    }
    const bool in_dir = (ep_addr & USB_EP_DIR_IN) != 0;
    return (uint8_t)(ep_num * 2u + (in_dir ? 1u : 0u));
}

bool xhci_ring_init(struct xhci_ring *ring, uint32_t trb_count);
uintptr_t xhci_ring_enqueue(struct xhci_ring *ring, const struct xhci_trb *trb);
bool xhci_wait_for_cmd_completion(struct xhci_controller *xhci,
                                  uintptr_t cmd_phys,
                                  uint8_t *slot_id_out);
void xhci_ring_doorbell(const struct xhci_controller *xhci,
                        uint8_t doorbell,
                        uint32_t value);

bool xhci_msc_parse_config(const struct xhci_device *dev,
                           const void *cfg_buf,
                           uint16_t total_len);
uint8_t xhci_msc_config_value(void);
bool xhci_msc_configure_endpoints(struct xhci_controller *xhci);
