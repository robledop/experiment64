#include "ahci.h"
#include "spinlock.h"
#include "string.h"
#include "heap.h"
#include "terminal.h"
#include "vmm.h"
#include "pmm.h"
#include "ide.h"
#include <stdbool.h>
#include <stdint.h>

#define AHCI_GHC_ENABLE (1u << 31)

#define AHCI_DET_NO_DEVICE 0x0
#define AHCI_DET_DEVICE_PRESENT 0x1
#define AHCI_DET_DEVICE_PRESENT_ACTIVE 0x3

#define AHCI_IPM_NOT_PRESENT 0x0
#define AHCI_IPM_ACTIVE 0x1
#define AHCI_IPM_PARTIAL 0x2
#define AHCI_IPM_SLUMBER 0x6

#define AHCI_HBA_PxCMD_ST (1u << 0)
#define AHCI_HBA_PxCMD_FRE (1u << 4)
#define AHCI_HBA_PxCMD_FR (1u << 14)
#define AHCI_HBA_PxCMD_CR (1u << 15)

#define AHCI_PORT_IS_TFES (1u << 30)

#define AHCI_TFD_ERR 0x01
#define AHCI_TFD_DRQ 0x08
#define AHCI_TFD_BUSY 0x80

#define AHCI_COMMAND_LIST_BYTES 1024u
#define AHCI_RECEIVED_FIS_BYTES 256u
#define AHCI_PRDT_MAX_BYTES (4u * 1024u * 1024u)
#define AHCI_MAX_SECTORS_PER_CMD (AHCI_PRDT_MAX_BYTES / AHCI_SECTOR_SIZE)
#define AHCI_CMD_SLOT 0u
#define AHCI_GENERIC_TIMEOUT 1000000u
#define AHCI_MMIO_BYTES 0x1100u

struct ahci_prdt_entry
{
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc;
} __attribute__((packed));

struct ahci_command_header
{
    uint16_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} __attribute__((packed));

struct ahci_command_table
{
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved0[48];
    struct ahci_prdt_entry prdt[1];
} __attribute__((packed));

struct ahci_port_state
{
    bool configured;
    uint8_t port_index;
    volatile struct ahci_port *port;
    struct ahci_command_header *command_list;
    struct ahci_command_table *command_table;
    uint8_t *fis;
    uint8_t *bounce_buffer;
    uintptr_t bounce_phys;
};

static volatile struct ahci_memory *hba_memory;
static struct ahci_port_state active_port;
static spinlock_t ahci_lock;
static bool ahci_lock_initialized;

static const char *ahci_det_to_string(const uint8_t det)
{
    switch (det) {
    case AHCI_DET_NO_DEVICE:
        return "no device";
    case AHCI_DET_DEVICE_PRESENT:
        return "device present";
    case AHCI_DET_DEVICE_PRESENT_ACTIVE:
        return "device active";
    default:
        return "reserved";
    }
}

static const char *ahci_ipm_to_string(const uint8_t ipm)
{
    switch (ipm) {
    case AHCI_IPM_NOT_PRESENT:
        return "not present";
    case AHCI_IPM_ACTIVE:
        return "active";
    case AHCI_IPM_PARTIAL:
        return "partial";
    case AHCI_IPM_SLUMBER:
        return "slumber";
    default:
        return "reserved";
    }
}

static bool ahci_port_device_present(const uint8_t det)
{
    return det == AHCI_DET_DEVICE_PRESENT || det == AHCI_DET_DEVICE_PRESENT_ACTIVE;
}

static uintptr_t ahci_virt_to_phys(const void *virt)
{
    if (!virt)
        return 0;
    return (uintptr_t)virt - g_hhdm_offset;
}

#if UINTPTR_MAX > 0xFFFFFFFFu
static inline uint32_t ahci_upper32(const uintptr_t value)
{
    return (uint32_t)(value >> 32);
}
#else
static inline uint32_t ahci_upper32(uintptr_t value)
{
    (void)value;
    return 0u;
}
#endif

static uint32_t ahci_calculate_chunk(const uint8_t *buffer, const uint32_t requested_sectors, uintptr_t *phys_out,
                                bool *needs_bounce)
{
    const uintptr_t phys = ahci_virt_to_phys(buffer);
    if (phys == 0) {
        *phys_out     = active_port.bounce_phys;
        *needs_bounce = true;
        return 1;
    }

    const size_t offset     = phys & (PAGE_SIZE - 1u);
    size_t contiguous_bytes = PAGE_SIZE - offset;
    if (contiguous_bytes > AHCI_PRDT_MAX_BYTES) {
        contiguous_bytes = AHCI_PRDT_MAX_BYTES;
    }

    const size_t requested_bytes = (size_t)requested_sectors * AHCI_SECTOR_SIZE;

    if (contiguous_bytes >= AHCI_SECTOR_SIZE) {
        if (contiguous_bytes > requested_bytes) {
            contiguous_bytes = requested_bytes;
        }

        uint32_t sectors = (uint32_t)(contiguous_bytes / AHCI_SECTOR_SIZE);
        if (sectors == 0) {
            sectors = 1;
        }

        if (sectors > AHCI_MAX_SECTORS_PER_CMD) {
            sectors = AHCI_MAX_SECTORS_PER_CMD;
        }

        *phys_out     = phys;
        *needs_bounce = false;
        return sectors;
    }

    // Crosses a page with less than a full sector remaining; fall back to the bounce buffer.
    *phys_out     = active_port.bounce_phys;
    *needs_bounce = true;
    return 1;
}

static void ahci_init_lock()
{
    if (!ahci_lock_initialized) {
        spinlock_init(&ahci_lock);
        ahci_lock_initialized = true;
    }
}

static void *ahci_alloc_aligned(const size_t size, const size_t alignment)
{
    const size_t total = size + alignment - 1;
    uint8_t *raw            = kmalloc(total);
    if (!raw) {
        return NULL;
    }

    const uintptr_t addr    = (uintptr_t)raw;
    const uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    uint8_t *const ptr      = (uint8_t *)(uintptr_t)aligned;
    memset(ptr, 0, size);
    return ptr;
}

static int ahci_port_wait(const volatile struct ahci_port *port, const uint32_t mask)
{
    uint32_t timeout = AHCI_GENERIC_TIMEOUT;
    while ((port->tfd & mask) != 0 && timeout-- > 0) {
        // busy wait
    }

    if (timeout == 0) {
        return -1;
    }

    return 0;
}

static int ahci_port_stop(volatile struct ahci_port *port)
{
    port->cmd &= ~AHCI_HBA_PxCMD_ST;

    uint32_t timeout = AHCI_GENERIC_TIMEOUT;
    while ((port->cmd & AHCI_HBA_PxCMD_CR) != 0 && timeout-- > 0) {
        // busy wait
    }
    if (timeout == 0) {
        return -1;
    }

    port->cmd &= ~AHCI_HBA_PxCMD_FRE;
    timeout = AHCI_GENERIC_TIMEOUT;
    while ((port->cmd & AHCI_HBA_PxCMD_FR) != 0 && timeout-- > 0) {
        // busy wait
    }

    return timeout == 0 ? -1 : 0;
}

static int ahci_port_start(volatile struct ahci_port *port)
{
    uint32_t timeout = AHCI_GENERIC_TIMEOUT;
    while ((port->cmd & (AHCI_HBA_PxCMD_CR | AHCI_HBA_PxCMD_FR)) != 0 && timeout-- > 0) {
        // busy wait
    }
    if (timeout == 0) {
        return -1;
    }

    port->cmd |= AHCI_HBA_PxCMD_FRE;
    port->cmd |= AHCI_HBA_PxCMD_ST;
    return 0;
}

static int ahci_configure_active_port(volatile struct ahci_memory *memory, const uint32_t port_index)
{
    volatile struct ahci_port *port = &memory->ports[port_index];

    int status = ahci_port_stop(port);
    if (status != 0) {
        boot_message(ERROR,
                     "[AHCI] failed to stop command engine on port %lu",
                     (unsigned long)port_index);
        return status;
    }

    struct ahci_command_header *const command_list =
        (struct ahci_command_header *)ahci_alloc_aligned(AHCI_COMMAND_LIST_BYTES, 1024);
    uint8_t *const fis                                  = ahci_alloc_aligned(AHCI_RECEIVED_FIS_BYTES, 256);
    struct ahci_command_table *const command_table =
        (struct ahci_command_table *)ahci_alloc_aligned(sizeof(struct ahci_command_table), 128);
    uint8_t *const bounce_buffer = ahci_alloc_aligned(AHCI_SECTOR_SIZE, AHCI_SECTOR_SIZE);

    if (!command_list || !fis || !command_table || !bounce_buffer) {
        boot_message(ERROR,
                     "[AHCI] failed to allocate command structures for port %lu",
                     (unsigned long)port_index);
        return -1;
    }

    memset(command_list, 0, AHCI_COMMAND_LIST_BYTES);
    memset(fis, 0, AHCI_RECEIVED_FIS_BYTES);
    memset(command_table, 0, sizeof(struct ahci_command_table));
    memset(bounce_buffer, 0, AHCI_SECTOR_SIZE);

    const uintptr_t clb_phys    = ahci_virt_to_phys(command_list);
    const uintptr_t fb_phys     = ahci_virt_to_phys(fis);
    const uintptr_t ct_phys     = ahci_virt_to_phys(command_table);
    const uintptr_t bounce_phys = ahci_virt_to_phys(bounce_buffer);

    if (clb_phys == 0 || fb_phys == 0 || ct_phys == 0 || bounce_phys == 0) {
        boot_message(ERROR, "[AHCI] failed to resolve physical addresses for command buffers");
        return -1;
    }

    port->clb  = (uint32_t)clb_phys;
    port->clbu = ahci_upper32(clb_phys);
    port->fb   = (uint32_t)fb_phys;
    port->fbu  = ahci_upper32(fb_phys);

    command_list[AHCI_CMD_SLOT].ctba  = (uint32_t)ct_phys;
    command_list[AHCI_CMD_SLOT].ctbau = ahci_upper32(ct_phys);
    command_list[AHCI_CMD_SLOT].prdtl = 1;

    port->serr = 0xFFFFFFFF;
    port->is   = 0xFFFFFFFF;

    status = ahci_port_start(port);
    if (status != 0) {
        boot_message(ERROR,
                     "[AHCI] failed to start command engine on port %lu",
                     (unsigned long)port_index);
        return status;
    }

    active_port.configured    = true;
    active_port.port_index    = (uint8_t)port_index;
    active_port.port          = port;
    active_port.command_list  = command_list;
    active_port.command_table = command_table;
    active_port.fis           = fis;
    active_port.bounce_buffer = bounce_buffer;
    active_port.bounce_phys   = bounce_phys;

    ahci_init_lock();

    boot_message(INFO, "[AHCI] using port %lu for DMA transfers", (unsigned long)port_index);
    return 0;
}

void ahci_init(struct pci_device device)
{
    boot_message(INFO,
                 "[AHCI] controller %x:%x at %lu:%lu.%lu",
                 device.header.vendor_id,
                 device.header.device_id,
                 (unsigned long)device.bus,
                 (unsigned long)device.slot,
                 (unsigned long)device.function);

    if (device.header.prog_if != 0x01) {
        boot_message(WARNING,
                     "[AHCI] controller is not in AHCI mode (prog_if=0x%x)",
                     device.header.prog_if);
        return;
    }

    pci_enable_bus_mastering(device);

    uint32_t abar = device.header.bars[5] & ~0x0F;
    if (abar == 0) {
        abar = pci_get_bar(device, PCI_BAR_MEM) & ~0x0F;
    }

    if (abar == 0) {
        boot_message(ERROR, "[AHCI] controller missing ABAR; cannot continue");
        return;
    }

    void *abar_va = (void *)((uint64_t)abar + g_hhdm_offset);
    if (abar_va == nullptr) {
        boot_message(ERROR, "[AHCI] failed to map ABAR 0x%lx", (unsigned long)abar);
        return;
    }
    hba_memory = (volatile struct ahci_memory *)abar_va;

    // Ensure AHCI mode is enabled.
    hba_memory->ghc |= AHCI_GHC_ENABLE;

    const uint32_t version = hba_memory->vs;
    const uint32_t cap     = hba_memory->cap;
    const uint32_t ports   = hba_memory->pi;

    const uint32_t port_count    = (cap & 0x1F) + 1;
    const uint32_t version_major = (version >> 16) & 0xFFFF;
    const uint32_t version_minor = version & 0xFFFF;

    boot_message(INFO,
                 "[AHCI] ABAR=0x%lx version %lu.%lu cap=0x%lx ports mask=0x%lx",
                 (unsigned long)abar,
                 (unsigned long)version_major,
                 (unsigned long)version_minor,
                 (unsigned long)cap,
                 (unsigned long)ports);

    uint32_t port_mask = ports;
    if (port_mask == 0) {
        if (port_count == 0 || port_count > 32) {
        boot_message(ERROR,
                     "[AHCI] invalid port count reported in CAP (NP=%lu)",
                     (unsigned long)port_count);
            return;
        }

        port_mask = (port_count == 32) ? 0xFFFFFFFFu : ((1u << port_count) - 1u);
        boot_message(ERROR,
                     "[AHCI] controller reports empty PI; using CAP.NP derived mask=0x%lx",
                     (unsigned long)port_mask);
    }

    if (port_mask == 0) {
        boot_message(ERROR, "[AHCI] no ports implemented");
        return;
    }

    bool device_present_found = false;
    bool link_active_found    = false;

    for (uint32_t i = 0; i < 32; i++) {
        if ((port_mask & (1u << i)) == 0) {
            continue;
        }

        volatile struct ahci_port *const port = &hba_memory->ports[i];
        const uint32_t ssts                        = port->ssts;
        const uint8_t det                          = (uint8_t)(ssts & 0x0F);
        const uint8_t ipm                          = (uint8_t)((ssts >> 8) & 0x0F);

        const bool device_present = ahci_port_device_present(det);
        const bool link_active    = det == AHCI_DET_DEVICE_PRESENT_ACTIVE && ipm == AHCI_IPM_ACTIVE;

        if (device_present) {
            device_present_found = true;
        }
        if (link_active) {
            link_active_found = true;
        }

        boot_message(INFO,
                     "[AHCI] port %lu: det=%s(%u) ipm=%s(%u) sig=0x%lx%s%s",
                     (unsigned long)i,
                     ahci_det_to_string(det),
                     det,
                     ahci_ipm_to_string(ipm),
                     ipm,
                     (unsigned long)port->sig,
                     link_active ? " [link-up]" : "",
                     device_present && !link_active ? " [present]" : "");

        if (!active_port.configured && link_active) {
            if (ahci_configure_active_port(hba_memory, i) != 0) {
                boot_message(ERROR, "[AHCI] failed to configure port %lu for DMA", (unsigned long)i);
            }
        }
    }

    if (!device_present_found) {
        boot_message(WARNING, "[AHCI] no SATA devices detected on implemented ports");
    } else if (!link_active_found) {
        boot_message(WARNING,
                     "[AHCI] SATA device presence detected but links are not active (DET != 3 or IPM != 1)");
    }
}

bool ahci_port_ready(void)
{
    return active_port.configured;
}

static int ahci_issue_dma(uint64_t lba, const uintptr_t buffer_phys, const uint32_t sector_count, const bool write)
{
    if (!active_port.configured) {
        return -1;
    }

    volatile struct ahci_port *const port = active_port.port;

    int status = ahci_port_wait(port, AHCI_TFD_BUSY | AHCI_TFD_DRQ);
    if (status != 0) {
        return status;
    }

    port->serr = 0xFFFFFFFF;
    port->is   = 0xFFFFFFFF;

    struct ahci_command_header *const header = &active_port.command_list[AHCI_CMD_SLOT];
    struct ahci_command_table *const table   = active_port.command_table;

    memset(table, 0, sizeof(*table));

    header->flags = 5; // CFL = 5 (20 bytes)
    if (write) {
        header->flags |= 1u << 6; // write
    } else {
        header->flags &= ~(1u << 6);
    }
    header->prdtl = 1;
    header->prdbc = 0;

    struct ahci_prdt_entry *const prdt = &table->prdt[0];
    const uint32_t bytes                    = sector_count * AHCI_SECTOR_SIZE;

    prdt->dba  = (uint32_t)buffer_phys;
    prdt->dbau = ahci_upper32(buffer_phys);
    prdt->dbc  = (bytes - 1) | (1u << 31); // Interrupt on completion

    uint8_t *const cfis = table->cfis;
    memset(cfis, 0, sizeof(table->cfis));

    cfis[0]  = 0x27; // FIS type: Register Host to Device
    cfis[1]  = 1u << 7;
    cfis[2]  = write ? 0x35 : 0x25; // WRITE/READ DMA EXT
    cfis[3]  = 0;
    cfis[4]  = (uint8_t)(lba & 0xFF);
    cfis[5]  = (uint8_t)((lba >> 8) & 0xFF);
    cfis[6]  = (uint8_t)((lba >> 16) & 0xFF);
    cfis[7]  = (uint8_t)(0x40 | ((lba >> 24) & 0x0F));
    cfis[8]  = (uint8_t)((lba >> 24) & 0xFF);
    cfis[9]  = (uint8_t)((lba >> 32) & 0xFF);
    cfis[10] = (uint8_t)((lba >> 40) & 0xFF);
    cfis[12] = (uint8_t)(sector_count & 0xFF);
    cfis[13] = (uint8_t)((sector_count >> 8) & 0xFF);

    port->ci = 1u << AHCI_CMD_SLOT;

    uint32_t timeout = AHCI_GENERIC_TIMEOUT;
    while ((port->ci & (1u << AHCI_CMD_SLOT)) != 0 && timeout-- > 0) {
        if (port->is & AHCI_PORT_IS_TFES) {
            const uint32_t is   = port->is;
            const uint32_t serr = port->serr;
            const uint32_t tfd  = port->tfd;

            boot_message(ERROR,
                         "[AHCI] DMA taskfile error during %s: LBA=%llu count=%u IS=0x%x SERR=0x%x TFD=0x%x",
                         write ? "write" : "read",
                         (unsigned long long)lba,
                         sector_count,
                         is,
                         serr,
                         tfd);
            port->is = AHCI_PORT_IS_TFES;
            return -1;
        }
    }

    if (timeout == 0) {
        const uint32_t is   = port->is;
        const uint32_t serr = port->serr;
        const uint32_t tfd  = port->tfd;
        boot_message(ERROR,
                     "[AHCI] DMA timeout during %s: LBA=%llu count=%u IS=0x%x SERR=0x%x TFD=0x%x",
                     write ? "write" : "read",
                     (unsigned long long)lba,
                     sector_count,
                     is,
                     serr,
                     tfd);
        port->is = 0xFFFFFFFF;
        return -1;
    }

    if (port->tfd & AHCI_TFD_ERR) {
        const uint32_t serr = port->serr;
        const uint32_t is   = port->is;
        const uint32_t tfd  = port->tfd;
        boot_message(ERROR,
                     "[AHCI] DMA taskfile status error during %s: LBA=%llu count=%u IS=0x%x SERR=0x%x TFD=0x%x",
                     write ? "write" : "read",
                     (unsigned long long)lba,
                     sector_count,
                     is,
                     serr,
                     tfd);
        port->is = 0xFFFFFFFF;
        return -1;
    }

    return 0;
}

int ahci_read(uint64_t lba, uint32_t sector_count, void *buffer)
{
    if (!buffer || sector_count == 0) {
        return -1;
    }

    if (!active_port.configured) {
        return -1;
    }

    spinlock_acquire(&ahci_lock);

    uint8_t *byte_buffer = (uint8_t *)buffer;
    uint32_t remaining   = sector_count;
    int result      = 0;

    while (remaining > 0) {
        uintptr_t buffer_phys  = 0;
        bool needs_bounce = false;
        uint32_t chunk         = ahci_calculate_chunk(byte_buffer, remaining, &buffer_phys, &needs_bounce);

        result = ahci_issue_dma(lba, buffer_phys, chunk, false);
        if (result != 0) {
            break;
        }

        if (needs_bounce) {
            memcpy(byte_buffer, active_port.bounce_buffer, AHCI_SECTOR_SIZE);
        }

        lba += chunk;
        byte_buffer += chunk * AHCI_SECTOR_SIZE;
        remaining -= chunk;
    }

    spinlock_release(&ahci_lock);
    return result;
}

int ahci_write(uint64_t lba, uint32_t sector_count, const void *buffer)
{
    if (!buffer || sector_count == 0) {
        return -1;
    }

    if (!active_port.configured) {
        return -1;
    }

    spinlock_acquire(&ahci_lock);

    const uint8_t *byte_buffer_const = (const uint8_t *)buffer;
    uint32_t remaining               = sector_count;
    int result                  = 0;

    while (remaining > 0) {
        uintptr_t buffer_phys  = 0;
        bool needs_bounce = false;
        uint32_t chunk         = ahci_calculate_chunk(byte_buffer_const, remaining, &buffer_phys, &needs_bounce);

        if (needs_bounce) {
            memcpy(active_port.bounce_buffer, byte_buffer_const, AHCI_SECTOR_SIZE);
        }

        result = ahci_issue_dma(lba, buffer_phys, chunk, true);
        if (result != 0) {
            break;
        }

        lba += chunk;
        byte_buffer_const += chunk * AHCI_SECTOR_SIZE;
        remaining -= chunk;
    }

    spinlock_release(&ahci_lock);
    return result;
}
