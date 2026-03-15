# Address Space and Memory Layout

---

## Basics

- **Page size**: 4 KiB (`PAGE_SIZE`).
- **Canonical addresses**: 48-bit; the kernel uses the higher half.
- **PML4 split**: entries 0-255 are user space, 256-511 are kernel space.
- **HHDM**: a higher-half direct map, `virt = phys + g_hhdm_offset`.
- **Syscall user pointers**: validated for canonical addressing and user-mapped PTE permissions.

---

## Kernel Virtual Address Space

- **Kernel image base**: `0xFFFFFFFF80000000` (see `linker.ld`).
- **HHDM base**: `g_hhdm_offset` from Limine (tests expect `0xFFFF800000000000`).
- **Direct map**: physical memory is accessed via the HHDM; PMM, heap, DMA, and
  MMIO helpers use it instead of a separate vmalloc region.
- **Kernel heap**: slab + big allocations are backed by physical pages and
  returned as HHDM-mapped pointers.
- **Kernel stacks**: per-thread `KSTACK_SIZE` (64 KiB) with
  `KSTACK_SYSCALL_HEADROOM` (512 B) reserved at the top for syscall entry
  pushes; `tss_set_stack()` programs the stack on context switch.
- **Bootstrap stack**: a 4 KiB static stack (`bootstrap_stack`) is used as the
  BSP's initial syscall entry `kernel_rsp` (TSS RSP0) until a per-thread stack
  is configured.

---

## User Virtual Address Space

- **Program base**: user binaries are linked at `0x0000000000400000`.
- **ELF segments**: `elf_load()` maps `PT_LOAD` segments at their `p_vaddr`.
- **Heap (`sbrk`)**: initial `heap_end` is the max loaded vaddr; grows upward
  and is page-mapped on demand.
- **`mmap` base**: default search starts at `0x0000004000000000` and grows up.
  Mappings are kept below `0x00007FFFFFFFF000`. Shared file-backed mappings
  are supported for `/dev/fb0` and shared memory objects (`shm_open`).
- **Stacks**:
  - `execve`/`spawn`: top is `0x00007FFFFFFFF000`, 4 pages of stack plus a
    1-page guard below.
  - `sys_thread_create`: same size; stacks are placed by scanning downward
    from `0x00007FFFFFFFF000` (or below the current user RSP) and never below
    `0x0000000000400000`.
  - `init` process: 4 pages without a guard page.
- **Guard pages**: tracked in VMAs but left unmapped.

---

## Address Space Construction

- `vmm_new_pml4()` allocates a new PML4 and copies the kernel half
  (entries 256-511) from the current CR3.
- `vmm_copy_pml4()` deep-copies the user half for `fork`.
- `vmm_switch_pml4()` switches CR3 to change address spaces.

---

## Typical Layout (Virtual)

```
User (PML4 0-255)
0x0000000000400000   ELF text/segments (linker -Ttext)
...                  heap (sbrk) grows up from max_vaddr
0x0000004000000000   mmap base (anon + fb), grows up
...                  gap
0x00007FFFFFFFF000   user stack top (grows down)
0x0000800000000000   canonical boundary / kernel half starts

Kernel (PML4 256-511)
0xFFFF800000000000   HHDM base (g_hhdm_offset)
...                  direct map of physical memory
0xFFFFFFFF80000000   kernel image (.text/.rodata/.data/.bss)
0xFFFFFFFFFFFFFFFF   end
```

---

## Reference Pointers

- Kernel base: `linker.ld`
- HHDM offset: `kernel/boot.c`, `kernel/mem/vmm.c`
- User stack/heap/mmap: `kernel/syscalls/sys_execve.c`,
  `kernel/syscalls/sys_spawn.c`, `kernel/syscalls/sys_thread_create.c`,
  `kernel/syscalls/sys_sbrk.c`, `kernel/syscalls/sys_mmap.c`
- Process/thread fields: `include/task/process.h`
