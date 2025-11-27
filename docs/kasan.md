# Kernel KASAN (shadow memory) – overview

This KASAN-style implementation adds a shadow memory region to catch some classes of memory-safety bugs (heap/page use-after-free, simple over/underruns) when building with `KASAN=1`.

## How it works

- Shadow memory: a parallel address space where each shadow byte summarizes 8 bytes of real memory. Accesses consult the shadow to decide if the target bytes are addressable; poisoning/unpoisoning shadow bytes encodes validity.
- Poisoning: writing special tag values into shadow to mark real bytes as invalid (e.g., freed) or valid. An access to a poisoned region triggers a KASAN report; unpoisoning marks the region addressable again.
- Shadow layout: each shadow byte covers 8 bytes of real memory (`KASAN_SHADOW_SCALE_SHIFT = 3`), located at `KASAN_SHADOW_OFFSET = 0xffff900000000000`.
- Coverage: currently limited to the first 1GiB of physical memory as mapped through the HHDM. The covered real address range is `[HHDM, HHDM + 1GiB)`. 
- Early init: after paging is set up, `kasan_early_init` maps the shadow region, zeros it, and marks itself ready. The PMM exposes `pmm_get_highest_addr` to size coverage.
- Poisoning:
  - PMM: `pmm_alloc_page(s)` unpoisons the corresponding HHDM mapping; `pmm_free_page(s)` poisons it with `0xFF` (non-addressable).
  - Heap: slab/big allocations add 16-byte redzones on both sides (`KASAN_REDZONE_SIZE`) and unpoison only the user region on alloc; redzones stay poisoned and the whole slot is poisoned again on free. Slab pages start poisoned.
- Checking: `memcpy`, `memset`, and `memcmp` consult `kasan_check_range` when KASAN is ready. On invalid access, `kasan_report` panics with a short message.
- Usercopy helper: `safe_copy` (used for syscall paths) checks the destination range when KASAN is ready; user pointers outside the covered region are skipped by design.
- Tests: `kernel/tests/kasan_test.c` verifies that the shadow is initialized and heap ranges are treated as accessible.

## Usage

- Build/run with KASAN: `make tests`
