# Minimal C userland

- `crt0.S`: the kernel enters at `_start` with `x0` holding the bootstrap
  channel handle. It zeroes `.bss`, sets `sp` to `__stack_top`, and calls
  `main`.
- `runtime.c`: C wrappers over the trap ABI for handles, channels, VMOs,
  VMAR maps/unmaps, process create/address-space/run/wait, ports and the
  monotonic clock, plus `kumo_debug_write` and `kumo_process_exit`.
- `user.ld`: links the image at `0x200000`, the kernel's `USER_IMAGE_BASE`.
- `../tests/c_smoke.c`: first C smoke, packaged as `bin/sora` in the initrd.

Build, stage, and run under QEMU:

    cargo xtask qemu-smoke --arch aarch64

The runner builds the kernel with `KUMO_CONSOLE_LOGLEVEL=7`, builds the C
smoke, creates a one-entry initrd, stages an EDK2 ESP with `niji-uefi`, and
waits for both the C marker and the kernel's post-exit `USERLAND EL0` check.

## IPC ring

`include/kumo_ipc_ring.h` and `userland/ipc_ring.c` provide the initial
single-producer/single-consumer shared-memory ring. It is designed to live in
a KUMO VMO mapped into both address spaces, with reserve/commit and
acquire/release zero-copy paths plus copy-based push/pop helpers.

The C smoke now goes further in the same image: it maps a VMO-backed ring into
its own address space, creates a child process, maps the same ring VMO and a
copy of the parent image into the child, runs a child entry synchronously, and
verifies the child's record in the shared ring. It also exercises a channel
round trip. No second ELF or loader is involved; the child runs from the image
VMO and shares only the ring mapping.

Host-only ring check:

    scripts/check-c-ipc-ring.sh
