#include "elf.h"
#include "vfs.h"
#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "string.h"
#include "terminal.h"

bool elf_load(const char *path, uint64_t *entry_point, uint64_t *max_vaddr, pml4_t pml4)
{
    vfs_inode_t *node = vfs_resolve_path(path);
    if (!node)
    {
        printf("ELF: Failed to resolve path %s\n", path);
        return false;
    }

    if (max_vaddr)
        *max_vaddr = 0;

    Elf64_Ehdr header;
    if (vfs_read(node, 0, sizeof(header), (uint8_t *)&header) != sizeof(header))
    {
        printf("ELF: Failed to read header\n");
        return false;
    }

    if (*(uint32_t *)header.e_ident != ELF_MAGIC)
    {
        printf("ELF: Invalid magic\n");
        return false;
    }

    // Read program headers
    uint64_t ph_size = header.e_phnum * header.e_phentsize;
    Elf64_Phdr *phdrs = kmalloc(ph_size);
    if (vfs_read(node, header.e_phoff, ph_size, (uint8_t *)phdrs) != ph_size)
    {
        printf("ELF: Failed to read program headers\n");
        kfree(phdrs);
        return false;
    }

    for (int i = 0; i < header.e_phnum; i++)
    {
        Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type == PT_LOAD)
        {
            uint64_t flags = PTE_PRESENT | PTE_USER;
            if (ph->p_flags & PF_W)
                flags |= PTE_WRITABLE;

            // Align start and end to page boundaries
            uint64_t start_addr = ph->p_vaddr;
            uint64_t end_addr = ph->p_vaddr + ph->p_memsz;

            uint64_t page_start = start_addr & ~(PAGE_SIZE - 1);
            uint64_t page_end = (end_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

            if (max_vaddr && page_end > *max_vaddr)
            {
                *max_vaddr = page_end;
            }

            for (uint64_t addr = page_start; addr < page_end; addr += PAGE_SIZE)
            {
                // Check if already mapped?
                // For now, just allocate new page.
                void *phys = pmm_alloc_page();
                if (!phys)
                {
                    kfree(phdrs);
                    return false;
                }
                // Map as writable initially to copy data
                vmm_map_page(pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
            }

            // Save current CR3
            uint64_t old_cr3;
            __asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3));

            // Switch to new address space
            if ((uint64_t)pml4 != old_cr3)
                vmm_switch_pml4(pml4);

            // Zero out the whole range first (handles BSS)
            memset((void *)start_addr, 0, ph->p_memsz);

            // Read from file
            if (ph->p_filesz > 0)
            {
                vfs_read(node, ph->p_offset, ph->p_filesz, (uint8_t *)start_addr);
            }

            // Restore CR3
            if ((uint64_t)pml4 != old_cr3)
                vmm_switch_pml4((pml4_t)old_cr3);

            // Remap if read-only
            if (!(ph->p_flags & PF_W))
            {
                for (uint64_t addr = page_start; addr < page_end; addr += PAGE_SIZE)
                {
                    uint64_t phys = vmm_virt_to_phys(pml4, addr);
                    vmm_map_page(pml4, addr, phys, PTE_PRESENT | PTE_USER);
                }
            }
        }
    }

    kfree(phdrs);
    *entry_point = header.e_entry;
    return true;
}
