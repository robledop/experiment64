#include "e1000.h"
#include "apic.h"
#include "debug.h"
#include "idt.h"
#include "io.h"
#include "net/arp.h"
#include "net/dhcp.h"
#include "net/helpers.h"
#include "net/network.h"
#include "pmm.h"
#include "string.h"
#include "terminal.h"
#include "tsc.h"
#include "vmm.h"
#include <stddef.h>

#define IRQ0 0x20
#define E1000_MMIO_SIZE 0x20000U

static uint8_t bar_type;                                        // Type of BAR0
static uint16_t io_base;                                        // IO Base Address
static uint64_t mem_base;                                       // MMIO Base Address
static bool eeprom_exists;                                      // A flag indicating if eeprom exists
static uint8_t mac[6];                                          // A buffer for storing the mac address
static struct e1000_rx_desc *rx_descs[E1000_RX_RING_SIZE];      // Receive Descriptor Buffers
static struct e1000_tx_desc *tx_descs[E1000_TX_RING_SIZE];      // Transmit Descriptor Buffers
static uint8_t *rx_buffers[E1000_RX_RING_SIZE];                 // Virtual receive buffers
static uint8_t *tx_buffers[E1000_TX_RING_SIZE];                 // Virtual transmit buffers
static uint16_t rx_cur;                                         // Current Receive Descriptor Buffer
static uint16_t tx_cur;                                         // Current Transmit Descriptor Buffer
static struct pci_device pci_device;
static bool e1000_initialized;

static bool e1000_start(void);
static void e1000_linkup(void);

uint32_t wait_for_network_timeout = 5000;

void wait_for_network(void)
{
    boot_message(INFO, "Waiting for DHCP offer...");
    uint32_t budget = wait_for_network_timeout;
    while (!network_is_ready() && budget-- > 0) {
        e1000_receive();  // poll RX ring while interrupts are unavailable
        tsc_sleep_ms(1);  // ~1ms
    }

    if (!network_is_ready()) {
        boot_message(ERROR, "Network failed to start");
    }
}

/**
 * @brief Convert virtual address to physical address using HHDM offset.
 */
static uintptr_t virt_to_phys(const void *virt)
{
    if (!virt) {
        return 0;
    }
    return (uintptr_t)virt - g_hhdm_offset;
}

/**
 * @brief Write a 32-bit value to an e1000 register via MMIO or IO space.
 */
static void e1000_write_command(const uint16_t p_address, const uint32_t p_value)
{
    if (bar_type == PCI_BAR_MEM) {
        write32(mem_base + p_address, p_value);
    } else {
        outl(io_base, p_address);
        outl(io_base + 4, p_value);
    }
}

/**
 * @brief Read a 32-bit value from an e1000 register via MMIO or IO space.
 */
static uint32_t e1000_read_command(const uint16_t p_address)
{
    if (bar_type == PCI_BAR_MEM) {
        return read32(mem_base + p_address);
    }

    outl(io_base, p_address);
    return inl(io_base + 4);
}

/**
 * @brief Detect whether the controller has an EEPROM attached.
 *
 * @return true if an EEPROM is present, false otherwise.
 */
static bool e1000_detect_eeprom(void)
{
    uint32_t val = 0;
    e1000_write_command(REG_EERD, 0x1);

    for (int i = 0; i < 1000 && !eeprom_exists; i++) {
        val = e1000_read_command(REG_EERD);
        if (val & 0x10) {
            eeprom_exists = true;
        } else {
            eeprom_exists = false;
        }
    }
    return eeprom_exists;
}

/**
 * @brief Read a 16-bit word from the controller's EEPROM.
 *
 * @param addr EEPROM word address.
 * @return Value read from the EEPROM.
 */
static uint32_t e1000_eeprom_read(const uint8_t addr)
{
    uint16_t data = 0;
    uint32_t tmp = 0;
    if (eeprom_exists) {
        e1000_write_command(REG_EERD, (1) | ((uint32_t)(addr) << 8));
        while (!((tmp = e1000_read_command(REG_EERD)) & (1 << 4)))
            ;
    } else {
        e1000_write_command(REG_EERD, (1) | ((uint32_t)(addr) << 2));
        while (!((tmp = e1000_read_command(REG_EERD)) & (1 << 1)))
            ;
    }
    data = (uint16_t)((tmp >> 16) & 0xFFFF);
    return data;
}

/**
 * @brief Populate the MAC address from EEPROM or MMIO registers.
 *
 * @return true if a MAC address was successfully read.
 */
static bool e1000_read_mac_address(void)
{
    if (eeprom_exists) {
        uint32_t temp = e1000_eeprom_read(0);
        mac[0] = temp & 0xff;
        mac[1] = temp >> 8;
        temp = e1000_eeprom_read(1);
        mac[2] = temp & 0xff;
        mac[3] = temp >> 8;
        temp = e1000_eeprom_read(2);
        mac[4] = temp & 0xff;
        mac[5] = temp >> 8;
    } else {
        const uint8_t *mem_base_mac_8 = (uint8_t *)(uintptr_t)(mem_base + 0x5400);
        const uint32_t *mem_base_mac_32 = (uint32_t *)(uintptr_t)(mem_base + 0x5400);
        if (mem_base_mac_32[0] != 0) {
            for (int i = 0; i < 6; i++) {
                mac[i] = mem_base_mac_8[i];
            }
        } else {
            return false;
        }
    }

    network_set_mac(mac);
    return true;
}

/**
 * @brief Initialise receive descriptors and configure the controller for RX.
 */
static void e1000_rx_init(void)
{
    uint8_t *ring = (uint8_t *)pmm_alloc_page();
    if (ring == nullptr) {
        panic("e1000_rx_init: no descriptor memory");
    }
    ring = (uint8_t *)((uintptr_t)ring + g_hhdm_offset);
    memset(ring, 0, PAGE_SIZE);

    for (int i = 0; i < E1000_RX_RING_SIZE; i++) {
        rx_descs[i] = (struct e1000_rx_desc *)(ring + i * sizeof(struct e1000_rx_desc));
        void *buf_phys = pmm_alloc_page();
        if (buf_phys == nullptr) {
            panic("e1000_rx_init: no rx buffer");
        }
        rx_buffers[i] = (uint8_t *)((uintptr_t)buf_phys + g_hhdm_offset);
        memset(rx_buffers[i], 0, PAGE_SIZE);
        rx_descs[i]->addr = (uintptr_t)buf_phys;
        rx_descs[i]->status = 0;
    }

    e1000_write_command(REG_RXDESCLO, virt_to_phys(ring));
    e1000_write_command(REG_RXDESCHI, 0);

    e1000_write_command(REG_RXDESCLEN, E1000_RX_RING_SIZE * sizeof(struct e1000_rx_desc));

    e1000_write_command(REG_RXDESCHEAD, 0);
    e1000_write_command(REG_RXDESCTAIL, E1000_RX_RING_SIZE - 1);
    rx_cur = 0;
    e1000_write_command(REG_RCTRL,
                        RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE | RTCL_RDMTS_HALF | RCTL_BAM | RCTL_SECRC |
                            RCTL_BSIZE_4096);
}

/**
 * @brief Initialise transmit descriptors and enable transmission.
 */
static void e1000_tx_init(void)
{
    uint8_t *ring = (uint8_t *)pmm_alloc_page();
    if (ring == nullptr) {
        panic("e1000_tx_init: no descriptor memory");
    }
    ring = (uint8_t *)((uintptr_t)ring + g_hhdm_offset);
    memset(ring, 0, PAGE_SIZE);

    for (int i = 0; i < E1000_TX_RING_SIZE; i++) {
        tx_descs[i] = (struct e1000_tx_desc *)(ring + i * sizeof(struct e1000_tx_desc));
        void *buf_phys = pmm_alloc_page();
        if (buf_phys == nullptr) {
            panic("e1000_tx_init: no tx buffer");
        }
        tx_buffers[i] = (uint8_t *)((uintptr_t)buf_phys + g_hhdm_offset);
        memset(tx_buffers[i], 0, PAGE_SIZE);
        tx_descs[i]->addr = (uintptr_t)buf_phys;
        tx_descs[i]->cmd = 0;
        tx_descs[i]->status = TSTA_DD;
    }

    e1000_write_command(REG_TXDESCLO, virt_to_phys(ring));
    e1000_write_command(REG_TXDESCHI, 0);

    e1000_write_command(REG_TXDESCLEN, E1000_TX_RING_SIZE * sizeof(struct e1000_tx_desc));

    e1000_write_command(REG_TXDESCHEAD, 0);
    e1000_write_command(REG_TXDESCTAIL, 0);
    tx_cur = 0;
    e1000_write_command(REG_TCTRL, TCTL_EN | TCTL_PSP | (15 << TCTL_CT_SHIFT) | (64 << TCTL_COLD_SHIFT) | TCTL_RTLC);
}

/**
 * @brief Unmask e1000 interrupts and clear pending status bits.
 */
static void e1000_enable_interrupt(void)
{
    e1000_write_command(REG_IMS, E1000_IMS_ENABLE_MASK);
    e1000_read_command(REG_ICR);
}

/**
 * @brief Interrupt service routine for e1000 events.
 */
static void e1000_interrupt_handler(struct interrupt_frame *frame)
{
    (void)frame;

    // Mask device interrupts while we process the current event.
    e1000_write_command(REG_IMC, E1000_IMS_ENABLE_MASK);

    const uint32_t status = e1000_read_command(REG_ICR);
    if (status & E1000_LSC) {
        e1000_linkup();
    }
    if (status & (E1000_RXDMT0 | E1000_RX0 | E1000_RXT0)) {
        e1000_receive();
    }

    e1000_write_command(REG_IMS, E1000_IMS_ENABLE_MASK);
    apic_send_eoi();
}

/**
 * @brief Log the detected MAC address to the console.
 */
static void e1000_print_mac_address(void)
{
    boot_message(INFO, "[E1000] MAC Address: %s", get_mac_address_string(mac));
}

/**
 * @brief Force the link-up state in the controller's control register.
 */
static void e1000_linkup(void)
{
    uint32_t val = e1000_read_command(REG_CTRL);
    val |= ECTRL_SLU;
    e1000_write_command(REG_CTRL, val);
}

/**
 * @brief Bring the controller online, register interrupts, and start queues.
 *
 * @return true on success, false otherwise.
 */
static bool e1000_start(void)
{
    e1000_detect_eeprom();
    if (!e1000_read_mac_address()) {
        return false;
    }
    e1000_print_mac_address();
    e1000_linkup();

    // Clear multicast table array
    for (int i = 0; i < 0x80; i++) {
        e1000_write_command(REG_MTA + i * 4, 0);
    }

    const uint8_t irq = pci_device.header.irq;
    const uint8_t vector = IRQ0 + irq;

    apic_enable_irq(irq, vector);
    register_interrupt_handler(vector, e1000_interrupt_handler);
    e1000_enable_interrupt();
    e1000_rx_init();
    e1000_tx_init();

    // Mark as initialized before sending DHCP discover so e1000_send_packet works
    e1000_initialized = true;

    dhcp_send_discover(mac);
    return true;
}

/**
 * @brief Probe PCI resources and start the e1000 network controller.
 *
 * @param device PCI device descriptor for the controller.
 */
void e1000_init(struct pci_device device)
{
#ifdef TEST_MODE
    // Skip e1000 initialization in test mode - KASAN doesn't know about MMIO regions
    (void)device;
    boot_message(INFO, "[E1000] Skipping initialization in test mode");
    return;
#endif

    pci_device = device;
    bar_type = pci_get_bar(pci_device, PCI_BAR_MEM) & 1;
    io_base = pci_get_bar(pci_device, PCI_BAR_IO) & ~1;
    uint32_t mem_base_raw = pci_get_bar(pci_device, PCI_BAR_MEM) & ~3;

    if (bar_type == PCI_BAR_MEM && mem_base_raw != 0) {
        // Map MMIO region using HHDM offset
        mem_base = (uint64_t)mem_base_raw + g_hhdm_offset;
    } else if (io_base != 0) {
        // IO space fallback
        mem_base = 0;
    } else {
        boot_message(ERROR, "[E1000] No valid BAR found");
        return;
    }

    pci_enable_bus_mastering(device);
    eeprom_exists = false;

    if (e1000_start()) {
        arp_init();
        wait_for_network();
    } else {
        boot_message(ERROR, "[E1000] Failed to start");
    }
}

/**
 * @brief Process all packets currently available in the receive ring.
 */
void e1000_receive(void)
{
    while ((rx_descs[rx_cur]->status & E1000_RXD_STAT_DD)) {
        uint8_t *buf = rx_buffers[rx_cur];
        const uint16_t len = rx_descs[rx_cur]->length;

        if (!(rx_descs[rx_cur]->status & E1000_RXD_STAT_EOP)) {
            // TODO: Handle incomplete packets
            rx_descs[rx_cur]->status = 0;
            const uint16_t old_cur = rx_cur;
            rx_cur = (rx_cur + 1) % E1000_RX_RING_SIZE;
            e1000_write_command(REG_RXDESCTAIL, old_cur);
            continue;
        }

        network_receive(buf, len);

        rx_descs[rx_cur]->status = 0;
        const uint16_t old_cur = rx_cur;
        rx_cur = (rx_cur + 1) % E1000_RX_RING_SIZE;
        e1000_write_command(REG_RXDESCTAIL, old_cur);
    }
}

/**
 * @brief Submit a frame for transmission.
 *
 * @param data Pointer to the Ethernet frame.
 * @param len Frame length in bytes.
 * @return 0 on success, -1 on error.
 */
int e1000_send_packet(const void *data, const uint16_t len)
{
    if (!e1000_initialized) {
        return -1;
    }

    const uint8_t slot = tx_cur;

    // Wait for descriptor to be available
    while ((tx_descs[slot]->status & TSTA_DD) == 0)
        ;

    if (len > PAGE_SIZE) {
        return -1;
    }

    memcpy(tx_buffers[slot], data, len);

    tx_descs[slot]->length = len;
    tx_descs[slot]->cmd = CMD_EOP | CMD_IFCS | CMD_RS;
    tx_descs[slot]->status = 0;

    tx_cur = (tx_cur + 1) % E1000_TX_RING_SIZE;
    e1000_write_command(REG_TXDESCTAIL, tx_cur);

    // Wait for transmission to complete
    while ((tx_descs[slot]->status & TSTA_DD) == 0)
        ;

    return 0;
}

/**
 * @brief Get the MAC address of the e1000 controller.
 *
 * @param mac_out Buffer to store the 6-byte MAC address.
 */
void e1000_get_mac(uint8_t *mac_out)
{
    if (mac_out) {
        memcpy(mac_out, mac, 6);
    }
}
