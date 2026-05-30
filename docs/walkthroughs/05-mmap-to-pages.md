# An anonymous mmap to zeroed pages (and why a stray fault kills the process)

This walkthrough traces one `mmap(MAP_ANONYMOUS)` call from the syscall entry
down to physical frames being allocated, zeroed, and inserted into the page
tables. The defining choice in this kernel is that the mapping is **eager**:
every page is backed by real physical memory at `mmap()` time. There is no
demand paging and no fault-driven population. That single fact decides what
happens later when a thread touches an address the kernel never mapped, which
is why the page-fault handler in `idt.c` is the natural end of this story.

## Scope

This covers the `MAP_ANONYMOUS` path of `sys_mmap` only. The file-backed,
shared-memory, and framebuffer paths exist in the same file but are not
demand-paged either and are mentioned only where they share machinery. It does
not cover `munmap`, `mprotect`, or copy-on-write (none of which do lazy fault
handling here).

## Files in play

- `include/mem/vmm.h` — PTE flag bits (`PTE_PRESENT`/`PTE_USER`/`PTE_WRITABLE`/…), the `pml4_t` type, and the HHDM helpers `phys_to_virt`/`virt_to_phys`.
- `kernel/syscalls/sys_mmap.c` — `sys_mmap` argument decoding, `mmap_find_base` address picking, and the dispatch to the anonymous path.
- `kernel/syscalls/common.c` — `map_user_anonymous_range`, the real per-page loop (alloc, zero, map, verify, then register one VMA), with full unwind on failure. Despite the generic filename, this is *the* anonymous-backing primitive.
- `kernel/mem/pmm.c` — `pmm_alloc_page` / `pmm_free_page`, the bitmap frame allocator that hands out the physical pages.
- `kernel/mem/vmm.c` — `vmm_map_page` walks/creates the 4-level tables and writes the leaf PTE; `vmm_virt_to_phys` reads it back for the post-map verify.
- `kernel/task/process.c` — `vm_area_add` and the per-process VMA list (`vm_areas`) that records what *should* be mapped.
- `include/task/process.h` — `vm_area_t` struct and the `VMA_*` flag bits.
- `kernel/arch/x86_64/idt.c` — `interrupt_handler`, the vector-14 (#PF) path that turns a user fault into a kill.

## The walk

1. **Decode and validate the request.** `sys_mmap` (`kernel/syscalls/sys_mmap.c:166`)
   rejects a zero length immediately, then derives `vma_flags` from `prot`:
   it always starts with `VMA_USER | VMA_MMAP` and ORs in `VMA_READ`/`VMA_WRITE`/`VMA_EXEC`
   per the `PROT_*` bits (`sys_mmap.c:175`-`181`). For an anonymous request it
   also requires exactly one of `MAP_SHARED`/`MAP_PRIVATE` and `offset == 0`
   (`sys_mmap.c:184`-`187`), rounds the length up to a page multiple with
   `align_up`, checks for overflow, and sets `VMA_ANON` (`sys_mmap.c:188`-`191`).

2. **Pick a base address.** `mmap_find_base(addr, total_len)` (`sys_mmap.c:17`)
   page-aligns the hint (defaulting to `0x4000000000`, the mmap window from the
   address-space layout) and then scans the process VMA list, bumping `base`
   past any overlapping area until it finds a free gap, capping at
   `0x7FFFFFFFF000` (`sys_mmap.c:39`-`44`). The scan holds
   `current_process->vm_lock` (`sys_mmap.c:26`-`36`). This is a linear search,
   not a tree, so placement cost grows with the number of existing mappings.

3. **The `PROT_NONE` short-circuit.** If `prot == PROT_NONE`, `sys_mmap` registers
   a VMA covering the range but maps **no** pages and returns the base
   (`sys_mmap.c:197`-`201`). This is the one anonymous case where the VMA exists
   without backing memory — and touching it faults, exactly as intended for a
   guard/reservation. It is worth seeing first because it shows the VMA list and
   the page tables are independent bookkeeping.

4. **Hand off to the eager backer.** For a real `prot`, `sys_mmap` calls
   `map_user_anonymous_range(current_process, current_process->pml4, base, total_len, vma_flags)`
   (`sys_mmap.c:204`) and, on success, returns `base` to userspace
   (`sys_mmap.c:206`). Everything load-bearing happens inside this one call.

5. **Re-validate alignment and overflow.** `map_user_anonymous_range`
   (`kernel/syscalls/common.c:254`) rejects null `proc`/`pml4`, a zero length,
   a misaligned `start` or `length`, and an `end` that wraps
   (`common.c:256`-`262`). These checks are duplicated here rather than trusted
   from the caller precisely because this function has four callers (see the
   Gotchas) and must stand on its own.

6. **The per-page loop: allocate.** For each page in `[start, end)`,
   `pmm_alloc_page()` (`common.c:266`; defined at `kernel/mem/pmm.c:154`) returns
   a free physical frame. The allocator scans its bitmap from a `next_free_hint`
   cursor and wraps once, skipping any frame the slab heap claims, and returns
   `nullptr` when memory is exhausted (`pmm.c:161`-`181`). A null result jumps to
   the `fail` unwind (`common.c:267`-`268`).

7. **The per-page loop: zero through the HHDM.** The freshly allocated frame is a
   *physical* address; the kernel cannot dereference it directly. It is zeroed
   via the Higher Half Direct Map: `memset((void *)((uint64_t)phys + g_hhdm_offset), 0, PAGE_SIZE)`
   (`common.c:270`). `g_hhdm_offset` is the same constant behind
   `phys_to_virt` in `include/mem/vmm.h:34`. Zeroing here is what makes anonymous
   memory read as zero and prevents leaking a previous owner's data — there is no
   pre-zeroed page pool, so every anonymous page pays a `memset` at map time.

8. **The per-page loop: map it.** `vmm_map_page(pml4, virt, (uint64_t)phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE)`
   (`common.c:271`) installs the leaf entry. Inside `vmm_map_page`
   (`kernel/mem/vmm.c:69`) the virtual address is split into its four 9-bit
   indices, intermediate tables are created on demand (`get_next_level(..., true)`),
   the leaf PTE is written as `phys | flags` (`vmm.c:90`), and the entry is
   flushed with `invlpg` (`vmm.c:95`). Note the flags are hardcoded
   `present|user|writable` regardless of the `prot` the user asked for — see the
   Gotchas; the `VMA_*` protection bits are recorded but not enforced in the PTE.

9. **The per-page loop: verify the map took.** Immediately after mapping,
   `vmm_virt_to_phys(pml4, virt)` (`common.c:272`; defined at `vmm.c:303`) reads
   the entry back. `vmm_map_page` returns `void` and silently does nothing if an
   intermediate-table allocation fails (`vmm.c:79`/`83`/`87`), so this read-back
   is the only signal that the mapping actually exists. If it reads zero the page
   is freed and the loop unwinds (`common.c:272`-`275`). `mapped_end` advances one
   page per success so the unwind knows exactly how far it got (`common.c:277`).

10. **Register exactly one VMA.** After every page is mapped,
    `vm_area_add(proc, start, end, vma_flags)` (`common.c:280`; defined at
    `kernel/task/process.c:84`) records the whole range as a single
    `vm_area_t { start, end, flags }` appended to `proc->vm_areas` under
    `vm_lock`, re-checking for overlap first (`process.c:91`-`97`). The VMA is
    bookkeeping for `fork`/`munmap`/placement — it is **not** consulted on a page
    fault. If `vm_area_add` fails (overlap or out of heap), the whole range
    unwinds (`common.c:280`-`281`).

11. **Failure unwinding.** The `fail` label walks `[start, mapped_end)`, and for
    each page reads its physical address back with `vmm_virt_to_phys`, frees it
    via `pmm_free_page`, and clears the PTE with `vmm_unmap_page`
    (`common.c:285`-`292`). This is why `mapped_end` (not `end`) bounds the loop:
    only pages that were actually installed get freed, so a mid-loop OOM neither
    leaks frames nor double-frees. On success the function returns `true` and the
    user gets `base`.

12. **Later: a stray access to an unmapped address.** Suppose the program reads or
    writes an address with no PTE — past the end of its mmap, into a `PROT_NONE`
    reservation, or into a stack guard page. The CPU raises vector 14. In
    `interrupt_handler` (`kernel/arch/x86_64/idt.c:310`) there is **no** registered
    `isr_handlers[14]`, so control falls into the generic `frame->int_no < 32`
    branch (`idt.c:315`). For vector 14 it reads `CR2` (the faulting address) only
    to print it (`idt.c:322`-`325`); it never looks at `current_process->vm_areas`
    and never tries to allocate or map a page. There is nothing to "fault in"
    because mapping was already eager.

13. **The fault becomes a kill.** Because the fault came from user mode
    (`(snap->cs & 0x3) != 0`, `idt.c:343`), the handler captures crash info
    (`idt.c:360`-`363`), looks up the signal for the vector — vector 14 maps to
    `SIGSEGV` in `vector_to_signal` (`idt.c:98`, used at `idt.c:365`-`367`) —
    marks the process exited with code `128 + sig` under `scheduler_lock`
    (`process_mark_exited_locked`, `idt.c:375`), wakes the parent, closes the
    FDs, and calls `schedule()` (`idt.c:391`), which never returns to the faulting
    context. A user page fault is therefore always fatal on this path; there is no
    "the fault populated the page, retry the instruction" outcome.

## Gotchas

- **No demand paging — the design choice that defines everything.** A first-time
  reader expects the #PF handler to consult the VMA list and lazily allocate. It
  does not (`idt.c:322`-`394` never touches `vm_areas`). Mapping is eager in
  `map_user_anonymous_range`, so any fault to an unmapped page is a real bug in
  the program and is killed, not serviced.
- **`common.c` is misleadingly generic, and this primitive backs stacks too.**
  `map_user_anonymous_range` has four callers: `sys_mmap` (`sys_mmap.c:204`),
  `sys_execve` for the initial user stack (`sys_execve.c:237`),
  `sys_thread_create` for thread stacks (`sys_thread_create.c:140`), and
  `sys_spawn` (`sys_spawn.c:177`). The same zero-fill-and-map code that serves
  `mmap()` also lays down every user stack.
- **PTE protection ignores `prot`.** `vmm_map_page` is always called with
  `PTE_PRESENT | PTE_USER | PTE_WRITABLE` (`common.c:271`). The `VMA_READ/WRITE/EXEC`
  bits derived from `prot` are stored in the `vm_area_t` but never written into
  the hardware PTE, so an anonymous mapping made `PROT_READ` only is still
  writable in practice. `PROT_NONE` is the exception, but only because it skips
  mapping entirely (`sys_mmap.c:197`).
- **`vmm_map_page` returns `void` and can silently fail.** Mid-table allocation
  failures bail out with no error (`vmm.c:79`/`83`/`87`). The explicit
  `vmm_virt_to_phys` read-back at `common.c:272` is the only thing that catches
  this — without it a "successful" mmap could hand back addresses with no PTE.
- **Two independent ledgers.** The page tables (hardware truth, consulted by the
  CPU) and `proc->vm_areas` (software bookkeeping, consulted by placement/`fork`/
  `munmap`) are kept in sync only by convention. The `PROT_NONE` case
  (`sys_mmap.c:197`-`201`) and the stack guard pages (`sys_execve.c:245`-`246`,
  `sys_thread_create.c:148`-`149`) deliberately add a VMA with no backing pages.

## See also

- `docs/address_space.md` — the user/kernel split, the `0x4000000000` mmap window, and where stacks/heap live.
- `docs/vmm.md` — page-table walking and the HHDM in depth.
- `docs/pmm.md` — the bitmap frame allocator behind `pmm_alloc_page`.
- `docs/signals.md` — how the `SIGSEGV` raised on a user fault is delivered/handled.
- Acronyms (HHDM, PML4, PTE, PMM, VMM, VMA, #PF) are defined in `docs/glossary.md`.
