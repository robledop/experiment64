# Virtual Memory Manager (`vmm.c`)

The VMM builds and manipulates x86_64 page tables. It is responsible for
allocating paging structures with the PMM, mapping/unmapping virtual addresses,
and creating per-process page tables.

---

## Key Concepts

- **PML4**: top-level x86_64 page table.
- **User vs kernel space**: user mappings live in PML4 slots 0-255, kernel
  mappings live in 256-511.
- **HHDM**: the higher-half direct map provides a fixed offset to access
  physical memory as virtual (`phys + g_hhdm_offset`).

---

## Core Functions

### `vmm_init(hhdm_offset)`
Stores the HHDM offset used to convert physical addresses into a kernel virtual
pointer for page table access.

### `vmm_map_page(pml4, virt, phys, flags)`
Walks (or creates) PML4 -> PDPT -> PD -> PT and inserts a mapping.
`pml4` must be a **physical address** (HHDM offset is added internally).

Important details:
- Intermediate tables are allocated with `pmm_alloc_page()`.
- Intermediate entries are marked `PTE_PRESENT | PTE_WRITABLE | PTE_USER`.
- Performs `invlpg` for the mapped virtual address.

### `vmm_unmap_page(pml4, virt)`
Clears the PTE for `virt` and executes `invlpg`. `pml4` is a physical address.
It does **not** free page tables.

### `vmm_new_pml4()`
Allocates a new PML4 and copies the **kernel half** (entries 256-511) from the
current CR3. The user half is empty.

### `vmm_copy_pml4(src)`
Creates a deep copy of the **user half** (entries 0-255) of `src` and reuses the
kernel half copied by `vmm_new_pml4()`.

### `vmm_destroy_pml4(pml4)`
Recursively frees the user half (entries 0-255): intermediate page tables **and**
the mapped physical (leaf) pages are freed via `free_page_table_level`. The
PML4 itself is freed. The kernel half is shared and is not freed.

### `vmm_switch_pml4(pml4)`
Writes CR3 to switch address spaces.

### `vmm_virt_to_phys(pml4, virt)`
Walks the page tables for the given virtual address and returns the physical
address (including page offset). Returns 0 if the mapping is not present.

### `vmm_finalize()`
Allocates a fresh kernel PML4 via `vmm_new_pml4()`, switches to it, replacing
the bootloader-provided page tables. Panics on failure.

---

## Typical Usage

```c
// Create a new address space
pml4_t pml4 = vmm_new_pml4();

// Map a user page
void *phys = pmm_alloc_page();
vmm_map_page(pml4, 0x400000, (uint64_t)phys,
             PTE_PRESENT | PTE_WRITABLE | PTE_USER);

// Switch into it
vmm_switch_pml4(pml4);
```

---

## User mappings and mmap

User virtual memory areas (VMAs) track `mmap` ranges so they can be released later.
`sys_mmap` supports:

- `MAP_SHARED` framebuffer mappings (`/dev/fb0`).
- `MAP_ANONYMOUS` mappings backed by PMM pages and zeroed on map.

Anonymous mappings are tagged with `VMA_ANON` so `sys_munmap` can free the
backing pages before unmapping. `PROT_NONE` anonymous mappings reserve address
space without backing pages, which is useful for guard pages.

`sys_munmap` accepts stack VMAs (`VMA_STACK`) as well, which is how user thread
stacks are released on `SYS_THREAD_EXIT`. It can also unmap subranges of a VMA,
splitting or trimming the VMA as needed.
