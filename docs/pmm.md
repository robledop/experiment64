# Physical Memory Manager (`pmm.c`)

The PMM tracks physical page ownership using a bitmap. It provides page
allocations for the VMM and other subsystems.

---

## Key Concepts

- **Bitmap allocator**: one bit per physical page (1 = used, 0 = free).
- **HHDM**: the PMM bitmap lives in a usable region, addressed via the HHDM
  offset (`phys + hhdm_offset`).
- **Reserved base page**: the allocator skips a guard region after the bitmap
  to avoid accidental reuse.

---

## Initialization

`pmm_init(hhdm_offset)`:

1. Reads the Limine memory map.
2. Finds the highest usable address and sizes the bitmap.
3. Places the bitmap in a usable region and marks all pages as used.
4. Marks usable regions as free.
5. Marks the bitmap itself as used and reserves a guard region.
6. Marks page 0 as used to avoid null confusion.

The PMM uses a spinlock to guard bitmap operations for SMP safety.

---

## Allocation APIs

### `pmm_alloc_page()`
Returns a **physical address** of a free 4 KiB page, or `nullptr` on OOM.
Skips pages that are tracked as slab backing pages.

### `pmm_free_page(ptr)`
Accepts either a physical address or an HHDM-mapped pointer. The PMM converts
HHDM addresses back to physical and clears the bitmap bit.

### `pmm_alloc_pages(count)`
First-fit contiguous allocation. Returns a physical base address or `nullptr`.

### `pmm_free_pages(ptr, count)`
Frees a contiguous range. Accepts a physical address or HHDM pointer.

---

## Slab Interaction

The heap tracks slab backing pages. PMM allocation/free checks
`heap_is_slab_page()` to avoid handing out or freeing slab-backed memory.

---

## Current Limitations

- No per-zone or NUMA awareness.
- No coalescing or best-fit; contiguous allocations use a simple first-fit scan.
- No validation that freed ranges are within allocator-managed memory.
