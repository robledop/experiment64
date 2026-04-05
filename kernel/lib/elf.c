#include <lib/elf.h>
#include <fs/vfs.h>
#include <mem/vmm.h>
#include <mem/pmm.h>
#include <mem/heap.h>
#include <lib/string.h>
#include <drivers/terminal.h>

/** @brief Base address at which the dynamic linker (ld.so) is loaded. */
#define INTERP_LOAD_BASE 0x7000000000ULL

/**
 * @brief Validate the ELF magic number in the file header.
 *
 * @param header Pointer to the ELF header read from the file.
 * @return true if magic matches, false otherwise.
 */
static bool elf_validate_header(const elf64_ehdr *header)
{
    if (*(uint32_t *)header->e_ident != ELF_MAGIC) {
        printk("ELF: Invalid magic\n");
        return false;
    }
    return true;
}

/**
 * @brief Read all program headers from an ELF file.
 *
 * Allocates a kernel buffer and reads the program header table into it.
 *
 * @param node   VFS inode of the ELF file.
 * @param header Pointer to the already-read ELF header.
 * @return Allocated array of program headers, or nullptr on failure.
 *         Caller must kfree() the returned pointer.
 */
static Elf64_Phdr *elf_read_program_headers(vfs_inode_t *node, const elf64_ehdr *header)
{
    if (header->e_phentsize != sizeof(Elf64_Phdr) || header->e_phnum == 0) {
        printk("ELF: Invalid program header size/count\n");
        return nullptr;
    }

    uint64_t ph_size  = header->e_phnum * header->e_phentsize;
    Elf64_Phdr *phdrs = kmalloc(ph_size);
    if (!phdrs) {
        printk("ELF: Failed to allocate memory for program headers\n");
        return nullptr;
    }

    if (vfs_read(node, header->e_phoff, ph_size, (uint8_t *)phdrs) != ph_size) {
        printk("ELF: Failed to read program headers\n");
        kfree(phdrs);
        return nullptr;
    }
    return phdrs;
}

/**
 * @brief Load a single PT_LOAD segment into an address space.
 *
 * Allocates physical pages, maps them at (p_vaddr + bias), and copies
 * file data into the mapped pages. BSS regions (p_memsz > p_filesz)
 * are zero-filled.
 *
 * @param node      VFS inode to read segment data from.
 * @param ph        Program header describing the segment.
 * @param pml4      Page table root for the target address space.
 * @param bias      Load bias added to all virtual addresses (0 for normal
 *                  executables, nonzero for ET_DYN objects like ld.so).
 * @param max_vaddr If non-null, updated with the highest mapped address.
 * @return true on success, false on failure.
 */
static bool elf_load_segment(vfs_inode_t *node, const Elf64_Phdr *ph,
                             pml4_t pml4, uint64_t bias, uint64_t *max_vaddr)
{
    uint64_t start_addr = ph->p_vaddr + bias;
    uint64_t end_addr   = start_addr + ph->p_memsz;

    uint64_t page_start = start_addr & ~(PAGE_SIZE - 1);
    uint64_t page_end   = (end_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (max_vaddr && page_end > *max_vaddr) {
        *max_vaddr = page_end;
    }

    uint8_t *temp_buf = nullptr;
    if (ph->p_filesz > 0) {
        temp_buf = kmalloc(ph->p_filesz);
        if (!temp_buf) {
            printk("ELF: Failed to allocate temp buffer\n");
            return false;
        }

        if (vfs_read(node, ph->p_offset, ph->p_filesz, temp_buf) != ph->p_filesz) {
            printk("ELF: Failed to read segment data\n");
            kfree(temp_buf);
            return false;
        }
    }

    for (uint64_t addr = page_start; addr < page_end; addr += PAGE_SIZE) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            if (temp_buf)
                kfree(temp_buf);
            return false;
        }

        vmm_map_page(pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

        uint8_t *dest_ptr = (uint8_t *)((uint64_t)phys + g_hhdm_offset);
        memset(dest_ptr, 0, PAGE_SIZE);

        if (temp_buf) {
            uint64_t seg_start    = ph->p_vaddr + bias;
            uint64_t seg_file_end = seg_start + ph->p_filesz;

            uint64_t copy_start = (addr > seg_start) ? addr : seg_start;
            uint64_t copy_end   = (addr + PAGE_SIZE < seg_file_end) ? (addr + PAGE_SIZE) : seg_file_end;

            if (copy_start < copy_end) {
                uint64_t offset_in_page = copy_start - addr;
                uint64_t offset_in_file = copy_start - seg_start;
                uint64_t len            = copy_end - copy_start;

                memcpy(dest_ptr + offset_in_page, temp_buf + offset_in_file, len);
            }
        }
    }

    if (temp_buf)
        kfree(temp_buf);

    return true;
}

/**
 * @brief Load an ELF binary and optionally its interpreter (dynamic linker).
 *
 * This is the extended ELF loader that supports dynamic linking. It:
 * 1. Loads all PT_LOAD segments of the executable
 * 2. Extracts PT_INTERP (path to the dynamic linker) if present
 * 3. Records PT_PHDR location for the auxiliary vector
 * 4. If PT_INTERP is present, loads the interpreter (ld.so) at a high
 *    bias address and records its entry point
 *
 * For statically linked binaries, interp_base and interp_entry in @p result
 * are zero.
 *
 * @param path   Absolute filesystem path to the ELF binary.
 * @param result Output structure filled with load metadata. On success,
 *               result->entry contains the executable's entry point,
 *               and result->interp_entry (if nonzero) is where the kernel
 *               should transfer control instead.
 * @param pml4   Page table root for the target address space.
 * @return true on success, false on failure.
 */
bool elf_load(const char *path, elf_load_result_t *result, pml4_t pml4)
{
    memset(result, 0, sizeof(*result));

    vfs_inode_t *node = vfs_resolve_path(path);
    if (!node)
        return false;

    elf64_ehdr header;
    if (vfs_read(node, 0, sizeof(header), (uint8_t *)&header) != sizeof(header)) {
        printk("ELF: Failed to read header\n");
        vfs_release(node);
        return false;
    }

    if (!elf_validate_header(&header)) {
        vfs_release(node);
        return false;
    }

    Elf64_Phdr *phdrs = elf_read_program_headers(node, &header);
    if (!phdrs) {
        vfs_release(node);
        return false;
    }

    result->phent = header.e_phentsize;
    result->phnum = header.e_phnum;

    /* First pass: scan for PT_INTERP and PT_PHDR */
    for (int i = 0; i < header.e_phnum; i++) {
        Elf64_Phdr *ph = &phdrs[i];

        if (ph->p_type == PT_INTERP) {
            /* Read the interpreter path string */
            uint64_t len = ph->p_filesz;
            if (len >= sizeof(result->interp))
                len = sizeof(result->interp) - 1;
            if (vfs_read(node, ph->p_offset, len, (uint8_t *)result->interp) != len) {
                printk("ELF: Failed to read PT_INTERP\n");
                kfree(phdrs);
                vfs_release(node);
                return false;
            }
            result->interp[len] = '\0';
        }

        if (ph->p_type == PT_PHDR) {
            result->phdr_vaddr = ph->p_vaddr;
        }
    }

    /* Second pass: load PT_LOAD segments */
    for (int i = 0; i < header.e_phnum; i++) {
        Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type == PT_LOAD) {
            if (!elf_load_segment(node, ph, pml4, 0, &result->max_vaddr)) {
                kfree(phdrs);
                vfs_release(node);
                return false;
            }
        }
    }

    /*
     * If no explicit PT_PHDR, compute phdr_vaddr from the first PT_LOAD
     * that covers e_phoff.
     */
    if (result->phdr_vaddr == 0) {
        for (int i = 0; i < header.e_phnum; i++) {
            Elf64_Phdr *ph = &phdrs[i];
            if (ph->p_type == PT_LOAD &&
                header.e_phoff >= ph->p_offset &&
                header.e_phoff < ph->p_offset + ph->p_filesz)
            {
                result->phdr_vaddr = ph->p_vaddr + (header.e_phoff - ph->p_offset);
                break;
            }
        }
    }

    /*
     * If phdr_vaddr is still 0 (no PT_LOAD covers the program headers),
     * map them explicitly into the process address space. Place them at
     * a fixed address just below the interpreter load region.
     */
    if (result->phdr_vaddr == 0 && result->interp[0] != '\0') {
        uint64_t phdr_map_addr = INTERP_LOAD_BASE - PAGE_SIZE;
        uint64_t ph_total = (uint64_t)header.e_phnum * header.e_phentsize;

        void *phys = pmm_alloc_page();
        if (!phys) {
            kfree(phdrs);
            vfs_release(node);
            return false;
        }
        vmm_map_page(pml4, phdr_map_addr, (uint64_t)phys,
                      PTE_PRESENT | PTE_USER);
        uint8_t *dest = (uint8_t *)((uint64_t)phys + g_hhdm_offset);
        memset(dest, 0, PAGE_SIZE);
        memcpy(dest, phdrs, ph_total);
        result->phdr_vaddr = phdr_map_addr;
    }

    result->entry = header.e_entry;
    kfree(phdrs);
    vfs_release(node);

    /* If an interpreter is specified, load it at a high bias */
    if (result->interp[0] != '\0') {
        vfs_inode_t *interp_node = vfs_resolve_path(result->interp);
        if (!interp_node) {
            printk("ELF: Failed to resolve interpreter %s\n", result->interp);
            return false;
        }

        elf64_ehdr interp_header;
        if (vfs_read(interp_node, 0, sizeof(interp_header), (uint8_t *)&interp_header) != sizeof(interp_header)) {
            printk("ELF: Failed to read interpreter header\n");
            vfs_release(interp_node);
            return false;
        }

        if (!elf_validate_header(&interp_header)) {
            vfs_release(interp_node);
            return false;
        }

        Elf64_Phdr *interp_phdrs = elf_read_program_headers(interp_node, &interp_header);
        if (!interp_phdrs) {
            vfs_release(interp_node);
            return false;
        }

        uint64_t interp_max = 0;
        for (int i = 0; i < interp_header.e_phnum; i++) {
            Elf64_Phdr *ph = &interp_phdrs[i];
            if (ph->p_type == PT_LOAD) {
                if (!elf_load_segment(interp_node, ph, pml4, INTERP_LOAD_BASE, &interp_max)) {
                    kfree(interp_phdrs);
                    vfs_release(interp_node);
                    return false;
                }
            }
        }

        result->interp_base = INTERP_LOAD_BASE;
        result->interp_entry = INTERP_LOAD_BASE + interp_header.e_entry;

        /* Update max_vaddr if interpreter loaded higher */
        if (interp_max > result->max_vaddr)
            result->max_vaddr = interp_max;

        kfree(interp_phdrs);
        vfs_release(interp_node);
    }

    return true;
}

