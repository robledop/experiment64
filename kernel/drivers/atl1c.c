// SPDX-License-Identifier: GPL-2.0-only
// This driver is derived from Linux alx and is GPL-2.0-only.
#include <drivers/atl1c.h>
#include <drivers/terminal.h>
#include <drivers/tsc.h>
#include <lib/mmio.h>
#include <lib/string.h>
#include <arpa/inet.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <mem/heap.h>
#include <mem/dma.h>
#include <mem/pmm.h>
#include <net/helpers.h>
#include <net/network.h>
#include <net/arp.h>
#include <net/dhcp.h>
#include <mem/vmm.h>
#include <arch/x86_64/apic.h>
#include <arch/x86_64/idt.h>
#include <task/spinlock.h>

#define ATL1C_RESET_DELAY_MS 10
#define ATL1C_MDIO_MAX_WAIT 200
#define ATL1C_RX_BURST_PREF 8u
#define ATL1C_TX_BURST_PREF 5u

static uint64_t mmio_base;
static uint8_t mac[6];
static bool atl1c_initialized;
static uint32_t wait_for_network_timeout = 5000;

#define ATL1C_RX_RING_SIZE 64
#define ATL1C_TX_RING_SIZE 32
#define ATL1C_RX_BUF_SIZE 2048

static struct atl1c_rx_free_desc *rfd_ring;
static struct atl1c_recv_ret_status *rrd_ring;
static struct atl1c_tpd_desc *tpd_ring;
static uint8_t *rx_buffers[ATL1C_RX_RING_SIZE];
static uint8_t *tx_buffers[ATL1C_TX_RING_SIZE];
static uintptr_t rx_phys[ATL1C_RX_RING_SIZE];
static uintptr_t tx_phys[ATL1C_TX_RING_SIZE];
static uint16_t rrd_cur;
static uint16_t tpd_cur;
static uint16_t rfd_prod;
static spinlock_t atl1c_tx_lock;
static uint32_t rx_rrd_updates;
static uint32_t rx_packets;
static uint32_t tx_packets;
[[maybe_unused]] static uint32_t rx_debug_left = 8;
[[maybe_unused]] static uint32_t tx_debug_left = 8;
static uint32_t tx_fail_debug_left             = 8;
static bool atl1c_link_up;
static bool atl1c_dhcp_started;
static uint8_t atl1c_irq_vector;
static uint32_t atl1c_intr_mask;
static uint32_t atl1c_irq_hits;
[[maybe_unused]] static uint32_t atl1c_irq_debug_left = 8;
static bool atl1c_irq_kick_done;
static bool atl1c_msix_enabled;
static bool atl1c_msi_enabled;

static inline uint32_t atl1c_read32(uint32_t reg);
static inline void atl1c_write32(uint32_t reg, uint32_t value);
static void atl1c_handle_isr(uint32_t status, bool from_irq);

static uint8_t atl1c_get_revision(struct pci_device device);
static void atl1c_reset_osc(uint8_t revision);

static inline void atl1c_mb(void)
{
    __asm__ volatile("" ::: "memory");
}

static bool atl1c_mdio_wait_idle(void)
{
    for (uint32_t i = 0; i < ATL1C_MDIO_MAX_WAIT; i++) {
        const uint32_t val = atl1c_read32(REG_MDIO_CTRL);
        if ((val & (MDIO_CTRL_BUSY | MDIO_CTRL_START)) == 0) {
            return true;
        }
    }
    return false;
}

static bool atl1c_mdio_read(const uint8_t reg, uint16_t *out)
{
    if (!out)
        return false;

    const uint32_t val = MDIO_CTRL_SPRES_PRMBL |
        (0u << MDIO_CTRL_CLK_SEL_SHIFT) |
        ((uint32_t)reg << MDIO_CTRL_REG_SHIFT) |
        MDIO_CTRL_START |
        MDIO_CTRL_OP_READ;
    atl1c_write32(REG_MDIO_CTRL, val);
    if (!atl1c_mdio_wait_idle()) {
        return false;
    }

    const uint32_t res = atl1c_read32(REG_MDIO_CTRL);
    *out               = (uint16_t)(res & 0xFFFFu);
    return true;
}

static bool atl1c_mdio_write(const uint8_t reg, const uint16_t data)
{
    const uint32_t val = MDIO_CTRL_SPRES_PRMBL |
        (0u << MDIO_CTRL_CLK_SEL_SHIFT) |
        ((uint32_t)reg << MDIO_CTRL_REG_SHIFT) |
        ((uint32_t)data << MDIO_CTRL_DATA_SHIFT) |
        MDIO_CTRL_START;
    atl1c_write32(REG_MDIO_CTRL, val);
    return atl1c_mdio_wait_idle();
}

static bool atl1c_mdio_read_dbg(const uint16_t reg, uint16_t *out)
{
    constexpr uint8_t MII_DBG_ADDR = 0x1D;
    constexpr uint8_t MII_DBG_DATA = 0x1E;
    if (!atl1c_mdio_write(MII_DBG_ADDR, reg)) {
        return false;
    }
    return atl1c_mdio_read(MII_DBG_DATA, out);
}

static bool atl1c_mdio_write_dbg(const uint16_t reg, const uint16_t data)
{
    constexpr uint8_t MII_DBG_ADDR = 0x1D;
    constexpr uint8_t MII_DBG_DATA = 0x1E;
    if (!atl1c_mdio_write(MII_DBG_ADDR, reg)) {
        return false;
    }
    return atl1c_mdio_write(MII_DBG_DATA, data);
}

static bool atl1c_phy_init(void)
{
    constexpr uint8_t MII_BMCR             = 0x00;
    constexpr uint8_t MII_BMSR             = 0x01;
    constexpr uint8_t MII_ADVERTISE        = 0x04;
    constexpr uint16_t BMCR_RESET          = 0x8000;
    constexpr uint16_t BMCR_ANENABLE       = 0x1000;
    constexpr uint16_t BMCR_ANRESTART      = 0x0200;
    constexpr uint16_t ADVERTISE_10HALF    = 0x0020;
    constexpr uint16_t ADVERTISE_10FULL    = 0x0040;
    constexpr uint16_t ADVERTISE_100HALF   = 0x0080;
    constexpr uint16_t ADVERTISE_100FULL   = 0x0100;
    constexpr uint16_t ADVERTISE_PAUSE_CAP = 0x0400;

    if (!atl1c_mdio_write(MII_BMCR, BMCR_RESET)) {
        return false;
    }

    for (uint32_t i = 0; i < ATL1C_MDIO_MAX_WAIT; i++) {
        uint16_t bmcr = 0;
        if (atl1c_mdio_read(MII_BMCR, &bmcr) && (bmcr & BMCR_RESET) == 0) {
            break;
        }
    }

    constexpr uint16_t adv = ADVERTISE_10HALF | ADVERTISE_10FULL | ADVERTISE_100HALF | ADVERTISE_100FULL |
        ADVERTISE_PAUSE_CAP;
    if (!atl1c_mdio_write(MII_ADVERTISE, adv)) {
        return false;
    }

    if (!atl1c_mdio_write(MII_BMCR, (uint16_t)(BMCR_ANENABLE | BMCR_ANRESTART))) {
        return false;
    }

    uint16_t bmsr = 0;
    if (atl1c_mdio_read(MII_BMSR, &bmsr)) {
        constexpr uint16_t BMSR_LSTATUS = 0x0004;
        boot_message(INFO, "[ATL1C] PHY link: %s", (bmsr & BMSR_LSTATUS) ? "up" : "down");
    }

    return true;
}

static bool atl1c_enable_msi(const struct pci_device device, const uint8_t vector)
{
    constexpr uint8_t PCI_STATUS_OFFSET  = 0x06;
    constexpr uint8_t PCI_CAP_PTR_OFFSET = 0x34;

    const uint16_t status = pci_config_read_word(device.bus, device.slot, device.function, PCI_STATUS_OFFSET);
    if ((status & PCI_STATUS_CAPABILITIES_LIST) == 0) {
        return false;
    }

    uint8_t cap_ptr = (uint8_t)(pci_config_read_word(device.bus,
                                                     device.slot,
                                                     device.function,
                                                     PCI_CAP_PTR_OFFSET) & 0xFFu);
    for (uint8_t i = 0; cap_ptr != 0 && i < 48; i++) {
        const uint16_t cap_hdr = pci_config_read_word(device.bus, device.slot, device.function, cap_ptr);
        const uint8_t cap_id   = (uint8_t)(cap_hdr & 0xFFu);
        const uint8_t next_ptr = (uint8_t)((cap_hdr >> 8) & 0xFFu);

        if (cap_id == 0x05) {
            const uint16_t msg_ctrl = pci_config_read_word(device.bus,
                                                           device.slot,
                                                           device.function,
                                                           (uint8_t)(cap_ptr + 2));
            const bool is_64bit = (msg_ctrl & (1u << 7)) != 0;

            const uint32_t lapic_id = apic_get_lapic_id();
            const uint32_t msg_addr = 0xFEE00000u | (lapic_id << 12);
            const uint16_t msg_data = vector;

            pci_config_write_word(device.bus,
                                  device.slot,
                                  device.function,
                                  (uint8_t)(cap_ptr + 4),
                                  (uint16_t)(msg_addr & 0xFFFFu));
            pci_config_write_word(device.bus,
                                  device.slot,
                                  device.function,
                                  (uint8_t)(cap_ptr + 6),
                                  (uint16_t)((msg_addr >> 16) & 0xFFFFu));

            uint8_t data_off = (uint8_t)(cap_ptr + 8);
            if (is_64bit) {
                pci_config_write_word(device.bus, device.slot, device.function, (uint8_t)(cap_ptr + 8), 0);
                pci_config_write_word(device.bus, device.slot, device.function, (uint8_t)(cap_ptr + 10), 0);
                data_off = (uint8_t)(cap_ptr + 12);
            }

            pci_config_write_word(device.bus, device.slot, device.function, data_off, msg_data);
            pci_config_write_word(device.bus,
                                  device.slot,
                                  device.function,
                                  (uint8_t)(cap_ptr + 2),
                                  (uint16_t)(msg_ctrl | 0x1u));

            atl1c_msi_enabled = true;
            return true;
        }

        if (next_ptr == cap_ptr || next_ptr < 0x40) {
            break;
        }
        cap_ptr = next_ptr;
    }

    return false;
}

static uint32_t atl1c_pci_read_dword(const struct pci_device device, const uint8_t offset)
{
    const uint16_t lo = pci_config_read_word(device.bus, device.slot, device.function, offset);
    const uint16_t hi = pci_config_read_word(device.bus, device.slot, device.function, (uint8_t)(offset + 2));
    return (uint32_t)lo | ((uint32_t)hi << 16);
}

static bool atl1c_get_bar_phys(const struct pci_device device, const uint8_t bir, uint64_t *out_phys)
{
    if (!out_phys || bir >= 6) {
        return false;
    }

    const uint32_t bar = device.bars[bir];
    if (bar == 0 || (bar & PCI_BAR_IO)) {
        return false;
    }

    const uint8_t mem_type = (bar >> 1) & 0x3;
    if (mem_type == PCI_BAR_MEMORY_TYPE_64) {
        if (bir + 1 >= 6) {
            return false;
        }
        const uint32_t bar_hi = device.bars[bir + 1];
        *out_phys             = ((uint64_t)bar_hi << 32) | (bar & ~0xFULL);
        return *out_phys != 0;
    }

    *out_phys = bar & ~0xFULL;
    return *out_phys != 0;
}

static bool atl1c_enable_msix(const struct pci_device device, const uint8_t vector)
{
    constexpr uint8_t PCI_STATUS_OFFSET  = 0x06;
    constexpr uint8_t PCI_CAP_PTR_OFFSET = 0x34;

    const uint16_t status = pci_config_read_word(device.bus, device.slot, device.function, PCI_STATUS_OFFSET);
    if ((status & PCI_STATUS_CAPABILITIES_LIST) == 0) {
        return false;
    }

    uint8_t cap_ptr = (uint8_t)(pci_config_read_word(device.bus,
                                                     device.slot,
                                                     device.function,
                                                     PCI_CAP_PTR_OFFSET) & 0xFFu);
    for (uint8_t i = 0; cap_ptr != 0 && i < 48; i++) {
        const uint16_t cap_hdr = pci_config_read_word(device.bus, device.slot, device.function, cap_ptr);
        const uint8_t cap_id   = (uint8_t)(cap_hdr & 0xFFu);
        const uint8_t next_ptr = (uint8_t)((cap_hdr >> 8) & 0xFFu);

        if (cap_id == 0x11) {
            const uint16_t msg_ctrl = pci_config_read_word(device.bus,
                                                           device.slot,
                                                           device.function,
                                                           (uint8_t)(cap_ptr + 2));
            const uint16_t table_size   = (uint16_t)((msg_ctrl & 0x7FFu) + 1u);
            const uint32_t table        = atl1c_pci_read_dword(device, (uint8_t)(cap_ptr + 4));
            const uint32_t pba          = atl1c_pci_read_dword(device, (uint8_t)(cap_ptr + 8));
            const uint8_t bir           = (uint8_t)(table & 0x7u);
            const uint32_t table_offset = table & ~0x7u;
            const uint32_t pba_offset   = pba & ~0x7u;

            uint64_t bar_phys = 0;
            if (!atl1c_get_bar_phys(device, bir, &bar_phys)) {
                boot_message(WARNING, "[ATL1C] MSI-X BAR%u invalid", bir);
                return false;
            }

            boot_message(INFO,
                         "[ATL1C] MSI-X cap=0x%02x ctrl=0x%04x size=%u table=0x%08x pba=0x%08x bir=%u bar=0x%016lx",
                         cap_ptr,
                         msg_ctrl,
                         table_size,
                         table,
                         pba,
                         bir,
                         (unsigned long)bar_phys);

            const uint32_t lapic_id = apic_get_lapic_id();
            const uint32_t msg_addr = 0xFEE00000u | (lapic_id << 12);
            const uint32_t msg_data = vector;

            uint16_t ctrl_mask = (uint16_t)(msg_ctrl | (1u << 14));
            pci_config_write_word(device.bus, device.slot, device.function, (uint8_t)(cap_ptr + 2), ctrl_mask);

            auto entry0                = (volatile uint32_t *)((uintptr_t)bar_phys + g_hhdm_offset + table_offset);
            const uint32_t old_addr_lo = entry0[0];
            const uint32_t old_addr_hi = entry0[1];
            const uint32_t old_data    = entry0[2];
            const uint32_t old_ctrl    = entry0[3];

            for (uint16_t vec = 0; vec < table_size; vec++) {
                auto entry = (volatile uint32_t *)((uintptr_t)bar_phys + g_hhdm_offset +
                    table_offset + (uint32_t)vec * 16u);
                entry[0] = msg_addr;
                entry[1] = 0;
                entry[2] = msg_data;
                entry[3] = 0;
            }

            auto entry = (volatile uint32_t *)((uintptr_t)bar_phys + g_hhdm_offset + table_offset);

            const uint32_t new_addr_lo = entry[0];
            const uint32_t new_addr_hi = entry[1];
            const uint32_t new_data    = entry[2];
            const uint32_t new_ctrl    = entry[3];

            boot_message(INFO,
                         "[ATL1C] MSI-X entry0 old=[%08x %08x %08x %08x] new=[%08x %08x %08x %08x]",
                         old_addr_lo,
                         old_addr_hi,
                         old_data,
                         old_ctrl,
                         new_addr_lo,
                         new_addr_hi,
                         new_data,
                         new_ctrl);

            if (pba_offset != 0) {
                auto pba_words = (volatile uint32_t *)((uintptr_t)bar_phys + g_hhdm_offset + pba_offset);
                boot_message(INFO, "[ATL1C] MSI-X PBA[0]=0x%08x", pba_words[0]);
            }

            uint16_t ctrl = msg_ctrl;
            ctrl          &= ~(1u << 14);
            ctrl          |= (1u << 15);
            pci_config_write_word(device.bus, device.slot, device.function, (uint8_t)(cap_ptr + 2), ctrl);

            atl1c_msix_enabled = true;
            return true;
        }

        if (next_ptr == cap_ptr || next_ptr < 0x40) {
            break;
        }
        cap_ptr = next_ptr;
    }

    return false;
}

static void atl1c_config_vector_mapping(void)
{
    uint32_t tbl1 = 0;
    uint32_t tbl2 = 0;
    uint32_t vec  = 0;

    if (atl1c_msix_enabled) {
        vec = 1;
    }

    tbl1 |= (vec << MSI_MAP_TBL1_RXQ0_SHIFT);
    tbl1 |= (vec << MSI_MAP_TBL1_TXQ0_SHIFT);

    atl1c_write32(REG_MSI_MAP_TBL1, tbl1);
    atl1c_write32(REG_MSI_MAP_TBL2, tbl2);
    atl1c_write32(REG_MSI_ID_MAP, 0);
}

static void atl1c_config_msi_retrans_timer(void)
{
    constexpr uint32_t MSI_RETRANS_DEFAULT = 100;
    uint32_t val                           = 0;

    if (atl1c_msix_enabled) {
        val = (MSI_RETRANS_DEFAULT & MSI_RETRANS_TM_MASK) << MSI_RETRANS_TM_SHIFT;
    } else if (atl1c_msi_enabled) {
        val = ((MSI_RETRANS_DEFAULT & MSI_RETRANS_TM_MASK) << MSI_RETRANS_TM_SHIFT) | MSI_MASK_SEL_LINE;
    }

    atl1c_write32(REG_MSI_RETRANS_TIMER, val);
}

static void atl1c_restart_autoneg(void)
{
    constexpr uint8_t MII_BMCR        = 0x00;
    constexpr uint16_t BMCR_ANENABLE  = 0x1000;
    constexpr uint16_t BMCR_ANRESTART = 0x0200;
    (void)atl1c_mdio_write(MII_BMCR, (uint16_t)(BMCR_ANENABLE | BMCR_ANRESTART));
}

static bool atl1c_wait_for_link_up(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        constexpr uint8_t MII_BMSR = 0x01;
        uint16_t bmsr              = 0;
        if (atl1c_mdio_read(MII_BMSR, &bmsr) && atl1c_mdio_read(MII_BMSR, &bmsr)) {
            constexpr uint16_t BMSR_LSTATUS = 0x0004;
            if (bmsr & BMSR_LSTATUS) {
                return true;
            }
        }
        tsc_sleep_ms(100);
        elapsed += 100;
    }
    return false;
}

static void atl1c_reset_pcie(const struct pci_device device)
{
    constexpr uint8_t PCI_COMMAND_OFFSET  = 0x04;
    constexpr uint8_t PCI_STATUS_OFFSET   = 0x06;
    constexpr uint8_t PCI_REVISION_OFFSET = 0x08;

    uint16_t cmd = pci_config_read_word(device.bus, device.slot, device.function, PCI_COMMAND_OFFSET);
    cmd          |= (PCI_COMMAND_MEMORY | PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER);
    cmd          &= ~PCI_COMMAND_INTERRUPT_DISABLE;
    pci_config_write_word(device.bus, device.slot, device.function, PCI_COMMAND_OFFSET, cmd);

    const uint16_t status = pci_config_read_word(device.bus, device.slot, device.function, PCI_STATUS_OFFSET);
    if (status & PCI_STATUS_CAPABILITIES_LIST) {
        constexpr uint8_t PCI_CAP_PTR_OFFSET = 0x34;
        uint8_t cap_ptr                      = (uint8_t)(pci_config_read_word(device.bus,
                                                                              device.slot,
                                                                              device.function,
                                                                              PCI_CAP_PTR_OFFSET) & 0xFFu);
        for (uint8_t i = 0; cap_ptr != 0 && i < 48; i++) {
            const uint16_t cap_hdr = pci_config_read_word(device.bus, device.slot, device.function, cap_ptr);
            const uint8_t cap_id   = (uint8_t)(cap_hdr & 0xFFu);
            const uint8_t next_ptr = (uint8_t)((cap_hdr >> 8) & 0xFFu);

            if (cap_id == 0x05) {
                const uint16_t ctrl = pci_config_read_word(device.bus,
                                                           device.slot,
                                                           device.function,
                                                           (uint8_t)(cap_ptr + 2));
                pci_config_write_word(device.bus,
                                      device.slot,
                                      device.function,
                                      (uint8_t)(cap_ptr + 2),
                                      (uint16_t)(ctrl & ~0x1u));
            } else if (cap_id == 0x11) {
                const uint16_t ctrl = pci_config_read_word(device.bus,
                                                           device.slot,
                                                           device.function,
                                                           (uint8_t)(cap_ptr + 2));
                pci_config_write_word(device.bus,
                                      device.slot,
                                      device.function,
                                      (uint8_t)(cap_ptr + 2),
                                      (uint16_t)(ctrl & ~(1u << 15)));
            }

            if (next_ptr == cap_ptr || next_ptr < 0x40) {
                break;
            }
            cap_ptr = next_ptr;
        }
    }

    const uint16_t rev_prog = pci_config_read_word(device.bus, device.slot, device.function, PCI_REVISION_OFFSET);
    const uint8_t revision  = (uint8_t)(rev_prog & 0xFF);
    const uint8_t rev       = (uint8_t)(revision >> 3);
    const bool rev_a        = (rev == 0 || rev == 1);
    const bool with_cr      = (revision & 0x1) != 0;

    uint32_t master = atl1c_read32(REG_MASTER_CTRL);
    if (rev_a && with_cr) {
        master |= (MASTER_CTRL_PCLKSEL_SRDS | MASTER_CTRL_WAKEN_25M);
    } else {
        master &= ~MASTER_CTRL_PCLKSEL_SRDS;
        master |= MASTER_CTRL_WAKEN_25M;
    }
    atl1c_write32(REG_MASTER_CTRL, master);
    tsc_sleep_ms(1);

    atl1c_write32(REG_WOL_CTRL, 0);

    uint32_t pdll = atl1c_read32(REG_PDLL_TRNS1);
    atl1c_write32(REG_PDLL_TRNS1, pdll & ~PDLL_TRNS1_D3PLLOFF_EN);

    uint32_t ue = atl1c_read32(REG_UE_SVRT);
    ue          &= ~(UE_SVRT_DLPROTERR | UE_SVRT_FCPROTERR);
    atl1c_write32(REG_UE_SVRT, ue);

    uint32_t pmctrl = atl1c_read32(REG_PMCTRL);
    pmctrl          &= ~(PMCTRL_L0S_EN | PMCTRL_L1_EN | PMCTRL_ASPM_FCEN);
    atl1c_write32(REG_PMCTRL, pmctrl);

    atl1c_reset_osc(revision);
    uint32_t serdes = atl1c_read32(REG_SERDES);
    serdes          |= SERDES_MACCLK_SLWDWN | SERDES_PHYCLK_SLWDWN;
    atl1c_write32(REG_SERDES, serdes);
}

static uint8_t atl1c_get_revision(const struct pci_device device)
{
    constexpr uint8_t PCI_REVISION_OFFSET = 0x08;
    const uint16_t rev_prog = pci_config_read_word(device.bus, device.slot, device.function, PCI_REVISION_OFFSET);
    return (uint8_t)(rev_prog & 0xFF);
}

static void atl1c_reset_osc(const uint8_t revision)
{
    const uint8_t rev = (uint8_t)(revision >> 3);
    const bool rev_a  = (rev == 0 || rev == 1);

    uint32_t val = atl1c_read32(REG_MISC3);
    atl1c_write32(REG_MISC3, (val & ~MISC3_25M_BY_SW) | MISC3_25M_NOTO_INTNL);

    val = atl1c_read32(REG_MISC);
    if (rev >= 2) {
        val &= ~MISC_INTNLOSC_OPEN;
        atl1c_write32(REG_MISC, val);
        atl1c_write32(REG_MISC, val | MISC_INTNLOSC_OPEN);

        uint32_t msic2 = atl1c_read32(REG_MSIC2);
        msic2          &= ~MSIC2_CALB_START;
        atl1c_write32(REG_MSIC2, msic2);
        atl1c_write32(REG_MSIC2, msic2 | MSIC2_CALB_START);
    } else {
        val &= ~MISC_INTNLOSC_OPEN;
        if (rev_a) {
            val &= ~MISC_ISO_EN;
        }
        atl1c_write32(REG_MISC, val | MISC_INTNLOSC_OPEN);
        atl1c_write32(REG_MISC, val);
    }

    tsc_sleep_ms(1);
}

static void atl1c_stop_mac(void)
{
    uint32_t rxq = atl1c_read32(REG_RXQ_CTRL);
    uint32_t txq = atl1c_read32(REG_TXQ_CTRL);
    atl1c_write32(REG_RXQ_CTRL, rxq & ~RXQ_CTRL_EN);
    atl1c_write32(REG_TXQ_CTRL, txq & ~TXQ_CTRL_EN);
    tsc_sleep_ms(1);

    uint32_t mac_address = atl1c_read32(REG_MAC_CTRL);
    mac_address          &= ~(MAC_CTRL_RX_EN | MAC_CTRL_TX_EN);
    atl1c_write32(REG_MAC_CTRL, mac_address);
}

static bool atl1c_reset_mac(const struct pci_device device)
{
    atl1c_write32(REG_IMR, 0);
    atl1c_write32(REG_ISR, 0xFFFFFFFF);
    atl1c_stop_mac();

    atl1c_write32(REG_MB_RFD0_PROD_IDX, 1);

    uint32_t master = atl1c_read32(REG_MASTER_CTRL);
    atl1c_write32(REG_MASTER_CTRL, master | MASTER_CTRL_SOFT_RST | MASTER_CTRL_OOB_DIS);
    tsc_sleep_ms(1);
    bool reset_done = false;
    for (uint32_t i = 0; i < 50; i++) {
        const uint32_t val = atl1c_read32(REG_MASTER_CTRL);
        if ((val & MASTER_CTRL_SOFT_RST) == 0) {
            reset_done = true;
            break;
        }
        tsc_sleep_ms(1);
    }
    const uint8_t revision = atl1c_get_revision(device);
    atl1c_reset_osc(revision);

    uint32_t serdes = atl1c_read32(REG_SERDES);
    serdes          |= SERDES_MACCLK_SLWDWN | SERDES_PHYCLK_SLWDWN;
    atl1c_write32(REG_SERDES, serdes);

    return reset_done;
}

static void atl1c_reset_phy(void)
{
    constexpr uint8_t MII_IER        = 0x12;
    constexpr uint16_t IER_LINK_UP   = 0x0400;
    constexpr uint16_t IER_LINK_DOWN = 0x0800;

    constexpr uint16_t DBG_ANACTRL     = 0x00;
    constexpr uint16_t DBG_SYSMODCTRL  = 0x04;
    constexpr uint16_t DBG_SRDSYSMOD   = 0x05;
    constexpr uint16_t DBG_TST10BTCFG  = 0x12;
    constexpr uint16_t DBG_AZ_ANADECT  = 0x15;
    constexpr uint16_t DBG_LEGCYPS     = 0x29;
    constexpr uint16_t DBG_TST100BTCFG = 0x36;
    constexpr uint16_t DBG_GREENCFG2   = 0x3D;

    constexpr uint16_t ANACTRL_DEF     = 0x02EF;
    constexpr uint16_t SYSMODCTRL_DEF  = 0xBB8B;
    constexpr uint16_t SRDSYSMOD_DEF   = 0x2C46;
    constexpr uint16_t TST10BTCFG_DEF  = 0x4C04;
    constexpr uint16_t AZ_ANADECT_DEF  = 0x3220;
    constexpr uint16_t LEGCYPS_DEF     = 0x129D;
    constexpr uint16_t TST100BTCFG_DEF = 0xE12C;

    uint32_t val = atl1c_read32(REG_PHY_CTRL);
    val          &= ~(PHY_CTRL_DSPRST_OUT | PHY_CTRL_IDDQ | PHY_CTRL_GATE_25M | PHY_CTRL_POWER_DOWN | PHY_CTRL_CLS);
    val          |= PHY_CTRL_RST_ANALOG | PHY_CTRL_HIB_PULSE | PHY_CTRL_HIB_EN | PHY_CTRL_CLS;
    atl1c_write32(REG_PHY_CTRL, val);
    tsc_sleep_ms(1);
    atl1c_write32(REG_PHY_CTRL, val | PHY_CTRL_DSPRST_OUT);
    tsc_sleep_ms(1);

    atl1c_mdio_write_dbg(DBG_LEGCYPS, LEGCYPS_DEF);
    atl1c_mdio_write_dbg(DBG_SYSMODCTRL, SYSMODCTRL_DEF);
    atl1c_mdio_write_dbg(DBG_SRDSYSMOD, SRDSYSMOD_DEF);
    atl1c_mdio_write_dbg(DBG_TST10BTCFG, TST10BTCFG_DEF);
    atl1c_mdio_write_dbg(DBG_TST100BTCFG, TST100BTCFG_DEF);
    atl1c_mdio_write_dbg(DBG_ANACTRL, ANACTRL_DEF);
    atl1c_mdio_write_dbg(DBG_AZ_ANADECT, AZ_ANADECT_DEF);

    uint16_t greencfg2 = 0;
    if (atl1c_mdio_read_dbg(DBG_GREENCFG2, &greencfg2)) {
        constexpr uint16_t GREENCFG2_GATE_DFSE_EN = 0x0080;
        atl1c_mdio_write_dbg(DBG_GREENCFG2, (uint16_t)(greencfg2 & ~GREENCFG2_GATE_DFSE_EN));
    }

    atl1c_mdio_write(MII_IER, (uint16_t)(IER_LINK_UP | IER_LINK_DOWN));
}

static int eth_dev_ioctl([[maybe_unused]] vfs_inode_t *node, int request, void *arg);

static struct inode_operations eth_dev_ops = {
    .ioctl = eth_dev_ioctl,
};

static void atl1c_wait_for_network(void)
{
    boot_message(INFO, "Waiting for DHCP offer...");
    uint32_t last_rx      = 0;
    uint32_t last_updates = 0;
    uint32_t last_tx      = 0;
    uint32_t last_irq     = 0;
    uint32_t budget       = wait_for_network_timeout;
    while (!network_is_ready() && budget-- > 0) {
        tsc_sleep_ms(1);
        if ((budget % 500) == 0 && !atl1c_link_up) {
            atl1c_link_up = atl1c_wait_for_link_up(500);
            if (!atl1c_link_up) {
                atl1c_restart_autoneg();
            }
        }
        if (atl1c_link_up && !atl1c_dhcp_started) {
            dhcp_send_discover(mac);
            atl1c_dhcp_started = true;
        }
        if (!atl1c_irq_kick_done && atl1c_dhcp_started && atl1c_irq_hits == 0) {
            const uint32_t status = atl1c_read32(REG_ISR);
            if (status != 0 && (status & ISR_DIS) == 0) {
                atl1c_irq_kick_done = true;
                boot_message(INFO, "[ATL1C] irq kick status=0x%08x", status);
                atl1c_handle_isr(status, false);
            }
        }
        if ((budget % 1000) == 0) {
            if (rx_packets != last_rx || rx_rrd_updates != last_updates || tx_packets != last_tx ||
                atl1c_irq_hits != last_irq) {
                const uint32_t tx_pidx  = atl1c_read32(REG_TPD_PRI0_PIDX);
                const uint32_t tx_cidx  = atl1c_read32(REG_TPD_PRI0_CIDX);
                const uint32_t rfd_cons = atl1c_read32(REG_MB_RFD01_CONS_IDX);
                const uint32_t isr      = atl1c_read32(REG_ISR);
                const uint32_t imr      = atl1c_read32(REG_IMR);
                boot_message(INFO,
                             "[ATL1C] rx=%u rrd=%u tx=%u irq=%u isr=0x%08x imr=0x%08x rrd_cur=%u rfd_prod=%u tx_pidx=%u tx_cidx=%u rfd_cons=0x%08x",
                             rx_packets,
                             rx_rrd_updates,
                             tx_packets,
                             atl1c_irq_hits,
                             isr,
                             imr,
                             rrd_cur,
                             rfd_prod,
                             tx_pidx & 0xFFFFu,
                             tx_cidx & 0xFFFFu,
                             rfd_cons);
                last_rx      = rx_packets;
                last_updates = rx_rrd_updates;
                last_tx      = tx_packets;
                last_irq     = atl1c_irq_hits;
            }
        }
    }

    if (!network_is_ready()) {
        boot_message(ERROR, "Network failed to start");
    }
}

static int eth_dev_ioctl([[maybe_unused]] vfs_inode_t *node, const int request, void *arg)
{
    constexpr int GETNETINFO = 0x4090;
    if (request == GETNETINFO) {
        if (!arg)
            return -1;

        auto netinfo = (struct netinfo *)arg;
        memset(netinfo, 0, sizeof(*netinfo));
        memcpy(netinfo->mac, mac, 6);

        const uint32_t *ip              = (uint32_t *)network_get_my_ip_address();
        const uint32_t *mask            = (uint32_t *)network_get_subnet_mask();
        const uint32_t *gateway         = (uint32_t *)network_get_default_gateway();
        const uint32_t *dns_servers     = network_get_dns_servers();
        const uint32_t dns_server_count = network_get_dns_server_count();

        if (ip)
            netinfo->ip = *ip;
        if (mask)
            netinfo->subnet_mask = *mask;
        if (gateway)
            netinfo->default_gateway = *gateway;

        if (dns_servers && dns_server_count >= 1) {
            uint8_t dns_server[4];
            ip_to_bytes(*dns_servers, dns_server);
            netinfo->dns_server = *(uint32_t *)dns_server;
        }

        return 0;
    }
    return -1;
}

static inline uint32_t atl1c_read32(const uint32_t reg)
{
    return mmio_read32(mmio_base + reg);
}

static inline void atl1c_write32(const uint32_t reg, const uint32_t value)
{
    mmio_write32(mmio_base + reg, value);
}

static bool atl1c_map_bar(const struct pci_device device)
{
    const uint32_t bar0 = device.bars[0];
    if (bar0 == 0 || (bar0 & PCI_BAR_IO)) {
        return false;
    }

    uint64_t base          = 0;
    const uint8_t mem_type = (bar0 >> 1) & 0x3;
    if (mem_type == PCI_BAR_MEMORY_TYPE_64) {
        const uint32_t bar1 = device.bars[1];
        base                = ((uint64_t)bar1 << 32) | (bar0 & ~0xFULL);
    } else {
        base = bar0 & ~0xFULL;
    }

    if (base == 0) {
        return false;
    }

    mmio_base = base + g_hhdm_offset;
    return true;
}

static void atl1c_reset(void)
{
    uint32_t master = atl1c_read32(REG_MASTER_CTRL);
    master          |= MASTER_CTRL_OOB_DIS;
    atl1c_write32(REG_MASTER_CTRL, master | MASTER_CTRL_SOFT_RST);
    tsc_sleep_ms(ATL1C_RESET_DELAY_MS);
    atl1c_write32(REG_MASTER_CTRL, master);

    const uint32_t mac_ctrl = atl1c_read32(REG_MAC_CTRL);
    atl1c_write32(REG_MAC_CTRL, mac_ctrl | MAC_CTRL_WOLSPED_SWEN);

    atl1c_write32(REG_WOL_CTRL, 0);
    atl1c_write32(REG_IMR, 0);
    atl1c_write32(REG_ISR, 0xFFFFFFFF);
}

static bool atl1c_setup_rings(void)
{
    uintptr_t rfd_phys = 0;
    uintptr_t rrd_phys = 0;
    uintptr_t tpd_phys = 0;
    void *rfd_virt     = nullptr;
    void *rrd_virt     = nullptr;
    void *tpd_virt     = nullptr;

    if (!dma_alloc_pages(PAGE_SIZE, PAGE_SIZE, 0, &rfd_phys, &rfd_virt) ||
        !dma_alloc_pages(PAGE_SIZE, PAGE_SIZE, 0, &rrd_phys, &rrd_virt) ||
        !dma_alloc_pages(PAGE_SIZE, PAGE_SIZE, 0, &tpd_phys, &tpd_virt)) {
        return false;
    }

    rfd_ring = (struct atl1c_rx_free_desc *)rfd_virt;
    rrd_ring = (struct atl1c_recv_ret_status *)rrd_virt;
    tpd_ring = (struct atl1c_tpd_desc *)tpd_virt;

    memset(rfd_ring, 0, PAGE_SIZE);
    memset(rrd_ring, 0, PAGE_SIZE);
    memset(tpd_ring, 0, PAGE_SIZE);

    for (uint16_t i = 0; i < ATL1C_RX_RING_SIZE; i++) {
        uintptr_t buf_phys = 0;
        void *buf_virt     = nullptr;
        if (!dma_alloc_pages(PAGE_SIZE, PAGE_SIZE, 0, &buf_phys, &buf_virt)) {
            return false;
        }
        rx_buffers[i] = (uint8_t *)buf_virt;
        rx_phys[i]    = buf_phys;
        memset(rx_buffers[i], 0, PAGE_SIZE);
        rfd_ring[i].buffer_addr = rx_phys[i];
    }

    for (uint16_t i = 0; i < ATL1C_TX_RING_SIZE; i++) {
        uintptr_t buf_phys = 0;
        void *buf_virt     = nullptr;
        if (!dma_alloc_pages(PAGE_SIZE, PAGE_SIZE, 0, &buf_phys, &buf_virt)) {
            return false;
        }
        tx_buffers[i] = (uint8_t *)buf_virt;
        tx_phys[i]    = buf_phys;
        memset(tx_buffers[i], 0, PAGE_SIZE);
    }

    if ((rfd_phys >> 32) != 0 || (rrd_phys >> 32) != 0 || (tpd_phys >> 32) != 0) {
        boot_message(ERROR, "[ATL1C] DMA buffers above 4GB not supported");
        return false;
    }

    atl1c_write32(REG_RX_BASE_ADDR_HI, 0);
    atl1c_write32(REG_TX_BASE_ADDR_HI, 0);

    atl1c_write32(REG_RFD0_HEAD_ADDR_LO, (uint32_t)(rfd_phys & 0xFFFFFFFFu));
    atl1c_write32(REG_RRD0_HEAD_ADDR_LO, (uint32_t)(rrd_phys & 0xFFFFFFFFu));
    atl1c_write32(REG_TPD_PRI0_ADDR_LO, (uint32_t)(tpd_phys & 0xFFFFFFFFu));

    atl1c_write32(REG_RFD_RING_SIZE, ATL1C_RX_RING_SIZE);
    atl1c_write32(REG_RRD_RING_SIZE, ATL1C_RX_RING_SIZE);
    atl1c_write32(REG_TPD_RING_SIZE, ATL1C_TX_RING_SIZE);
    atl1c_write32(REG_RX_BUF_SIZE, ATL1C_RX_BUF_SIZE);

    rrd_cur  = 0;
    tpd_cur  = 0;
    rfd_prod = ATL1C_RX_RING_SIZE - 1;

    atl1c_write32(REG_TPD_PRI0_PIDX, tpd_cur);
    atl1c_mb();
    atl1c_write32(REG_TPD_PRI0_CIDX, 0);
    atl1c_write32(REG_MB_RFD01_CONS_IDX, 0);
    atl1c_write32(REG_MB_RFD0_PROD_IDX, rfd_prod);

    return true;
}

static void atl1c_configure_mac(void)
{
    atl1c_write32(REG_CLK_GATE_CTRL, CLK_GATE_ALL);

    atl1c_write32(REG_ISR, 0xFFFFFFFF);
    atl1c_write32(REG_WOL_CTRL, 0);
    atl1c_write32(REG_INT_RETRIG_TIMER, 20000);

    constexpr uint32_t irq_mod = ((1000u & IRQ_MODRT_TIMER_MASK) << IRQ_MODRT_TX_TIMER_SHIFT) |
        ((1000u & IRQ_MODRT_TIMER_MASK) << IRQ_MODRT_RX_TIMER_SHIFT);
    atl1c_write32(REG_IRQ_MODRT_TIMER_INIT, irq_mod);

    constexpr uint32_t master_ctrl = MASTER_CTRL_TX_ITIMER_EN | MASTER_CTRL_RX_ITIMER_EN |
        MASTER_CTRL_SA_TIMER_EN | MASTER_CTRL_INT_RDCLR;
    atl1c_write32(REG_MASTER_CTRL, master_ctrl);
    atl1c_write32(REG_SMB_STAT_TIMER, 0);
    atl1c_write32(REG_TINT_TPD_THRSHLD, 1);
    atl1c_write32(REG_TINT_TIMER, 1000);

    atl1c_write32(REG_MTU, 1518);

    uint32_t raw_mtu = 1518;
    uint32_t txq1    = (raw_mtu + 7u) >> 3;
    if (raw_mtu >= TXQ1_JUMBO_TSO_TH) {
        txq1 = TXQ1_JUMBO_TSO_TH >> 3;
    }
    atl1c_write32(REG_TXQ1_CTRL, txq1 | TXQ1_ERRLGPKT_DROP_EN);

    uint32_t txq_ctrl = (TXQ_TPD_BURST_PREF_DEF & TXQ_NUM_TPD_BURST_MASK) << TXQ_NUM_TPD_BURST_SHIFT;
    txq_ctrl          |= (TXQ_TXF_BURST_PREF_DEF << TXQ_TXF_BURST_NUM_SHIFT);
    txq_ctrl          |= TXQ_CTRL_ENH_MODE | TXQ_CTRL_LS_8023_EN | TXQ_CTRL_IP_OPTION_EN;
    atl1c_write32(REG_TXQ_CTRL, txq_ctrl);

    constexpr uint32_t rxq2 = (MTU_STD_ALGN >> 3) << RXQ2_RXF_XOFF_THRESH_SHIFT |
        (MTU_STD_ALGN >> 3) << RXQ2_RXF_XON_THRESH_SHIFT;
    atl1c_write32(REG_RXQ2_CTRL, rxq2);

    uint32_t rxq_ctrl = (RXQ_RFD_BURST_NUM_DEF << RXQ_RFD_BURST_NUM_SHIFT) |
        (RXQ_RSS_MODE_DIS << RXQ_RSS_MODE_SHIFT) |
        (RXQ_IDT_TBL_SIZE_DEF << RXQ_IDT_TBL_SIZE_SHIFT) |
        RXQ_RSS_HSTYP_ALL |
        RXQ_RSS_HASH_EN |
        RXQ_IPV6_PARSE_EN;
    atl1c_write32(REG_RXQ_CTRL, rxq_ctrl);

    uint32_t dma_ctrl = DMA_CTRL_RREQ_PRI_DATA |
        (DMA_CTRL_WDLY_CNT_DEF << DMA_CTRL_WDLY_CNT_SHIFT) |
        (DMA_CTRL_RDLY_CNT_DEF << DMA_CTRL_RDLY_CNT_SHIFT) |
        (4u << DMA_CTRL_RREQ_BLEN_SHIFT) |
        (DMA_CTRL_RORDER_MODE_OUT << DMA_CTRL_RORDER_MODE_SHIFT);
    atl1c_write32(REG_DMA_CTRL, dma_ctrl);
    atl1c_write32(REG_LOAD_PTR, 1);
}

static void atl1c_start_mac(void)
{
    uint32_t mac_ctrl = atl1c_read32(REG_MAC_CTRL);
    mac_ctrl          |= MAC_CTRL_MHASH_ALG_HI5B | MAC_CTRL_BRD_EN | MAC_CTRL_PCRCE | MAC_CTRL_CRCE |
        MAC_CTRL_FULLD | MAC_CTRL_RXFC_EN | MAC_CTRL_TXFC_EN | MAC_CTRL_RX_EN | MAC_CTRL_TX_EN;
    mac_ctrl &= ~(MAC_CTRL_SPEED_MASK << MAC_CTRL_SPEED_SHIFT);
    mac_ctrl |= (MAC_CTRL_SPEED_10_100 << MAC_CTRL_SPEED_SHIFT);
    atl1c_write32(REG_MAC_CTRL, mac_ctrl);

    uint32_t txq_ctrl = atl1c_read32(REG_TXQ_CTRL);
    uint32_t rxq_ctrl = atl1c_read32(REG_RXQ_CTRL);
    txq_ctrl          |= TXQ_CTRL_EN;
    rxq_ctrl          |= RXQ_CTRL_EN;
    atl1c_write32(REG_TXQ_CTRL, txq_ctrl);
    atl1c_write32(REG_RXQ_CTRL, rxq_ctrl);
}

static void atl1c_dump_regs(void)
{
    const uint32_t rxq          = atl1c_read32(REG_RXQ_CTRL);
    const uint32_t txq          = atl1c_read32(REG_TXQ_CTRL);
    const uint32_t dma          = atl1c_read32(REG_DMA_CTRL);
    const uint32_t mac_ctrl     = atl1c_read32(REG_MAC_CTRL);
    const uint32_t rfd_size     = atl1c_read32(REG_RFD_RING_SIZE);
    const uint32_t rrd_size     = atl1c_read32(REG_RRD_RING_SIZE);
    const uint32_t rfd_prod_reg = atl1c_read32(REG_MB_RFD0_PROD_IDX);
    const uint32_t rfd_cons_reg = atl1c_read32(REG_MB_RFD01_CONS_IDX);
    const uint32_t tx_pidx      = atl1c_read32(REG_TPD_PRI0_PIDX);
    const uint32_t tx_cidx      = atl1c_read32(REG_TPD_PRI0_CIDX);
    const uint32_t master       = atl1c_read32(REG_MASTER_CTRL);
    const uint32_t serdes       = atl1c_read32(REG_SERDES);
    const uint32_t pmctrl       = atl1c_read32(REG_PMCTRL);

    boot_message(INFO,
                 "[ATL1C] regs mac=0x%08x rxq=0x%08x txq=0x%08x dma=0x%08x",
                 mac_ctrl,
                 rxq,
                 txq,
                 dma);
    boot_message(INFO,
                 "[ATL1C] ctrl master=0x%08x serdes=0x%08x pmctrl=0x%08x",
                 master,
                 serdes,
                 pmctrl);
    boot_message(INFO,
                 "[ATL1C] rings rfd_size=%u rrd_size=%u rfd_prod=0x%08x rfd_cons=0x%08x tx_pidx=%u tx_cidx=%u",
                 rfd_size,
                 rrd_size,
                 rfd_prod_reg,
                 rfd_cons_reg,
                 tx_pidx & 0xFFFFu,
                 tx_cidx & 0xFFFFu);
}

static void atl1c_handle_isr(const uint32_t status, const bool from_irq)
{
    if (status == 0 || (status & ISR_DIS) != 0) {
        if (from_irq) {
            apic_send_eoi();
        }
        return;
    }

    atl1c_write32(REG_ISR, status | ISR_DIS);

    if (status & ISR_RX_Q0) {
        atl1c_receive();
    }

    // if (atl1c_irq_debug_left > 0)
    // {
    //     boot_message(INFO, "[ATL1C] irq status=0x%08x hits=%u", status, atl1c_irq_hits);
    //     atl1c_irq_debug_left--;
    // }

    atl1c_write32(REG_ISR, 0);
    atl1c_write32(REG_IMR, atl1c_intr_mask);

    if (from_irq) {
        apic_send_eoi();
    }
}

static void atl1c_enable_interrupts(void)
{
    atl1c_intr_mask = ISR_RX_Q0 | ISR_TX_Q0 | ISR_PHY | ISR_DMAW | ISR_DMAR | ISR_TXF_UR | ISR_RFD_UR | ISR_RXF_OV;
    atl1c_write32(REG_ISR, 0x7FFFFFFF);
    atl1c_write32(REG_IMR, atl1c_intr_mask);
}

static void atl1c_interrupt_handler(struct interrupt_frame *frame)
{
    (void)frame;
    atl1c_irq_hits++;
    atl1c_handle_isr(atl1c_read32(REG_ISR), true);
}

static bool atl1c_read_mac_address(void)
{
    const uint32_t addr_low  = atl1c_read32(REG_MAC_STA_ADDR);
    const uint32_t addr_high = atl1c_read32(REG_MAC_STA_ADDR + 4);

    const uint32_t low_be  = htonl(addr_low);
    const uint16_t high_be = htons((uint16_t)addr_high);

    memcpy(&mac[2], &low_be, sizeof(low_be));
    memcpy(&mac[0], &high_be, sizeof(high_be));

    uint8_t or_mask  = 0;
    uint8_t and_mask = 0xFF;
    for (size_t i = 0; i < sizeof(mac); i++) {
        or_mask  |= mac[i];
        and_mask &= mac[i];
    }

    return or_mask != 0 && and_mask != 0xFF;
}

static void atl1c_write_mac_address(void)
{
    const uint32_t low = ((uint32_t)mac[2] << 24) |
        ((uint32_t)mac[3] << 16) |
        ((uint32_t)mac[4] << 8) |
        (uint32_t)mac[5];
    const uint32_t high = ((uint32_t)mac[0] << 8) | (uint32_t)mac[1];

    atl1c_write32(REG_MAC_STA_ADDR, low);
    atl1c_write32(REG_MAC_STA_ADDR + 4, high);
}

void atl1c_init(struct pci_device device)
{
#ifdef TEST_MODE
    (void)device;
    boot_message(INFO, "[ATL1C] Skipping initialization in test mode");
    return;
#endif

    if (!atl1c_map_bar(device)) {
        boot_message(ERROR, "[ATL1C] No valid MMIO BAR");
        return;
    }

    pci_enable_bus_mastering(device);
    atl1c_reset_pcie(device);
    atl1c_reset();
    if (!atl1c_reset_mac(device)) {
        boot_message(WARNING, "[ATL1C] MAC reset timed out");
    }
    atl1c_reset_phy();

    if (!atl1c_read_mac_address()) {
        boot_message(ERROR, "[ATL1C] Failed to read MAC address");
        return;
    }

    network_set_mac(mac);
    boot_message(INFO, "[ATL1C] MAC Address: %s", get_mac_address_string(mac));
    atl1c_write_mac_address();

    if (!atl1c_phy_init()) {
        boot_message(WARNING, "[ATL1C] PHY init failed");
    }
    atl1c_link_up = atl1c_wait_for_link_up(10000);
    boot_message(INFO, "[ATL1C] Link wait: %s", atl1c_link_up ? "up" : "down");

    if (!atl1c_setup_rings()) {
        boot_message(ERROR, "[ATL1C] Failed to allocate rings");
        return;
    }

    atl1c_configure_mac();
    atl1c_start_mac();
    atl1c_dump_regs();

    atl1c_msix_enabled = false;
    atl1c_msi_enabled  = false;
    atl1c_irq_vector   = IRQ_BASE + device.irq;
    if (!atl1c_enable_msix(device, atl1c_irq_vector)) {
        if (!atl1c_enable_msi(device, atl1c_irq_vector)) {
            apic_enable_irq(device.irq, atl1c_irq_vector);
        }
    }
    atl1c_config_vector_mapping();
    atl1c_config_msi_retrans_timer();
    register_interrupt_handler(atl1c_irq_vector, atl1c_interrupt_handler);
    atl1c_enable_interrupts();

    spinlock_init(&atl1c_tx_lock);
    atl1c_initialized = true;
    network_register_driver(atl1c_send_packet);
    arp_init();
    atl1c_wait_for_network();

    vfs_inode_t *node = kmalloc(sizeof(vfs_inode_t));
    if (!node) {
        boot_message(ERROR, "[ATL1C] Failed to create device node");
        return;
    }

    memset(node, 0, sizeof(vfs_inode_t));
    node->flags = VFS_CHARDEVICE;
    node->iops  = &eth_dev_ops;
    devfs_register_device("eth0", node);
}

int atl1c_send_packet(const void *data, const uint16_t len)
{
    if (!atl1c_initialized) {
        if (tx_fail_debug_left > 0) {
            boot_message(WARNING, "[ATL1C] tx fail: driver not initialized");
            tx_fail_debug_left--;
        }
        return -1;
    }

    if (len == 0 || len > PAGE_SIZE) {
        if (tx_fail_debug_left > 0) {
            boot_message(WARNING, "[ATL1C] tx fail: invalid len=%u", len);
            tx_fail_debug_left--;
        }
        return -1;
    }

    // if (tx_debug_left > 0 && len >= sizeof(struct ether_header))
    // {
    //     const struct ether_header* eth = (const struct ether_header*)data;
    //     const uint16_t ether_type = ntohs(eth->ether_type);
    //     boot_message(INFO,
    //                  "[ATL1C] tx dst=%02x:%02x:%02x:%02x:%02x:%02x src=%02x:%02x:%02x:%02x:%02x:%02x type=0x%04x len=%u",
    //                  eth->dest_host[0], eth->dest_host[1], eth->dest_host[2],
    //                  eth->dest_host[3], eth->dest_host[4], eth->dest_host[5],
    //                  eth->src_host[0], eth->src_host[1], eth->src_host[2],
    //                  eth->src_host[3], eth->src_host[4], eth->src_host[5],
    //                  ether_type,
    //                  len);
    //     tx_debug_left--;
    // }

    uint64_t flags = 0;
    SPIN_LOCK_INT_SAVE(atl1c_tx_lock, flags);

    const uint16_t tx_cons = (uint16_t)(atl1c_read32(REG_TPD_PRI0_CIDX) & 0xFFFFu);
    const uint16_t next    = (uint16_t)((tpd_cur + 1) % ATL1C_TX_RING_SIZE);
    if (next == tx_cons) {
        if (tx_fail_debug_left > 0) {
            boot_message(WARNING,
                         "[ATL1C] tx fail: ring full pidx=%u cidx=%u",
                         tpd_cur,
                         tx_cons);
            tx_fail_debug_left--;
        }
        SPIN_UNLOCK_INT_RESTORE(atl1c_tx_lock, flags);
        return -1;
    }

    memcpy(tx_buffers[tpd_cur], data, len);

    tpd_ring[tpd_cur].word1       = 0;
    tpd_ring[tpd_cur].buffer_addr = tx_phys[tpd_cur];
    tpd_ring[tpd_cur].buffer_len  = (uint16_t)len;
    tpd_ring[tpd_cur].vlan_tag    = 0;
    tpd_ring[tpd_cur].word1       = (1u << TPD_EOP_SHIFT) | (1u << TPD_ETHTYPE_SHIFT);

    tpd_cur = next;
    tx_packets++;
    atl1c_mb();
    atl1c_write32(REG_TPD_PRI0_PIDX, tpd_cur);
    SPIN_UNLOCK_INT_RESTORE(atl1c_tx_lock, flags);

    return 0;
}

void atl1c_receive(void)
{
    if (!atl1c_initialized) {
        return;
    }

    while (rrd_ring[rrd_cur].word3 & RRS_RXD_UPDATED) {
        rx_rrd_updates++;
        const uint32_t word0     = rrd_ring[rrd_cur].word0;
        const uint32_t word3     = rrd_ring[rrd_cur].word3;
        const uint16_t rfd_count = (uint16_t)((word0 >> RRS_RX_RFD_CNT_SHIFT) & RRS_RX_RFD_CNT_MASK);
        if (rfd_count != 1) {
            rrd_ring[rrd_cur].word3 &= ~RRS_RXD_UPDATED;
            rrd_cur                 = (uint16_t)((rrd_cur + 1) % ATL1C_RX_RING_SIZE);
            continue;
        }

        const uint16_t rfd_index = (uint16_t)((word0 >> RRS_RX_RFD_INDEX_SHIFT) & RRS_RX_RFD_INDEX_MASK);
        if (rfd_index >= ATL1C_RX_RING_SIZE) {
            rrd_ring[rrd_cur].word3 &= ~RRS_RXD_UPDATED;
            rrd_cur                 = (uint16_t)((rrd_cur + 1) % ATL1C_RX_RING_SIZE);
            continue;
        }

        uint16_t length = (uint16_t)((word3 >> RRS_PKT_SIZE_SHIFT) & RRS_PKT_SIZE_MASK);
        if (length >= 4) {
            length -= 4;
        }
        if (length == 0 || length > ATL1C_RX_BUF_SIZE) {
            rrd_ring[rrd_cur].word3 &= ~RRS_RXD_UPDATED;
            rrd_cur                 = (uint16_t)((rrd_cur + 1) % ATL1C_RX_RING_SIZE);
            continue;
        }

        // if (rx_debug_left > 0 && length >= sizeof(struct ether_header))
        // {
        //     const struct ether_header* eth = (const struct ether_header*)rx_buffers[rfd_index];
        //     const uint16_t ether_type = ntohs(eth->ether_type);
        //     boot_message(INFO,
        //                  "[ATL1C] pkt dst=%02x:%02x:%02x:%02x:%02x:%02x src=%02x:%02x:%02x:%02x:%02x:%02x type=0x%04x len=%u",
        //                  eth->dest_host[0], eth->dest_host[1], eth->dest_host[2],
        //                  eth->dest_host[3], eth->dest_host[4], eth->dest_host[5],
        //                  eth->src_host[0], eth->src_host[1], eth->src_host[2],
        //                  eth->src_host[3], eth->src_host[4], eth->src_host[5],
        //                  ether_type,
        //                  length);
        //
        //     if (ether_type == ETHERTYPE_IP && length >= sizeof(struct ether_header) + sizeof(struct ipv4_header))
        //     {
        //         const struct ipv4_header* ip = (const struct ipv4_header*)(rx_buffers[rfd_index] + sizeof(*eth));
        //         boot_message(INFO,
        //                      "[ATL1C] ip proto=%u dst=%u.%u.%u.%u",
        //                      ip->protocol,
        //                      ip->dest_ip[0], ip->dest_ip[1], ip->dest_ip[2], ip->dest_ip[3]);
        //
        //         if (ip->protocol == IP_PROTOCOL_UDP &&
        //             length >= sizeof(struct ether_header) + sizeof(struct ipv4_header) + sizeof(struct udp_header))
        //         {
        //             const struct udp_header* udp = (const struct udp_header*)(rx_buffers[rfd_index] +
        //                 sizeof(struct ether_header) + sizeof(struct ipv4_header));
        //             boot_message(INFO,
        //                          "[ATL1C] udp src=%u dst=%u",
        //                          ntohs(udp->src_port),
        //                          ntohs(udp->dest_port));
        //         }
        //     }
        //
        //     rx_debug_left--;
        // }

        network_receive(rx_buffers[rfd_index], length);
        rx_packets++;

        rrd_ring[rrd_cur].word3 &= ~RRS_RXD_UPDATED;
        rrd_cur                 = (uint16_t)((rrd_cur + 1) % ATL1C_RX_RING_SIZE);

        rfd_prod = rfd_index;
        atl1c_mb();
        atl1c_write32(REG_MB_RFD0_PROD_IDX, rfd_prod);
    }
}

void atl1c_get_mac(uint8_t *mac_out)
{
    if (mac_out) {
        memcpy(mac_out, mac, sizeof(mac));
    }
}
