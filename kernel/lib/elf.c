#include "elf.h"
#include "vfs.h"
#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "string.h"
#include "terminal.h"
#include "uart.h"

bool elf_load(const char *path, uint64_t *entry_point, uint64_t *max_vaddr, pml4_t pml4)
{
    vfs_inode_t *node = vfs_resolve_path(path);
    if (!node)
    {
        printk("ELF: Failed to resolve path %s\n", path);
        return false;
    }

    if (max_vaddr)
        *max_vaddr = 0;

    Elf64_Ehdr header;
    if (vfs_read(node, 0, sizeof(header), (uint8_t *)&header) != sizeof(header))
    {
        printk("ELF: Failed to read header\n");
        if (node != vfs_root)
        {
            vfs_close(node);
            kfree(node);
        }
        return false;
    }

    if (*(uint32_t *)header.e_ident != ELF_MAGIC)
    {
        printk("ELF: Invalid magic\n");
        if (node != vfs_root)
        {
            vfs_close(node);
            kfree(node);
        }
        return false;
    }

    // Read program headers
    uint64_t ph_size = header.e_phnum * header.e_phentsize;
    Elf64_Phdr *phdrs = kmalloc(ph_size);
    if (vfs_read(node, header.e_phoff, ph_size, (uint8_t *)phdrs) != ph_size)
    {
        printk("ELF: Failed to read program headers\n");
        kfree(phdrs);
        if (node != vfs_root)
        {
            vfs_close(node);
            kfree(node);
        }
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

            // Read segment data into a temporary buffer first
            uint8_t *temp_buf = NULL;
            if (ph->p_filesz > 0)
            {
                temp_buf = kmalloc(ph->p_filesz);
                if (!temp_buf)
                {
                    printk("ELF: Failed to allocate temp buffer\n");
                    kfree(phdrs);
                    if (node != vfs_root)
                    {
                        vfs_close(node);
                        kfree(node);
                    }
                    return false;
                }

                if (vfs_read(node, ph->p_offset, ph->p_filesz, temp_buf) != ph->p_filesz)
                {
                    printk("ELF: Failed to read segment data\n");
                    kfree(temp_buf);
                    kfree(phdrs);
                    if (node != vfs_root)
                    {
                        vfs_close(node);
                        kfree(node);
                    }
                    return false;
                }
            }

            for (uint64_t addr = page_start; addr < page_end; addr += PAGE_SIZE)
            {
                // Check if already mapped?
                // For now, just allocate new page.
                void *phys = pmm_alloc_page();
                if (!phys)
                {
                    if (temp_buf)
                        kfree(temp_buf);
                    kfree(phdrs);
                    if (node != vfs_root)
                    {
                        vfs_close(node);
                        kfree(node);
                    }
                    return false;
                }
                // Map as writable initially to copy data
                vmm_map_page(pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

                // Copy data using HHDM
                uint8_t *dest_ptr = (uint8_t *)((uint64_t)phys + g_hhdm_offset);
                memset(dest_ptr, 0, PAGE_SIZE);

                if (temp_buf)
                {
                    uint64_t seg_start = ph->p_vaddr;
                    uint64_t seg_file_end = seg_start + ph->p_filesz;

                    uint64_t copy_start = (addr > seg_start) ? addr : seg_start;
                    uint64_t copy_end = (addr + PAGE_SIZE < seg_file_end) ? (addr + PAGE_SIZE) : seg_file_end;

                    if (copy_start < copy_end)
                    {
                        uint64_t offset_in_page = copy_start - addr;
                        uint64_t offset_in_file = copy_start - seg_start;
                        uint64_t len = copy_end - copy_start;

                        memcpy(dest_ptr + offset_in_page, temp_buf + offset_in_file, len);
                    }
                }
            }

            if (temp_buf)
                kfree(temp_buf);
        }
    }

    kfree(phdrs);
    *entry_point = header.e_entry;
    if (node != vfs_root)
    {
        vfs_close(node);
        kfree(node);
    }
    return true;
}
