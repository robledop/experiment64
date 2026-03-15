# Physical Memory Manager (`pmm.c`)

The PMM tracks physical page ownership using a bitmap. It provides page
allocations for the VMM and other subsystems.

---

## Key Concepts

- **Bitmap allocator**: one bit per physical page (1 = used, 0 = free).
- **HHDM**: the PMM bitmap lives in a usable region, addressed via the HHDM
  offset (`phys + hhdm_offset`).
- **Reserved base page**: the allocator starts at `reserved_base_page`, which is
  set 16 pages past the end of the bitmap. These 16 guard pages are **not**
  marked in the bitmap; they are simply below `reserved_base_page` and therefore
  never considered by the allocator.

---

## Initialization

`pmm_init(hhdm_offset)`:

1. Reads the Limine memory map.
2. Finds the highest usable address and sizes the bitmap.
3. Places the bitmap in a usable region and marks all pages as used.
4. Marks usable regions as free.
5. Marks the bitmap itself as used and sets `reserved_base_page` past a 16-page
   guard region.
6. Marks page 0 as used to avoid null confusion.

The PMM uses a spinlock to guard bitmap operations for SMP safety.

---

## Allocation APIs

### `pmm_alloc_page()`
Returns a **physical address** of a free 4 KiB page, or `nullptr` on OOM.
Uses a `next_free_hint` to avoid rescanning from the beginning; the scan starts
at the hint and wraps around in a second pass if needed. If a free bitmap bit
corresponds to a slab-tracked page, this is treated as an **error condition**
(logged at ERROR level) and that page is skipped.

### `pmm_free_page(ptr)`
Accepts either a physical address or an HHDM-mapped pointer. The PMM converts
HHDM addresses back to physical and clears the bitmap bit.

### `pmm_alloc_pages(count)`
First-fit contiguous allocation. Returns a physical base address or `nullptr`.

### `pmm_free_pages(ptr, count)`
Frees a contiguous range. Accepts a physical address or HHDM pointer.

### `pmm_get_highest_addr()`
Returns the highest usable physical address discovered during init.

### `pmm_get_reserved_base_page()`
Returns the `reserved_base_page` index (first page the allocator will consider).

### `pmm_get_bitmap_phys()`
Returns the physical address of the bitmap.

### `pmm_get_bitmap_size()`
Returns the bitmap size in bytes.

---

## Slab Interaction

The heap tracks slab backing pages. PMM allocation/free checks
`heap_is_slab_page()` to avoid handing out or freeing slab-backed memory.