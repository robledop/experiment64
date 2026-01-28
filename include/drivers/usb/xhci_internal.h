#pragma once

#include <drivers/pci.h>
#include <drivers/tsc.h>
#include <lib/string.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <stddef.h>
#include <stdint.h>

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
#define XHCI_ERDP_EHB (1ull << 3) // Event handler busy bit in ERDP.
#define XHCI_USBCMD_RS (1u << 0) // Run/Stop bit.
#define XHCI_USBCMD_HCRST (1u << 1) // Host controller reset bit.
#define XHCI_USBCMD_INTE (1u << 2) // Interrupt enable bit.
#define XHCI_USBSTS_HCH (1u << 0) // Host controller halted bit.
#define XHCI_USBSTS_CNR (1u << 11) // Controller not ready bit.
#define XHCI_TRB_CYCLE 0x1u // TRB cycle bit.
#define XHCI_TRB_TC (1u << 1) // Link TRB toggle cycle bit.
#define XHCI_TRB_ISP (1u << 2) // Interrupt on short packet bit.
#define XHCI_TRB_CHAIN (1u << 4) // TRB chain bit.
#define XHCI_TRB_IOC (1u << 5) // Interrupt on completion bit.
#define XHCI_TRB_IDT (1u << 6) // Immediate data bit.
#define XHCI_TRB_DIR_IN (1u << 16) // Data-in direction bit.
#define XHCI_TRB_TYPE_SHIFT 10u // TRB type field shift.
#define XHCI_TRB_TYPE_MASK (0x3Fu << XHCI_TRB_TYPE_SHIFT) // TRB type field mask.
#define XHCI_TRB_TRT_SHIFT 16u // Transfer type field shift for setup stage.
#define XHCI_TRB_TRT_DATA_OUT 2u // Setup stage transfer type: data out.
#define XHCI_TRB_TRT_DATA_IN 3u // Setup stage transfer type: data in.
#define XHCI_TRB_LEN_MASK 0x1FFFFu // TRB transfer length mask.
#define XHCI_TRB_TD_SIZE_SHIFT 17u // TRB transfer descriptor size shift.
#define XHCI_TRB_INTR_TARGET_SHIFT 22u // TRB interrupter target shift.
#define XHCI_TRB_LEN(value) ((value) & XHCI_TRB_LEN_MASK) // TRB transfer length field.
#define XHCI_TRB_TD_SIZE(value) (((value) & 0x1Fu) << XHCI_TRB_TD_SIZE_SHIFT) // TRB transfer descriptor size field.
#define XHCI_TRB_INTR_TARGET(value) ((value) << XHCI_TRB_INTR_TARGET_SHIFT) // TRB interrupter target field.
#define XHCI_TRB_TYPE_NORMAL 1u // Normal TRB type.
#define XHCI_TRB_TYPE_SETUP_STAGE 2u // Setup stage TRB type.
#define XHCI_TRB_TYPE_DATA_STAGE 3u // Data stage TRB type.
#define XHCI_TRB_TYPE_STATUS_STAGE 4u // Status stage TRB type.
#define XHCI_TRB_TYPE_LINK 6u // Link TRB type.
#define XHCI_TRB_TYPE_ENABLE_SLOT 9u // Enable Slot command TRB type.
#define XHCI_TRB_TYPE_DISABLE_SLOT 10u // Disable Slot command TRB type.
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11u // Address Device command TRB type.
#define XHCI_TRB_TYPE_CONFIGURE_ENDPOINT 12u // Configure Endpoint command TRB type.
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32u // Transfer event TRB type.
#define XHCI_TRB_TYPE_CMD_COMPLETION 33u // Command completion event TRB type.
#define XHCI_TRB_TYPE_PORT_STATUS 34u // Port status change event TRB type.
#define XHCI_COMPLETION_SUCCESS 1u // Command completion code for success.
#define XHCI_COMPLETION_SHORT_PACKET 13u // Transfer completion short packet code.
#define XHCI_COMPLETION_USB_TRANSACTION_ERROR 4u // Completion code for USB transaction error.
#define XHCI_COMPLETION_STALL_ERROR 6u // Completion code for stall error.
#define XHCI_COMPLETION_DATA_BUFFER_ERROR 7u // Completion code for data buffer error.
#define XHCI_COMPLETION_CONTEXT_STATE_ERROR 19u // Completion code for context state error.
#define XHCI_CMD_RING_TRBS 256u // Command ring TRB count.
#define XHCI_EVENT_RING_TRBS 256u // Event ring TRB count.
#define XHCI_MMIO_MAP_BYTES 0x100000u // MMIO mapping size.
#define XHCI_RESET_TIMEOUT_MS 1000u // Reset timeout in ms.
#define XHCI_PORT_POWER_DELAY_MS 20u // Port power settle delay in ms.
#define XHCI_PORT_CONNECT_DELAY_MS 100u // Port connect debounce delay in ms.
#define XHCI_ADDRESS_SETTLE_MS 50u // Address settle delay in ms.
#define XHCI_TIMEOUT_MS 200u // Generic timeout in ms.
#define XHCI_TRANSFER_TIMEOUT_MS 1000u // Transfer timeout in ms.
#define XHCI_WAIT_SPIN_COUNT 256u // Spin count before sleeping in wait loops.
#define XHCI_WAIT_SLEEP_NS 50000ull // Sleep duration in ns after spin budget.
#define XHCI_PORT_RESET_TIMEOUT_MS 500u // Port reset timeout in ms.
#define XHCI_USB3_RESET_RECOVERY_MS 50u // USB3 warm reset recovery delay in ms.
#define XHCI_EP_TYPE_CONTROL 4u // Endpoint type for control transfers.
#define XHCI_EP_TYPE_BULK_OUT 2u // Endpoint type for bulk OUT transfers.
#define XHCI_EP_TYPE_BULK_IN 6u // Endpoint type for bulk IN transfers.
#define XHCI_EP_ERROR_COUNT_SHIFT 1u // Endpoint error count field shift.
#define XHCI_EP_TYPE_SHIFT 3u // Endpoint type field shift.
#define XHCI_EP_MAX_BURST_SHIFT 8u // Endpoint max burst field shift.
#define XHCI_EP_MAX_PACKET_SHIFT 16u // Endpoint max packet size field shift.
#define XHCI_SLOT_CTX_CTX_ENTRIES_SHIFT 27u // Slot context entry count field shift.
#define XHCI_SLOT_CTX_SPEED_SHIFT 20u // Slot context speed field shift.
#define XHCI_SLOT_CTX_ROOT_PORT_SHIFT 16u // Slot context root port field shift.
#define XHCI_MAX_CONTEXTS 32u // Max context entries per device context.
#define XHCI_OP_PORTSC_BASE 0x400u // Port status/control base offset.
#define XHCI_OP_PORTSC_STRIDE 0x10u // Port status/control register stride.
#define XHCI_PORTSC_CCS (1u << 0) // Port current connect status bit.
#define XHCI_PORTSC_PED (1u << 1) // Port enabled/disabled bit.
#define XHCI_PORTSC_PR (1u << 4) // Port reset bit.
#define XHCI_PORTSC_PLS_SHIFT 5u // Port link state field shift.
#define XHCI_PORTSC_PLS_MASK (0xFu << XHCI_PORTSC_PLS_SHIFT) // Port link state field mask.
#define XHCI_PORTSC_PP (1u << 9) // Port power bit.
#define XHCI_PORTSC_SPEED_SHIFT 10u // Port speed field shift.
#define XHCI_PORTSC_SPEED_MASK (0xFu << XHCI_PORTSC_SPEED_SHIFT) // Port speed field mask.
#define XHCI_PORTSC_INDICATOR_MASK (0x3u << 14) // Port indicator control mask.
#define XHCI_PORTSC_WRC (1u << 19) // Port warm reset change bit.
#define XHCI_PORTSC_WK_MASK (0x7u << 25) // Port wake event mask.
#define XHCI_PORTSC_WR (1u << 31) // Port warm reset bit.
#define XHCI_PORTSC_RWS_MASK (XHCI_PORTSC_PLS_MASK | XHCI_PORTSC_PP | XHCI_PORTSC_INDICATOR_MASK | XHCI_PORTSC_WK_MASK) // Port read/write settable bits.
#define XHCI_INTEL_USB3_PSSEN 0xD0u // Intel USB3 port routing enable register offset in PCI config space.
#define XHCI_INTEL_XUSB2PR 0xD8u // Intel USB2 port routing register offset in PCI config space.
#define XHCI_EP0_RING_TRBS 256u // Endpoint 0 ring TRB count.
#define XHCI_MAX_DEVICES 256u // Max device slots tracked.

#define USB_DESC_TYPE_DEVICE 0x01u // USB device descriptor type.
#define USB_DESC_TYPE_CONFIGURATION 0x02u // USB configuration descriptor type.
#define USB_DESC_TYPE_INTERFACE 0x04u // USB interface descriptor type.
#define USB_DESC_TYPE_ENDPOINT 0x05u // USB endpoint descriptor type.
#define USB_DESC_TYPE_SS_ENDPOINT_COMP 0x30u // USB SuperSpeed endpoint companion descriptor type.
#define USB_REQ_GET_DESCRIPTOR 0x06u // USB standard GET_DESCRIPTOR request.
#define USB_REQ_SET_CONFIGURATION 0x09u // USB standard SET_CONFIGURATION request.
#define USB_CLASS_MASS_STORAGE 0x08u // USB mass storage device class code.
#define USB_SUBCLASS_SCSI 0x06u // USB SCSI transparent subclass code.
#define USB_PROTOCOL_BOT 0x50u // USB bulk-only transport protocol code.
#define USB_EP_DIR_IN 0x80u // Endpoint address IN direction bit.
#define USB_EP_ADDR_MASK 0x0Fu // Endpoint number mask.
#define USB_EP_ATTR_TYPE_MASK 0x03u // Endpoint attribute transfer type mask.
#define USB_EP_ATTR_TYPE_BULK 0x02u // Endpoint attribute bulk transfer type.
#define USB_MSC_CBW_SIGNATURE 0x43425355u // MSC command block wrapper signature.
#define USB_MSC_CSW_SIGNATURE 0x53425355u // MSC command status wrapper signature.
#define XHCI_MSC_DATA_BYTES (256u * 1024u) // MSC data buffer size in bytes.

struct xhci_trb
{
    uint32_t dword0; // TRB payload dword 0.
    uint32_t dword1; // TRB payload dword 1.
    uint32_t dword2; // TRB payload dword 2.
    union
    {
        struct
        {
            uint32_t cycle_bit : 1; // TRB cycle bit.
            uint32_t rsvd0 : 9;     // Reserved, must be zero.
            uint32_t trb_type : 6;  // TRB type field.
            uint32_t rsvd1 : 16;    // Reserved, must be zero.
        };

        uint32_t dword3; // TRB control and type dword.
    };
} __attribute__((packed));

static_assert(sizeof(struct xhci_trb) == sizeof(uint32_t) * 4);

struct xhci_erst_entry
{
    uint64_t addr;     // Physical base of event ring segment.
    uint32_t size;     // TRB count in this segment.
    uint32_t reserved; // Reserved, must be zero.
} __attribute__((packed));

static_assert(sizeof(struct xhci_erst_entry) == sizeof(uint32_t) * 4);

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

static_assert(sizeof(struct xhci_input_control_ctx) == sizeof(uint32_t) * 8);

struct xhci_slot_ctx
{
    uint32_t dev_info;    // Slot state, speed, and context count.
    uint32_t dev_info2;   // Root port and hub information.
    uint32_t tt_info;     // Transaction translator info.
    uint32_t dev_state;   // Slot state and device address.
    uint32_t reserved[4]; // Reserved, must be zero.
} __attribute__((packed));

static_assert(sizeof(struct xhci_slot_ctx) == sizeof(uint32_t) * 8);

struct xhci_ep_ctx
{
    uint32_t ep_info;     // Endpoint state and properties.
    uint32_t ep_info2;    // Endpoint type and max packet size.
    uint64_t deq;         // Endpoint dequeue pointer.
    uint32_t tx_info;     // Endpoint transfer settings.
    uint32_t reserved[3]; // Reserved, must be zero.
} __attribute__((packed));

static_assert(sizeof(struct xhci_ep_ctx) == sizeof(uint32_t) * 8);

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

static_assert(sizeof(struct usb_setup_packet) == sizeof(uint32_t) * 2);

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

struct usb_msc_cbw
{
    uint32_t signature;            // CBW signature.
    uint32_t tag;                  // CBW command tag.
    uint32_t data_transfer_length; // Data transfer length in bytes.
    uint8_t flags;                 // Direction and flags.
    uint8_t lun;                   // Logical unit number.
    uint8_t cb_length;             // Command block length.
    uint8_t cb[16];                // SCSI command block.
} __attribute__((packed));

struct usb_msc_csw
{
    uint32_t signature;    // CSW signature.
    uint32_t tag;          // CSW command tag.
    uint32_t data_residue; // Remaining data length.
    uint8_t status;        // CSW status code.
} __attribute__((packed));

struct xhci_msc_device
{
    bool active;                    // MSC device is active.
    struct xhci_device *dev;        // Associated xHCI device.
    uint8_t config_value;           // Configuration value to select.
    uint8_t interface_number;       // MSC interface number.
    uint8_t bulk_in_ep;             // Bulk IN endpoint address.
    uint8_t bulk_out_ep;            // Bulk OUT endpoint address.
    uint8_t bulk_in_id;             // xHCI endpoint ID for bulk IN.
    uint8_t bulk_out_id;            // xHCI endpoint ID for bulk OUT.
    uint16_t bulk_in_max_packet;    // Bulk IN max packet size.
    uint16_t bulk_out_max_packet;   // Bulk OUT max packet size.
    uint8_t bulk_in_max_burst;      // Bulk IN max burst.
    uint8_t bulk_out_max_burst;     // Bulk OUT max burst.
    struct xhci_ring bulk_in_ring;  // Bulk IN transfer ring state.
    struct xhci_ring bulk_out_ring; // Bulk OUT transfer ring state.
    void *cbw_buf;                  // CBW buffer virtual base.
    uintptr_t cbw_phys;             // CBW buffer physical base.
    void *csw_buf;                  // CSW buffer virtual base.
    uintptr_t csw_phys;             // CSW buffer physical base.
    void *data_buf;                 // Data transfer buffer virtual base.
    uintptr_t data_phys;            // Data transfer buffer physical base.
    uint32_t data_bytes;            // Data buffer size in bytes.
    uint32_t tag;                   // CBW tag counter.
    uint32_t block_size;            // MSC logical block size.
    uint64_t block_count;           // MSC total block count.
};

struct xhci_controller
{
    struct pci_device pci;           // PCI device descriptor for config access.
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

extern struct xhci_controller g_xhci;
extern struct xhci_device g_xhci_devices[XHCI_MAX_DEVICES];

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

static inline uint32_t xhci_pci_read32(const struct pci_device *device, const uint8_t offset)
{
    const uint16_t low  = pci_config_read_word(device->bus, device->slot, device->function, offset);
    const uint16_t high = pci_config_read_word(device->bus, device->slot, device->function, offset + 2u);
    return (uint32_t)low | ((uint32_t)high << 16);
}

static inline void xhci_pci_write32(const struct pci_device *device, const uint8_t offset, const uint32_t value)
{
    pci_config_write_word(device->bus, device->slot, device->function, offset, (uint16_t)value);
    pci_config_write_word(device->bus, device->slot, device->function, offset + 2u, (uint16_t)(value >> 16));
}

static inline bool xhci_wait_for(const volatile uint8_t *base,
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

static inline uint32_t xhci_packet_count(const uint32_t bytes, const uint16_t max_packet)
{
    if (max_packet == 0) {
        return 0;
    }
    return (bytes + max_packet - 1u) / max_packet;
}

static inline pml4_t xhci_current_pml4(void)
{
    uint64_t cr3 = 0;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    return (pml4_t)(cr3 & 0x000FFFFFFFFFF000ull);
}

static inline bool xhci_virt_mapped(const uint64_t *pml4, const uint64_t virt)
{
    auto pml4_virt        = (const uint64_t *)((uint64_t)pml4 + g_hhdm_offset);
    const size_t pml4_idx = (virt >> 39) & 0x1FFu;
    const uint64_t pml4e  = pml4_virt[pml4_idx];
    if ((pml4e & PTE_PRESENT) == 0) {
        return false;
    }

    auto pdpt             = (const uint64_t *)((pml4e & 0x000FFFFFFFFFF000ull) + g_hhdm_offset);
    const size_t pdpt_idx = (virt >> 30) & 0x1FFu;
    const uint64_t pdpte  = pdpt[pdpt_idx];
    if ((pdpte & PTE_PRESENT) == 0) {
        return false;
    }
    if ((pdpte & PTE_HUGE) != 0) {
        return true;
    }

    auto pd             = (const uint64_t *)((pdpte & 0x000FFFFFFFFFF000ull) + g_hhdm_offset);
    const size_t pd_idx = (virt >> 21) & 0x1FFu;
    const uint64_t pde  = pd[pd_idx];
    if ((pde & PTE_PRESENT) == 0) {
        return false;
    }
    if ((pde & PTE_HUGE) != 0) {
        return true;
    }

    auto pt             = (const uint64_t *)((pde & 0x000FFFFFFFFFF000ull) + g_hhdm_offset);
    const size_t pt_idx = (virt >> 12) & 0x1FFu;
    return (pt[pt_idx] & PTE_PRESENT) != 0;
}

static inline void xhci_map_mmio_range(const uint64_t phys_base, const uint64_t bytes)
{
    const uint64_t start = phys_base & ~(uint64_t)(PAGE_SIZE - 1u);
    const uint64_t end   = (phys_base + bytes + PAGE_SIZE - 1u) & ~(uint64_t)(PAGE_SIZE - 1u);
    pml4_t pml4          = xhci_current_pml4();
    for (uint64_t phys = start; phys < end; phys += PAGE_SIZE) {
        const uint64_t virt = phys + g_hhdm_offset;
        if (!xhci_virt_mapped(pml4, virt)) {
            vmm_map_page(pml4, virt, phys, PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT);
        }
    }
}

static inline uint32_t xhci_be32(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24u) |
        ((uint32_t)buf[1] << 16u) |
        ((uint32_t)buf[2] << 8u) |
        (uint32_t)buf[3];
}

static inline void xhci_put_be16(uint8_t *buf, const uint16_t value)
{
    buf[0] = (uint8_t)(value >> 8u);
    buf[1] = (uint8_t)value;
}

static inline void xhci_put_be32(uint8_t *buf, const uint32_t value)
{
    buf[0] = (uint8_t)(value >> 24u);
    buf[1] = (uint8_t)(value >> 16u);
    buf[2] = (uint8_t)(value >> 8u);
    buf[3] = (uint8_t)value;
}

static inline bool xhci_alloc_pages(const size_t bytes, uintptr_t *phys_out, void **virt_out)
{
    const size_t pages = (bytes + PAGE_SIZE - 1u) / PAGE_SIZE;
    void *phys         = pmm_alloc_pages(pages);
    if (!phys) {
        return false;
    }

    void *virt = (void *)((uintptr_t)phys + g_hhdm_offset);
    memset(virt, 0, pages * PAGE_SIZE);

    *phys_out = (uintptr_t)phys;
    *virt_out = virt;
    return true;
}

static inline void *xhci_input_context_ptr(void *base, const uint32_t index, const uint32_t ctx_size)
{
    const size_t offset = (ctx_size == 64u) ? 64u : 32u;
    return (void *)((uint8_t *)base + offset + (index * ctx_size));
}

static inline void *xhci_device_context_ptr(void *base, const uint32_t index, const uint32_t ctx_size)
{
    return (void *)((uint8_t *)base + (index * ctx_size));
}

static inline struct xhci_device *xhci_device_from_slot(const uint8_t slot_id)
{
    return &g_xhci_devices[slot_id];
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
void xhci_ring_reset(struct xhci_ring *ring);
uintptr_t xhci_ring_enqueue(struct xhci_ring *ring, const struct xhci_trb *trb);
bool xhci_event_ring_init(struct xhci_event_ring *ring, uint32_t trb_count);
bool xhci_wait_for_cmd_completion(struct xhci_controller *xhci,
                                  uintptr_t cmd_phys,
                                  uint8_t *slot_id_out);
bool xhci_wait_for_transfer_event(struct xhci_controller *xhci,
                                  uintptr_t trb_phys,
                                  uint8_t slot_id,
                                  uint8_t ep_id,
                                  bool require_ptr_match);
void xhci_ring_doorbell(const struct xhci_controller *xhci,
                        uint8_t doorbell,
                        uint32_t value);

bool xhci_port_reset(const struct xhci_controller *xhci,
                     uint32_t port,
                     uint32_t *portsc_out);
bool xhci_enable_slot(struct xhci_controller *xhci,
                      uint8_t *slot_id_out);
bool xhci_disable_slot(struct xhci_controller *xhci,
                       uint8_t slot_id);
bool xhci_alloc_device_context(struct xhci_controller *xhci,
                               struct xhci_device *dev);
bool xhci_prepare_slot_context(struct xhci_controller *xhci,
                               struct xhci_device *dev);
bool xhci_address_device(struct xhci_controller *xhci,
                         struct xhci_device *dev);
bool xhci_control_transfer(struct xhci_controller *xhci,
                           struct xhci_device *dev,
                           const struct usb_setup_packet *setup,
                           uintptr_t data_phys,
                           uint32_t data_len,
                           bool data_in);
bool xhci_set_configuration(struct xhci_controller *xhci,
                            struct xhci_device *dev,
                            uint8_t config_value);
bool xhci_get_device_descriptor(struct xhci_controller *xhci,
                                struct xhci_device *dev);
bool xhci_get_config_descriptor(struct xhci_controller *xhci,
                                struct xhci_device *dev);

bool xhci_msc_parse_config(const struct xhci_device *dev,
                           const void *cfg_buf,
                           uint16_t total_len);
uint8_t xhci_msc_config_value(void);
bool xhci_msc_configure_endpoints(struct xhci_controller *xhci);
bool xhci_msc_init(struct xhci_controller *xhci);
