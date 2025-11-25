# experiment 64

An x86_64 hobby kernel with a tiny userland, a VFS layer, ext2/FAT32 support, and a libc/tiny shell for exercising the kernel interfaces. The tree builds with a cross-compiling `x86_64-elf-gcc` toolchain and runs under QEMU.

![Kernel splash](https://pazotto.com/img/experiment64/Screenshot2.png)

## Layout (high level)

- `kernel/` core kernel code, arch bring-up, drivers, mm, fs, scheduler, syscalls, tests
- `user/` simple libc (`user/libc`) and sample programs (`init`, `shell`, `ls`)
- `include/` shared headers
- `docs/` design notes (e.g., `docs/kasan.md`)
- `scripts/` build helpers (disk image generation, etc.)

## Toolchain and build requirements

- Cross toolchain: `x86_64-elf-gcc` and binutils in `PATH`
- QEMU for running and for `make tests` / `make run`
- Optional: `clangd` / `clang-tidy` if you use the provided targets

## Common targets

- `make image.hdd` – build kernel + userland and assemble disk images
- `make run` – boot the kernel in QEMU with the generated image
- `make tests` – build a test image and run the in-kernel test suite (UBSan enabled)
- `make tests-kasan` – same as `make tests` but with the lightweight KASAN shadow
- `make checks` – formatting/lint/static-analysis wrapper
- `make clangd-check` / `make clang-tidy` – language server / lint helpers (no .S files)

## Tests

Always run tests after making changes to the codebase.
To run the tests, use the following command:

```bash
make tests
```

`make tests` will automatically clean up any previous test artifacts and build the necessary components before executing
the tests.
The tests run with a timeout of 10 seconds to prevent hanging. If you see that a timeout has occurred, it means the last
test did not complete successfully within the allotted time.
To know the tests completed, you need to see either "ALL TESTS PASSED" or "SOME TESTS FAILED" messages at the end.

Always add new tests for every new feature/bug fix, if possible.

### Custom test framework

- Tests live under `kernel/tests/` and are discovered via a linker section (`.test_array`)
- The runner prints `[PASS]/[FAIL]` with per-test timing and a compact summary
- Output is captured and only flushed on failure to keep logs short
- In KASAN mode, redzones and poisoned frees are enforced; trap helpers let specific tests assert a panic

![Testing framework output](https://pazotto.com/img/experiment64/Screenshot1.png)

## Checks

To ensure code quality and consistency, run the following checks:

```bash
make checks
```

This command will run formatting checks, linting, and static analysis on the codebase. Run it after making changes to
ensure everything adheres to the project's coding standards.

## Running

To actually run the OS inside QEMU, use the following command:

```bash
make run
```

## Kernel overview

- **Arch/boot**: x86_64, Limine bootloader, Intel-syntax asm, SMP bring-up, APIC + IOAPIC, IDT/GDT, syscall entry
- **Memory**: physical allocator (bitmap), virtual memory manager (4 KiB pages), kernel heap (slab + big allocs), optional KASAN shadow (1 byte / 8 bytes) and redzones, stack protector, UBSan
- **Timing**: PIT for ticks, TSC calibration for timing
- **Drivers**: serial/uart, framebuffer console, keyboard, IDE/ATA, GPT parsing
- **VFS & filesystems**: VFS layer with devfs nodes, ext2 mounted at `/`, FAT32 mounted at `/mnt`
- **Process/tasking**: basic scheduler, spinlocks/sleeplocks, syscall layer (see `user/libc/src/syscall.c`), simple user programs (`init`, `shell`, `ls`)
- **Debug**: symbolized stack traces, panic trapping in tests, test output capture, `docs/kasan.md` for shadow-memory details

### KASAN (shadow memory) mode

A lightweight KASAN-style shadow is available for debugging memory bugs. To run tests with KASAN enabled:

```bash
make tests-kasan
```

See `docs/kasan.md` for how it works, coverage, and limitations.
