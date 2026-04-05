# Dynamic Linking

This document describes the dynamic linking implementation that exists in this
tree today. It is not a generic ELF overview. Every step below is tied to the
current code under `kernel/`, `user/`, and `scripts/`.

## Scope

The current implementation covers:

- Dynamically linked user executables with a `PT_INTERP` segment
- A custom runtime linker at `/lib/ld.so`
- Shared libraries placed in `/lib`
- Eager relocation at process startup
- Enough TLS support for the current initial-exec usage in shared code

It does not implement the full feature set of a desktop Unix dynamic linker.
The exact limits are listed near the end.

## Main pieces

Build-time pieces:

- `user/user_dyn.ld`
  - Linker script for dynamically linked executables
- `user/user_shared.ld`
  - Linker script for shared libraries
- `user/rtld/rtld.ld`
  - Linker script for `ld.so`
- `user/Makefile`
  - Builds `ld.so`, `libc.so`, `libwm.so`, `libelf.so`, and selected dynamic
    executables

Kernel-side pieces:

- `include/lib/elf.h`
  - Shared ELF, dynamic-tag, relocation, and auxv definitions
- `kernel/lib/elf.c`
  - Extended ELF loader with `PT_INTERP` support
- `kernel/syscalls/sys_execve.c`
  - Builds the initial user stack and auxv
- `kernel/syscalls/sys_spawn.c`
  - Same handoff for spawned processes

User-side pieces:

- `user/rtld/rtld_start.S`
  - `ld.so` bootstrap and self-relocation
- `user/rtld/rtld.c`
  - DSO management, symbol lookup, relocation, library loading
- `user/rtld/rtld_syscall.S`
  - Raw syscall wrappers used before libc exists
- `user/libc/src/start.S`
  - Entry point used by dynamically linked programs after `ld.so` finishes
- `user/libc/src/tls.c`
  - TLS setup used by shared libc code

Tests and tools:

- `kernel/tests/elf_dynlink_test.c`
- `user/tests/dynlink_test.c`
- `user/ldd.c`

## High-level flow

At a high level, a dynamic program starts like this:

```text
execve/spawn
  -> kernel elf_load()
     -> map executable PT_LOAD segments
     -> read PT_INTERP
     -> load /lib/ld.so at a fixed high base
     -> build initial stack with auxv
     -> jump to ld.so entry

ld.so _start
  -> self-relocate ld.so
  -> parse auxv
  -> parse main executable PT_DYNAMIC
  -> load each DT_NEEDED library from /lib
  -> relocate all loaded DSOs eagerly
  -> jump to the program entry point

program _start
  -> __tls_init()
  -> main()
  -> exit()
```

The executable is still loaded at its normal fixed address range. The runtime
linker is the part that is position-independent and loaded at a separate bias.

## What gets built

`user/Makefile` splits userland into static and dynamic outputs.

Dynamic linking is enabled by these pieces:

- `DYN_LDFLAGS`
  - `-Wl,-T,user_dyn.ld`
  - `-Wl,--build-id=none`
  - `-Wl,-dynamic-linker,/lib/ld.so`
  - `-Wl,--hash-style=sysv`
- Shared libraries are linked with:
  - `-shared`
  - `-soname <name>`
  - `--hash-style=sysv`
  - `-T user_shared.ld`
- `ld.so` is linked with:
  - `-shared`
  - `-e _start`
  - `--no-undefined`
  - `--hash-style=sysv`
  - `-soname ld.so`
  - `-T rtld/rtld.ld`

Important consequences:

- The runtime linker expects SysV hash tables (`DT_HASH`), not GNU hash lookup.
- Dynamic executables embed `/lib/ld.so` in `PT_INTERP`.
- Shared objects are ET_DYN.
- The main executable still uses the fixed base from `user_dyn.ld`:
  `0x400000`.

The Makefile's default rule dynamically links all userland programs except
three static exceptions:

- `cat` -- statically linked as an intentional example
- `tls_test` -- statically linked because it uses `thread_local` in the
  executable and the runtime linker lacks per-module TLS
- `user_prog` -- assembly program, statically linked

Programs that need additional shared libraries have explicit rules:

- GUI programs (`calculator`, `term`, `wmclient_demo`) link `libc.so` + `libwm.so`
- ELF tools (`addr2line`, `objdump`, `ldd`) link `libc.so` + `libelf.so`
- The window manager (`wm/main`) links `libc.so` + `libwm.so` + `libelf.so`

Everything else uses the default rule, which links against `libc.so` only.

Dynamic outputs also include:

- `/lib/ld.so`
- `/lib/libc.so`
- `/lib/libwm.so`
- `/lib/libelf.so`
- `/tests/dynlink_test`

`scripts/make_image.sh` copies those shared objects into the rootfs under
`/lib`, and user binaries into `/bin` and `/tests`.

## ELF layout choices

### Dynamic executables

`user/user_dyn.ld` emits the sections and program headers needed for dynamic
linking:

- `PT_INTERP`
- `PT_LOAD` (text, rodata, data)
- `PT_DYNAMIC`
- `PT_TLS`
- dynamic sections such as:
  - `.interp`
  - `.dynsym`
  - `.dynstr`
  - `.rela.dyn`
  - `.rela.plt`
  - `.dynamic`
  - `.got`
  - `.got.plt`

The file is laid out at a fixed virtual base:

```text
0x400000   executable image base
```

That means this design is "dynamic libraries + runtime linker", not a fully
position-independent main executable.

### Shared libraries

`user/user_shared.ld` lays out ET_DYN shared objects. Their addresses are all
relative to load bias. The runtime linker chooses the actual load address at
runtime.

### The runtime linker

`user/rtld/rtld.ld` also builds `ld.so` as ET_DYN. Unlike normal shared
libraries, the kernel gives it a fixed load bias:

```text
INTERP_LOAD_BASE = 0x7000000000
```

The kernel maps each `ld.so` `PT_LOAD` segment at:

```text
INTERP_LOAD_BASE + p_vaddr
```

The runtime linker entry point given to userspace is:

```text
interp_entry = INTERP_LOAD_BASE + e_entry
```

## Kernel loader: `elf_load()`

The kernel-side entry point for dynamic-capable loading is
`kernel/lib/elf.c:elf_load()`.

It does four jobs:

1. Load the main executable
2. Detect whether the executable is dynamic
3. Prepare metadata needed by `ld.so`
4. Load the interpreter itself when `PT_INTERP` exists

### Step 1: open and validate the executable

The kernel:

- resolves the path with `vfs_resolve_path()`
- reads the ELF header
- checks the ELF magic
- reads the program header table

The shared result type is `elf_load_result_t` in `include/lib/elf.h`.

That structure carries:

- `entry`
  - the executable's own `e_entry`
- `max_vaddr`
  - highest mapped address used by `PT_LOAD`
- `phdr_vaddr`
  - where the program header table is visible in user memory
- `phent`
  - size of one program header
- `phnum`
  - number of program headers
- `interp`
  - interpreter path string from `PT_INTERP`
- `interp_base`
  - load bias used for the interpreter
- `interp_entry`
  - actual RIP the kernel should jump to for dynamic programs

### Step 2: scan for `PT_INTERP` and `PT_PHDR`

Before mapping segments, `elf_load()` scans the executable program headers:

- `PT_INTERP`
  - reads the interpreter path, usually `/lib/ld.so`
- `PT_PHDR`
  - records where the program header table should appear in memory

`ld.so` later receives that data through auxv.

### Step 3: map the executable `PT_LOAD` segments

Each loadable segment is mapped by `elf_load_segment()`.

What the kernel does today:

- computes the segment virtual range
- page-aligns the start and end
- allocates physical pages eagerly
- maps them into the target user page table
- copies file contents into the mapped pages
- zero-fills the rest of each page, including BSS tails

Important implementation detail:

- the current loader maps executable pages with
  `PTE_PRESENT | PTE_WRITABLE | PTE_USER`
- it does not currently translate ELF `p_flags` into final page permissions
- in other words, text, rodata, and data all start writable from the kernel's
  point of view

That keeps the current loader simple, but it also means this is not enforcing
W^X or RELRO-like protection yet.

### Step 4: ensure `AT_PHDR` will be valid

`ld.so` needs to inspect the main executable's program headers, so the kernel
must provide a valid in-memory address for them.

The loader tries, in order:

1. Use the explicit `PT_PHDR` virtual address if present
2. If there is no `PT_PHDR`, infer the in-memory address from the `PT_LOAD`
   segment that covers `e_phoff`
3. If neither works and the binary is dynamic, map one page manually and copy
   the program headers there

That manual fallback is placed just below the interpreter region:

```text
0x7000000000 - 0x1000 = 0x6ffffff000
```

This exists only to guarantee that `AT_PHDR` points at readable data when
`ld.so` starts.

### Step 5: load the interpreter

If `PT_INTERP` was found, the kernel opens that file and repeats the normal
`PT_LOAD` mapping logic for it, but with a nonzero bias:

```text
bias = 0x7000000000
```

The interpreter itself is not relocated by the kernel. The kernel only maps its
segments and computes:

- `interp_base`
- `interp_entry`

The actual relocation work is left to `ld.so`.

## Process handoff: `execve()` and `spawn()`

Both program launch paths use `elf_load()`:

- `kernel/syscalls/sys_execve.c`
- `kernel/syscalls/sys_spawn.c`

Their logic differs in details, but the dynamic-linking contract is the same.

### Entry point choice

For a static binary:

- `entry = elf_result.entry`

For a dynamic binary:

- `entry = elf_result.interp_entry`

So the kernel does not enter the application directly. It enters `ld.so`, and
the application's actual entry point is passed separately through auxv.

### Heap initialization

Both paths set:

```text
process->heap_end = elf_result.max_vaddr
```

This means the process heap starts after the highest mapped executable or
interpreter address the loader has seen.

### User stack layout

The initial stack follows the SysV x86_64 ABI shape:

```text
low addresses
  argc
  argv[0]
  argv[1]
  ...
  argv[argc - 1]
  NULL
  envp[0] = NULL
  auxv[0].a_type
  auxv[0].a_val
  ...
  AT_NULL
  0
  argument strings
high addresses
```

The auxv is pushed bottom-up, so `AT_NULL` is written first and ends up last in
logical order.

### Auxv entries that exist today

The kernel currently pushes exactly these entries:

- `AT_PHDR`
  - address of the main executable's program header table in memory
- `AT_PHENT`
  - `sizeof(Elf64_Phdr)`
- `AT_PHNUM`
  - number of program headers
- `AT_ENTRY`
  - the main executable's entry point, not `ld.so`'s entry point
- `AT_BASE`
  - interpreter load bias, or `0` for static binaries
- `AT_PAGESZ`
  - `4096`
- `AT_NULL`
  - terminator

Notably absent right now:

- real `envp` entries
- `AT_EXECFN`
- `AT_RANDOM`
- uid/gid auxv entries
- hardware capability entries

`execve()` currently places only a null `envp[]`. `spawn()` places both empty
`argv[]` and empty `envp[]`.

### `execve()` specifics

`sys_execve()`:

- copies in user arguments
- builds a fresh address space
- loads the binary and possibly the interpreter
- allocates the user stack
- switches to the new page table
- writes the stack directly in the current context
- sets `regs->rcx` to either the interpreter entry or the program entry

It also clears the thread FS base, so the new image starts with no inherited TLS
state. The new program will rebuild TLS in `_start`.

### `spawn()` specifics

`sys_spawn()` creates another process, so it cannot just write the child stack
through the current address space.

Instead it:

- creates the target page table first
- uses `vmm_virt_to_phys()` plus the HHDM to write into the child stack
- stores the same auxv contract there
- creates a user thread that starts at the interpreter entry for dynamic images

The child gets:

- `argc = 0`
- `argv[] = { NULL }`
- `envp[] = { NULL }`

## `ld.so` bootstrap

The kernel has mapped `ld.so`, but `ld.so` cannot immediately run normal C code.
As an ET_DYN image, its own GOT and other absolute references still need
relocation.

That is why the bootstrap is split into:

- `user/rtld/rtld_start.S`
- `user/rtld/rtld.c`
- `user/rtld/rtld_syscall.S`

### Why the bootstrap is in assembly

Before self-relocation:

- GOT entries may still contain link-time placeholders
- PLT calls are not safe
- global variables are not safe if they rely on relocations

So the first stage uses only:

- raw register manipulation
- RIP-relative addressing
- direct memory walks

### Step 1: find `AT_BASE`

`rtld_start.S` begins with the original userspace stack that the kernel built.

It:

- saves the original `rsp` in `rbp`
- walks past `argc`
- skips `argv[]`
- skips `envp[]`
- scans auxv until it finds `AT_BASE`

`AT_BASE` is the interpreter load bias. The code stores it in `r12`.

### Step 2: find `_DYNAMIC` without using the GOT

The assembly does:

```text
lea r13, [rip + _DYNAMIC]
```

This works before relocation because RIP-relative references within the same
image do not depend on GOT setup.

### Step 3: locate `DT_RELA`, `DT_RELASZ`, `DT_RELAENT`

The bootstrap walks the linker-provided `_DYNAMIC` array and extracts:

- `DT_RELA`
- `DT_RELASZ`
- `DT_RELAENT`

These describe `ld.so`'s own relocation table.

### Step 4: self-apply `R_X86_64_RELATIVE`

The bootstrap then iterates all `RELA` entries and applies only relocations of
type `R_X86_64_RELATIVE`.

Formula:

```text
*target = base + addend
```

That is enough to repair the runtime linker's own internal absolute references,
including its GOT entries.

No symbol lookup happens here. Only relative relocations are handled in this
stage.

### Step 5: enter C code

Once self-relocation is done, `rtld_start.S`:

- aligns the stack for the C ABI
- calls `rtld_main(original_sp)`
- restores the original application stack afterwards
- clears `rbp`
- jumps to the entry address returned by `rtld_main()`

That return value is the main executable's `_start`.

## Why `ld.so` has its own syscall layer

`ld.so` cannot use libc. libc itself lives in `libc.so`, and `libc.so` is one
of the things `ld.so` must load and relocate.

So `user/rtld/rtld_syscall.S` provides raw wrappers for:

- `write`
- `read`
- `exit`
- `open`
- `close`
- `mmap`

The C file wraps those with tiny helpers like:

- `rtld_write()`
- `rtld_open()`
- `rtld_read()`
- `rtld_mmap()`

The build uses `-fvisibility=hidden` for the runtime linker. That matters
because early calls must stay local and PC-relative instead of going through
PLT/GOT machinery.

`rtld.c` also reimplements a very small subset of libc:

- `strlen`
- `strcmp`
- `memcpy`
- `memset`

There is no allocator. DSO descriptors come from a fixed static array.

## DSO bookkeeping

The runtime linker represents every loaded image with `dso_t` in `user/rtld/rtld.h`.

Each `dso_t` stores:

- `name`
- `base`
  - load bias, `0` for the main executable
- `dynamic`
  - `PT_DYNAMIC` pointer in memory
- `symtab`
- `strtab`
- `hash`
  - SysV hash table
- `rela`
- `rela_count`
- `jmprel`
- `jmprel_count`
- `nsyms`
  - taken from `DT_HASH` `nchain`
- `next`

The loader uses:

- a static pool `dso_pool[RTLD_MAX_DSO]`
- a singly-linked load-order list
- a hard limit of `RTLD_MAX_DSO == 16`

If that limit is exceeded, `ld.so` aborts the process.

## Parsing the main executable

`rtld_main()` starts by parsing auxv into `rtld_auxv_t`.

From there it builds a `dso_t` for the main executable:

- `name = "<main>"`
- `base = 0`

It then finds the executable's `PT_DYNAMIC` by walking the program header table
that auxv exposed through `AT_PHDR`, `AT_PHNUM`, and `AT_PHENT`.

Once `PT_DYNAMIC` is found, `dso_parse_dynamic()` extracts:

- `DT_SYMTAB`
- `DT_STRTAB`
- `DT_HASH`
- `DT_RELA`
- `DT_RELASZ`
- `DT_RELAENT`
- `DT_JMPREL`
- `DT_PLTRELSZ`

The loader uses `DT_HASH` only to get the symbol count. It does not actually
perform hashed lookup. Symbol resolution is still a linear scan.

## Loading shared libraries

After the main executable is registered, `rtld_main()` walks the executable's
dynamic section and loads one library for each `DT_NEEDED`.

### Search path

The current search rule is simple:

```text
/lib/<name from DT_NEEDED>
```

There is no support for:

- `RPATH`
- `RUNPATH`
- alternate search directories
- environment-controlled lookup

### Library mapping algorithm

`rtld_load_library()` does this:

1. Open `/lib/<name>`
2. Read the ELF header
3. Read program headers
4. Find the lowest and highest `PT_LOAD` virtual address
5. Round that span to page boundaries
6. Allocate one contiguous anonymous mapping with `mmap()`
7. Set `load_bias = mapping_base - lowest_aligned_vaddr`
8. Zero the whole mapping
9. Copy each `PT_LOAD` file range into `load_bias + p_vaddr`
10. Locate `PT_DYNAMIC`
11. Parse it and append the new DSO to the global list

### Why the code reopens files repeatedly

The runtime linker currently has raw wrappers for `open`, `read`, and `close`,
but not `lseek`.

So library loading takes this approach:

- reopen the file
- read and discard bytes until `p_offset`
- read the segment payload

This is not fast, but it keeps the runtime linker independent from libc and
from any richer file API.

### Mapping permissions

Libraries are currently mapped with one anonymous region created as:

- `PROT_READ | PROT_WRITE | PROT_EXEC`
- `MAP_PRIVATE | MAP_ANONYMOUS`

That means:

- no file-backed mappings
- no shared physical pages between processes
- no per-segment final protection changes
- no RELRO or W^X policy yet

### Dependency depth

`rtld_main()` only walks `DT_NEEDED` entries from the main executable.

It does not recursively inspect each loaded library for more `DT_NEEDED`
entries.

This matters. It means transitive dependency loading is not general-purpose.
Today that works because the build intentionally links programs against the
libraries they need directly.

## Symbol resolution

`rtld_resolve_symbol()` implements symbol lookup.

The search order is the current DSO load order:

1. main executable
2. first `DT_NEEDED` library
3. second `DT_NEEDED` library
4. and so on

The first matching symbol wins.

Rules used today:

- skip the DSO passed in `skip`
  - used by copy relocations
- skip DSOs with missing symbol metadata
- skip undefined symbols (`st_shndx == SHN_UNDEF`)
- skip local symbols (`STB_LOCAL`)
- accept non-local defined symbols

Consequences:

- lookup is `O(number of DSOs * symbols per DSO)`
- SysV hash is not used to accelerate lookup
- unresolved weak symbols are not treated specially
- there is no symbol versioning
- there is no namespace or scope model beyond plain load order

If a required symbol cannot be resolved for most relocation types, `ld.so`
prints an error and exits with status `127`.

## Relocation model

All dynamic relocation work happens in userspace inside `ld.so`.

The runtime linker eagerly processes:

- `DT_RELA`
  - non-PLT relocations
- `DT_JMPREL`
  - PLT relocations

There is no lazy binding.

### Order of relocation

`rtld_main()` relocates:

1. all libraries first
2. the main executable last

That order is important for copy relocations, because the executable may need
initial data copied from an already-relocated library definition.

### Supported relocation types

The implementation in `rtld_apply_rela()` currently supports:

- `R_X86_64_NONE`
  - ignore
- `R_X86_64_RELATIVE`
  - `*P = B + A`
- `R_X86_64_GLOB_DAT`
  - `*P = S`
- `R_X86_64_JUMP_SLOT`
  - `*P = S`
- `R_X86_64_64`
  - `*P = S + A`
- `R_X86_64_COPY`
  - copy initial data bytes from a shared object into the executable's storage
- `R_X86_64_TPOFF64`
  - limited TLS offset handling for the current startup model

Where:

- `P`
  - relocation target
- `B`
  - DSO base/load bias
- `S`
  - resolved symbol value
- `A`
  - relocation addend

Unsupported relocation types abort immediately.

### `R_X86_64_RELATIVE`

This is used heavily for ET_DYN objects. It needs no symbol lookup:

```text
*target = dso_base + addend
```

It is also the only relocation handled during `ld.so` self-bootstrap.

### `R_X86_64_GLOB_DAT` and `R_X86_64_JUMP_SLOT`

These both resolve a symbol by name and write the resolved address directly into
the target slot.

This handles:

- GOT entries for imported data/functions
- PLT slots for function calls

Because all PLT relocations are processed before control reaches the program,
function calls are fully bound up front.

### `R_X86_64_64`

This is direct absolute relocation:

```text
*target = symbol_address + addend
```

### `R_X86_64_COPY`

This handles the classic executable-data-copy case:

- the executable reserves storage for a global
- the actual definition lives in a shared library
- startup copies the library's initial bytes into the executable slot

The loader deliberately skips the executable itself when resolving the source
symbol, so it finds the library definition instead of the destination object.

### `R_X86_64_TPOFF64`

The runtime linker contains limited support for `R_X86_64_TPOFF64`.

What it does today:

- if `sym_idx == 0`, write the addend directly
- otherwise try to resolve the symbol name
- if resolution succeeds, write `resolved + addend`
- if resolution fails, fall back to `sym->st_value + addend`

This is enough for the current shared-lib TLS usage tested by `errno`, but it
is not a full dynamic TLS implementation with module IDs, DTV management, or
late TLS allocation for new DSOs.

## Startup after relocation

Once `rtld_main()` finishes, it returns `AT_ENTRY`, which is the main
executable's entry point.

`rtld_start.S` then restores the original process stack and jumps straight to
that address.

For normal C programs that address is the `_start` symbol from
`user/libc/src/start.S`.

That startup code:

- reads `argc` from `[rsp]`
- sets `argv = rsp + 8`
- ignores `envp` for now and passes `NULL`
- aligns the stack
- calls `__tls_init()`
- calls `main(argc, argv, NULL)`
- passes the return value to `exit()`

So from the program's perspective, `ld.so` is invisible once startup is done.
By the time `_start` runs, relocations have already happened.

## TLS and dynamic linking

TLS matters here because shared libc code uses `thread_local` state.

The concrete example in this tree is `errno`:

- `user/libc/src/errno.c`
  - `static thread_local int g_errno;`

### What libc does

`user/libc/src/tls.c` allocates one per-thread block:

```text
[ tls image | tcb ]
```

Then it:

- copies `.tdata`
- leaves `.tbss` zeroed
- stores `tcb->self`
- sets FS base with `wrfsbase`

That is triggered from `_start` by `__tls_init()`.

### Interaction with the runtime linker

The runtime linker does not own TLS allocation. Its job is only to make sure
the relocations needed by the current TLS access model are patched.

The current design is therefore:

- `ld.so` handles enough relocation for initial startup
- libc allocates and activates the actual per-thread TLS block

This is enough for the current `errno` test and for the current shared-library
build settings (`-ftls-model=initial-exec`), but it is not the same thing as
full general-purpose dynamic TLS.

For more on the TLS side alone, see `docs/tls.md`.

## Example: `/bin/echo`

For a minimal example, `/bin/echo` just calls `printf()`.

What happens when it starts:

1. The kernel maps `/bin/echo` at the fixed executable addresses from
   `user_dyn.ld`
2. The kernel sees `PT_INTERP` and loads `/lib/ld.so` at `0x7000000000 + p_vaddr`
3. The kernel builds auxv with:
   - `AT_BASE = 0x7000000000`
   - `AT_ENTRY = echo e_entry`
   - `AT_PHDR`, `AT_PHENT`, `AT_PHNUM`
4. The kernel sets RIP to `ld.so`'s entry
5. `ld.so` relocates itself
6. `ld.so` parses the executable `DT_NEEDED` list and loads `/lib/libc.so`
7. `ld.so` resolves the `printf` import and patches the PLT/GOT entries eagerly
8. `ld.so` jumps to the executable `_start`
9. `_start` initializes TLS and calls `main`
10. `main` calls `printf`, which now resolves through already-patched entries

That is the complete startup chain in the simplest dynamic case.

## Tools and tests

### `ldd`

`user/ldd.c` is a small inspection tool that reads:

- `PT_INTERP`
- `PT_DYNAMIC`
- `DT_NEEDED`

It prints the interpreter path and the libraries the file expects to load from
`/lib`.

### Kernel tests

`kernel/tests/elf_dynlink_test.c` covers:

- dynamic binary detection
- static binary behavior with the new loader
- `AT_PHDR` data population
- end-to-end spawn of `/tests/dynlink_test`

### User dynamic-link test

`user/tests/dynlink_test.c` exercises the full startup path after relocation.
It checks:

- `printf`
- `malloc` and `free`
- `strcmp`
- `strlen`
- `memset`
- `snprintf`
- `errno`
- `atoi`
- repeated calls after relocation

That combination catches both PLT/GOT relocation and ordinary shared-lib data
and code usage.

## Further reading

If you want to go deeper, this order works well:

1. Read this document once all the way through
2. Read the code in this tree in execution order:
   - `kernel/lib/elf.c`
   - `kernel/syscalls/sys_execve.c`
   - `kernel/syscalls/sys_spawn.c`
   - `user/rtld/rtld_start.S`
   - `user/rtld/rtld.c`
   - `user/libc/src/start.S`
   - `user/libc/src/tls.c`
3. Read the ELF generic ABI sections on loading and dynamic linking
4. Read the x86_64 psABI material for relocation and TLS details
5. Read a small production loader like musl's `ldso`
6. Read glibc's dynamic linker docs only after that

Useful references:

- ELF generic ABI:
  - https://gabi.xinuos.com/elf/07-loading-intro.html
  - https://gabi.xinuos.com/elf/09-dynamic.html
- x86_64 psABI project:
  - https://gitlab.com/x86-psABIs/x86-64-ABI/-/wikis/home
- GNU ld manual:
  - https://www.sourceware.org/binutils/docs-2.40/ld.html
  - https://sourceware.org/binutils/docs/ld/Output-Section-Phdr.html
- Linux runtime references:
  - https://man7.org/linux/man-pages/man5/elf.5.html
  - https://man7.org/linux/man-pages/man8/ld.so.8.html
  - https://man7.org/linux/man-pages/man3/getauxval.3.html
- musl runtime linker source:
  - https://git.musl-libc.org/cgit/musl/tree/ldso/dlstart.c
  - https://git.musl-libc.org/cgit/musl/tree/ldso/dynlink.c
- glibc dynamic linker docs:
  - https://sourceware.org/glibc/manual/latest/html_node/Dynamic-Linker.html

Friendly books:

- Computer Systems: A Programmer's Perspective, 3rd ed.
  - Best first book if you want the model before reading ELF specs
  - The linking chapter is the most useful part for this topic
- Linkers and Loaders, John R. Levine
  - The classic dedicated book on linkers, loaders, relocation, and shared libraries
  - Old, but still one of the best explanations of the fundamentals
- Practical Binary Analysis, Dennis Andriesse
  - Good once you want a more hands-on ELF and Linux binary view
- The Linux Programming Interface, Michael Kerrisk
  - Good for the surrounding process startup, `execve`, and runtime environment

## Current limits

The current implementation is intentionally small. Important limits:

- Only the main executable's `DT_NEEDED` entries are loaded
- Fixed library search path: `/lib`
- No `dlopen`, `dlsym`, or `dlclose`
- No lazy binding
- No symbol versioning
- No GNU hash lookup
- No constructor/destructor handling (`.init_array`, `.fini_array`)
- No duplicate-library suppression or reference counting
- No `RPATH` or `RUNPATH`
- No file-backed library mappings
- No final per-segment page protections
- No RELRO
- No demand paging
- No full dynamic TLS implementation
- Hard cap of 16 loaded DSOs
- Symbol lookup is linear, not hashed
- `envp` handoff is still minimal

These are design constraints of the current code, not generic ELF rules.

## Practical summary

The implementation is a clean two-stage design:

- The kernel is responsible for mapping the main executable, detecting
  `PT_INTERP`, loading `ld.so`, and building the auxv contract.
- `ld.so` is responsible for everything dynamic after that: self-relocation,
  `DT_NEEDED` loading, symbol resolution, and eager relocation.

That split keeps the kernel loader simple while still supporting shared libc and
other shared libraries in userland.
