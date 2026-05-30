# experiment 64

An x86_64 hobby kernel with a VFS layer, ext2/FAT32 support and a libc/tiny shell.

![GUI screenshot](docs/img/gui.png)

![Kernel splash](https://pazotto.com/img/experiment64/Screenshot2.png)

## Toolchain and build requirements

- Cross toolchain: `x86_64-elf-gcc` and binutils
- QEMU for running and for `make tests` / `make run`
- Optional: `clang-tidy` if you use the provided targets

## Common targets

- `make image.hdd` - build kernel + userland and assemble disk images
- `make small-image` - build a minimal bootable image (`small-image.hdd`) with ESP + ext2 root
- `make run` - boot the kernel in QEMU with the generated image
- `make run-small-image` - boot QEMU using `small-image.hdd`
- `make run-nox` - boot headless (`-nographic`) with console I/O on serial
- `make vbox` - boot the kernel in VirtualBox with the generated images (USB disk attached)
- `make tests` - build a test image and run the in-kernel test suite (UBSan enabled)
- `make clang-tidy` - lint/static-analysis

## Tests

To run the tests, use the following command:

```bash
make tests
```

`make tests` will automatically clean up any previous test artifacts and build the necessary components before executing
the tests.
The tests run with a timeout of 120 seconds to prevent hanging. If you see that a timeout has occurred, it means the
last
test did not complete successfully within the allotted time.
To know the tests completed, you need to see either "ALL TESTS PASSED" or "SOME TESTS FAILED" messages at the end.

### Custom test framework

- Tests live under `kernel/tests/` and are discovered via a linker section (`.test_array`)
- The runner prints `[PASS]/[FAIL]` with per-test timing and a compact summary
- Output is captured and only flushed on failure to keep logs short

![Testing framework output](https://pazotto.com/img/experiment64/Screenshot1.png)

## Checks

To ensure code quality and consistency, run the following checks:

```bash
make clang-tidy
```

This command will run linting and static analysis on the codebase.

## Running

To actually run the OS inside QEMU, use the following command:

```bash
make run
```

To run using the minimal disk image:

```bash
make run-small-image
```

To boot the disk image as a USB mass storage device:

```bash
make run-usb
```

To run headless in a terminal:

```bash
make run-nox
```

## Running on real hardware

To write the disk image to a real disk, use the following command:

```bash
make disk DISK=/dev/sdX
```

By default, the disk image is written to `/dev/sdb` so, **BE CAREFUL**!

## Disk images

- `image.hdd`: GPT image with ESP (FAT32), root ext2 and data FAT32
- `small-image.hdd`: GPT image with ESP (FAT32) and root ext2 only
- `image2.ide`: secondary IDE disk with an ext2 partition
- `image3.usb`: USB mass storage disk with an ext2 partition

## Reading map

New here? [`docs/README.md`](docs/README.md) indexes every design doc, the
[glossary](docs/glossary.md) of acronyms, and a set of end-to-end
[walkthroughs](docs/walkthroughs/) that trace one real flow each (boot, a
syscall, a context switch, `open()` to disk, `mmap`, a TCP connection, a USB
read, a WM frame, signal delivery).

- Boot and init: `kernel/boot.c`, `kernel/kernel.c`
- CPU, interrupts, SMP: `kernel/arch/x86_64/`, with design notes in `docs/x86_64/` (`cpu.md`, `gdt.md`, `idt.md`, `apic.md`, `smp.md`, `acpi.md`)
- Memory: `kernel/mem/`, with design notes in `docs/pmm.md`, `docs/vmm.md`, `docs/heap.md`, `docs/dma.md`, and `docs/address_space.md`
- Storage and filesystems: `kernel/io/`, `kernel/fs/`, `docs/storage.md`, `docs/ext2.md`
- Tasks and syscalls: `kernel/task/`, `kernel/syscalls/`, `docs/scheduler.md`, `docs/syscalls.md`, `docs/signals.md`
- Networking: `kernel/net/`, `docs/networking.md`
- IPC: `kernel/ipc/`, with `docs/shm.md` and `docs/futex.md`
- Dynamic linking: `kernel/lib/elf.c`, `user/rtld/`, `docs/dynamic_linking.md`
- Debug and symbolization: `kernel/debug/`
- Userland and libc: `user/`, `user/libc/`
- GUI: `user/wm/`, `user/wmlib/`, `user/libc/src/wmclient.c`, `docs/wm_protocol.md`

## License note

The project is MIT licensed except for the Atheros AR8162 driver files
(`kernel/drivers/atl1c.c`, `include/drivers/atl1c.h`), which are GPL-2.0-only.

## Kernel overview

- **Arch/boot**: x86_64, Limine bootloader, Intel-syntax asm, SMP bring-up, APIC + IOAPIC, IDT/GDT, syscall entry
- **Memory**: physical allocator (bitmap), DMA allocator (contiguous, HHDM-mapped), virtual memory manager (4 KiB pages),
  kernel heap (slab + big allocs, panics on OOM), stack protector, UBSan, VMA tracking for mmap, address space layout in
  `docs/address_space.md`
- **Timing**: PIT for ticks, TSC calibration for timing
- **Drivers**: serial/uart (TX + RX), framebuffer console, keyboard, mouse, IDE/ATA and AHCI via PCI scan, GPT parsing, USB xHCI (
  USB 3.x enumeration + BOT mass storage read/write; EHCI quiesce + port routing), e1000 NIC, framebuffer device
  `/dev/fb0`
- **Networking**: e1000 and Atheros AR8162 driver, Ethernet/IPv4/UDP, ARP, ICMP (ping), DHCP client, and a little DNS
  client in userland. Socket syscall notes are in `docs/networking.md`.
- **VFS & filesystems**: VFS layer with devfs nodes, ext2 mounted at `/` (new entries default to 0755/0644), FAT32
  mounted at `/mnt`, ESP FAT32 mounted at `/boot`, second-disk ext2 (if present) mounted at `/disk1`, USB ext2 (if
  present and not the boot device) mounted at `/usb`. The boot disk is auto-detected across IDE/AHCI/USB by scanning
  GPT (ESP + root).
- **Process/tasking**: basic scheduler, spinlocks/sleeplocks, syscall layer (see `user/libc/src/syscall.c`), user
  programs (`init`, `sh`, `ls`, `cat`, `edit`, `grep`, `mv`, `wc`, etc.), thread-local storage via FSGSBASE (see
  `docs/tls.md`), a minimal POSIX-style pthread subset (see `docs/pthreads.md`), plus kernel test helper binaries
  under `/tests`
- **Dynamic linking**: dynamic executables can hand off to `/lib/ld.so`, which loads shared libraries from `/lib`,
  resolves symbols, and applies eager relocations; see `docs/dynamic_linking.md`
- **Syscalls & features**: `execve` with argv/envp, `waitpid` (WNOHANG), `ioctl` (TTY window size, foreground PID get/set, framebuffer
  queries, keyboard flush, network `GETNETINFO`; see `docs/ioctl.md`), `mmap`/`munmap` for `/dev/fb0` and shared memory,
  `link`/`unlink`/`rename`, `getcwd`, `dup2`, `openpty` (minimal PTY pair allocation), full `open` flag handling
  (create/trunc/append), `mmap`-backed framebuffer access, named shared memory (`shm_open`/`shm_unlink`), user pointer
  validation for canonical and user-mapped ranges, and differentiated negative status returns for thread/socket/fd/path
  syscalls (see `docs/syscalls.md`)
- **Logging**: boot messages mirrored to `/var/log/boot` once the root fs is up with storage cache flush
- **Debug**: symbolized stack traces, panic trapping in tests, test output capture, targeted PCI config dumps and USB
  controller interface logs

## GUI

A window manager with basic graphical primitives (based on https://github.com/JMarlin/wsbe) that
supports separate processes running in their own windows. Client processes communicate with the WM
over pipes and render into shared memory buffers that the WM composites onto the framebuffer. See
`docs/wm_protocol.md` for the protocol details. The desktop launches both
`/bin/wmclient_demo`, `/bin/calculator`, `/bin/term`, and `/bin/doom` as WM clients. `wm_terminal`
opens a PTY pair and runs `/bin/sh` on the slave side, letting the shell run inside a window.
It supports ANSI color/control sequences and updates PTY winsize when its window is resized.
WM clients use double-buffered shared-memory presents with WM acknowledgment before client-side buffer flips.
Keyboard press/release events are routed to the focused client window.
One WM client connection can own multiple windows, including child windows.
The userland libc also includes a small immediate-mode UI helper (`wm/imui.h`) for
building controls like buttons on top of the WM client protocol.


## DOOM

I also ported DOOM. When launched from WM it runs in its own window (with WM keyboard events and shared-memory
buffer compositing), and `ALT+ENTER` toggles between WM window-buffer rendering and direct `/dev/fb0` rendering.
When launched outside WM it still runs full screen by drawing directly to `/dev/fb0`.

![DOOM screenshot](docs/img/doom.png)

Doomgeneric is cloned on demand and patched; see `docs/doom.md`.

## Web server

A small web server is also included.

![Web server screenshot](docs/img/webserver.png)

## Real hardware

The OS running on an old Lenovo laptop with an Atheros AR8162 NIC.

![Real hardware](docs/img/real_hardware.jpg)
