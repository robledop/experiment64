# Kernel Heap Allocator (`heap.c`)

The kernel heap provides dynamic allocations via `kmalloc`/`kfree`. It is a
hybrid allocator:

- Small allocations (<= 2048 bytes) use fixed-size slab caches.
- Large allocations use contiguous page ranges from the PMM.

All heap operations are protected by a global spinlock with IRQ save/restore.

---

## Initialization

`heap_init(hhdm_offset)`:

1. Stores the HHDM offset so physical pages can be accessed via
   `phys + hhdm_offset`.
2. Enables CR0.WP so read-only kernel pages fault on write.
3. Initializes the slab cache lists and the heap lock.

---

## Slab Allocations (<= 2048 bytes)

### Cache sizes

The allocator uses 7 caches: 32, 64, 128, 256, 512, 1024, 2048 bytes.

### Slab layout

Each slab is one page and starts with a `slab_header_t` that contains:

- `magic` and `guard_magic` (sanity checks)
- `is_slab` flag
- `obj_size`, `free_count`, `free_list`
- `list` for the cache list

Between the header and the payload is a guard region filled with `SLAB_GUARD`.
The payload area is aligned to 64 bytes to satisfy FPU/XSTATE alignment.

### Free list and poisoning

Free objects are linked inside the slab. The allocator:

- Poisons freed payloads with `POISON_FREE`
- Checks poison when reusing a slot
- Fills allocated payloads with `POISON_ALLOC`

Only the payload area is poisoned; the first word is reserved for the free-list
pointer.

### Slab tracking

Slab backing pages are recorded in `slab_pages[]`. The PMM consults this to
avoid allocating or freeing slab-backed pages.

### Slab release

When all objects in a slab are free, the slab page is returned to the PMM,
except for the 64-byte cache (index 1), which is kept resident to simplify
corruption tracking.

---

## Large Allocations (> 2048 bytes)

Large allocations allocate `N` contiguous pages from the PMM. A `slab_header_t`
is stored at the start of the first page with:

- `is_slab = 0`
- `page_count = N`
- `obj_size = requested size`

The returned pointer is immediately after the header in the same page.

---

## API Summary

### `kmalloc(size)`
Returns a heap pointer or `nullptr` for size 0 or OOM.

### `kzalloc(size)`
`kmalloc` + `memset` to zero.

### `kfree(ptr)`
Validates the header magic and frees the object. For slabs, it re-poisons the
payload and may release the page back to the PMM.

### `krealloc(ptr, new_size)`
If growing beyond the current bucket, allocates a new block, copies the old
bucket size, then frees the old block. Shrinks are no-ops.

### Debug helpers

- `heap_is_slab_page(phys)` checks if a physical page backs a slab.
- `heap_is_slab_range(virt, len)` detects overlap with a slab page.
- `heap_is_slab_header_range(virt, len)` checks overlap with slab headers.

---

## Concurrency

All heap operations acquire `heap_lock` with IRQ save/restore. The allocator is
single-lock and does not use per-CPU caches.

---

## Current Limitations

- No per-CPU slabs or NUMA awareness.
- No in-place shrink for `krealloc`.
- Large allocations are contiguous only; no virtual-only scatter allocations.
