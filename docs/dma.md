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
`boundary` is in bytes. A value of 0 disables boundary checks.
Both values must be power-of-two and at least `PAGE_SIZE`.
If `bytes` exceeds `boundary`, the allocation fails.

### `dma_free_pages(addr, bytes)`
Frees the contiguous range backing the allocation.
Accepts either a physical base or an HHDM-mapped address.
