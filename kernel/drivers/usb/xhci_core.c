#include <drivers/usb/xhci.h>
#include <drivers/usb/ehci.h>
#include <drivers/terminal.h>
#include <drivers/tsc.h>
#include <mem/vmm.h>

#define XHCI_CAPLENGTH 0x00u
#define XHCI_HCSPARAMS1 0x04u
#define XHCI_OP_USBCMD 0x00u
#define XHCI_OP_USBSTS 0x04u
#define XHCI_MMIO_MAP_BYTES 0x100000u

#define XHCI_USBCMD_RS (1u << 0)
#define XHCI_USBCMD_HCRST (1u << 1)
#define XHCI_USBSTS_HCH (1u << 0)
#define XHCI_USBSTS_CNR (1u << 11)

#define XHCI_TIMEOUT_MS 200u
#define XHCI_RESET_TIMEOUT_MS 1000u

struct xhci_controller
{
    volatile uint8_t *mmio;
    volatile uint8_t *op_base;
    uint8_t cap_len;
};

static struct xhci_controller g_xhci;

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

static bool xhci_wait_for(const volatile uint8_t *base,
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

static inline pml4_t xhci_current_pml4(void)
{
    uint64_t cr3 = 0;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    return (pml4_t)(cr3 & 0x000FFFFFFFFFF000ull);
}

static void xhci_map_mmio_range(const uint64_t phys_base, const uint64_t bytes)
{
    const uint64_t start = phys_base & ~(uint64_t)(PAGE_SIZE - 1u);
    const uint64_t end   = (phys_base + bytes + PAGE_SIZE - 1u) & ~(uint64_t)(PAGE_SIZE - 1u);
    pml4_t pml4          = xhci_current_pml4();
    for (uint64_t phys = start; phys < end; phys += PAGE_SIZE) {
        const uint64_t virt = phys + g_hhdm_offset;
        if (vmm_virt_to_phys(pml4, virt) == 0) {
            vmm_map_page(pml4, virt, phys, PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT);
        }
    }
}

static bool xhci_get_mmio_bar(const struct pci_device *device, uint64_t *base_out)
{
    const uint32_t bar0 = device->bars[0];
    if (bar0 == 0 || (bar0 & PCI_BAR_IO)) {
        return false;
    }

    uint64_t base           = (uint64_t)(bar0 & ~0xFULL);
    const uint32_t mem_type = (bar0 >> 1) & 0x3;
    if (mem_type == PCI_BAR_MEMORY_TYPE_64) {
        const uint32_t bar1 = device->bars[1];
        base                |= (uint64_t)bar1 << 32;
    }

    if (base == 0) {
        return false;
    }

    *base_out = base;
    return true;
}

void xhci_init(struct pci_device device)
{
    if (device.prog_if != 0x30) {
        return;
    }

    ehci_quiesce_all();

    uint64_t mmio_phys = 0;
    if (!xhci_get_mmio_bar(&device, &mmio_phys)) {
        boot_message(ERROR, "[xHCI] No MMIO BAR");
        return;
    }

    xhci_map_mmio_range(mmio_phys, XHCI_MMIO_MAP_BYTES);
    g_xhci.mmio    = (volatile uint8_t *)(mmio_phys + g_hhdm_offset);
    const uint32_t cap = xhci_read32(g_xhci.mmio, XHCI_CAPLENGTH);
    g_xhci.cap_len = (uint8_t)(cap & 0xFFu);
    const uint32_t hcs = xhci_read32(g_xhci.mmio, XHCI_HCSPARAMS1);
    g_xhci.op_base = g_xhci.mmio + g_xhci.cap_len;

    boot_message(INFO,
                 "[xHCI] PCI %04x:%04x bus=%u slot=%u func=%u MMIO=0x%lx",
                 device.vendor_id,
                 device.device_id,
                 device.bus,
                 device.slot,
                 device.function,
                 (unsigned long)mmio_phys);
    boot_message(INFO, "[xHCI] caplen=%u hcsparams1=0x%08x", g_xhci.cap_len, hcs);

    uint32_t cmd = xhci_read32(g_xhci.op_base, XHCI_OP_USBCMD);
    cmd          &= ~XHCI_USBCMD_RS;
    xhci_write32(g_xhci.op_base, XHCI_OP_USBCMD, cmd);
    if (!xhci_wait_for(g_xhci.op_base, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, true, XHCI_TIMEOUT_MS)) {
        boot_message(WARNING, "[xHCI] Stop timeout");
    }

    cmd = xhci_read32(g_xhci.op_base, XHCI_OP_USBCMD);
    cmd |= XHCI_USBCMD_HCRST;
    xhci_write32(g_xhci.op_base, XHCI_OP_USBCMD, cmd);

    if (!xhci_wait_for(g_xhci.op_base,
                       XHCI_OP_USBCMD,
                       XHCI_USBCMD_HCRST,
                       false,
                       XHCI_RESET_TIMEOUT_MS)) {
        boot_message(ERROR, "[xHCI] Reset timeout");
        return;
    }

    if (!xhci_wait_for(g_xhci.op_base,
                       XHCI_OP_USBSTS,
                       XHCI_USBSTS_CNR,
                       false,
                       XHCI_RESET_TIMEOUT_MS)) {
        boot_message(ERROR, "[xHCI] Controller not ready");
        return;
    }
}
