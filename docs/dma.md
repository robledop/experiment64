# DMA Allocator (`dma.c`)

The DMA helper provides contiguous, HHDM-mapped buffers for device DMA.
It allocates physical pages via the PMM and returns both physical and
virtual addresses.

---

## API

### `dma_alloc_pages(bytes, alignment, boundary, phys_out, virt_out)`
Allocates enough contiguous pages to cover `bytes` with the requested
alignment and boundary constraints.
Returns `true` on success and `false` on failure (invalid arguments or OOM).
On success, `phys_out` and `virt_out` receive the physical base and
HHDM virtual base respectively. The allocation is zeroed.

`alignment` is in bytes. A value of 0 or any value below `PAGE_SIZE` is
silently clamped to `PAGE_SIZE`. `alignment` must be a power of two.
`boundary` is in bytes. A value of 0 disables boundary checks and bypasses
the power-of-two/`PAGE_SIZE` requirement; non-zero `boundary` must be
power-of-two, at least `PAGE_SIZE`, and `bytes` must not exceed it.

### `dma_free_pages(addr, bytes)`
Frees the contiguous range backing the allocation.
Accepts a physical base address (no HHDM translation is performed).
