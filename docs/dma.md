# DMA Allocator (`dma.c`)

The DMA helper provides contiguous, HHDM-mapped buffers for device DMA.
It allocates physical pages via the PMM and returns both physical and
virtual addresses.

---

## API

### `dma_alloc_pages(bytes, alignment, boundary)`
Allocates enough contiguous pages to cover `bytes` with the requested
alignment and boundary constraints.
Returns a physical base and an HHDM virtual base via output pointers.
The allocation is zeroed.

`alignment` is in bytes. A value of 0 uses `PAGE_SIZE`.
`alignment` must be power-of-two and at least `PAGE_SIZE`.
`boundary` is in bytes. A value of 0 disables boundary checks and bypasses
the power-of-two/`PAGE_SIZE` requirement; non-zero `boundary` must be
power-of-two, at least `PAGE_SIZE`, and `bytes` must not exceed it.

### `dma_free_pages(addr, bytes)`
Frees the contiguous range backing the allocation.
Accepts a physical base address (no HHDM translation is performed).
