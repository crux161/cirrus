# Minimal C userland

- `crt0.S`: the kernel enters at `_start` with `x0` holding the bootstrap
  channel handle. It zeroes `.bss`, sets `sp` to `__stack_top`, and calls
  `main`.
- `runtime.c`: minimal stubs over the trap ABI: `kumo_debug_write`,
  `kumo_handle_close`, and `kumo_process_exit`.
- `user.ld`: links the image at `0x200000`, the kernel's `USER_IMAGE_BASE`.
- `../tests/c_smoke.c`: first C smoke, packaged as `bin/sora` in the initrd.

Build, stage, and run under QEMU:

    cargo xtask qemu-smoke --arch aarch64

The runner builds the kernel with `KUMO_CONSOLE_LOGLEVEL=7`, builds the C
smoke, creates a one-entry initrd, stages an EDK2 ESP with `niji-uefi`, and
waits for both the C marker and the kernel's post-exit `USERLAND EL0` check.
