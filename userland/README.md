# C-family userland

- `crt0.S`: the kernel enters at `_start` with `x0` holding the bootstrap
  channel handle. It zeroes `.bss`, sets `sp` to `__stack_top`, and calls
  `main`.
- `runtime.c`: C wrappers over the trap ABI for handles, channels, VMOs,
  VMAR maps/unmaps, process create/address-space/run/wait, ports and the
  monotonic clock, plus `kumo_debug_write` and `kumo_process_exit`.
- `user.ld`: links the image at `0x200000`, the kernel's `USER_IMAGE_BASE`.
- `trap.S`: external raw trap ABI for C++ and Objective-C++ callers.
- `../tests/c_smoke.c`: first C smoke, packaged as `bin/sora` in the initrd.
- `../tests/cxx_smoke.cpp`: freestanding C++ raw trap and runtime check.

Build, stage, and run under QEMU:

    cmake --preset qemu
    cmake --build build/qemu --target qemu-smoke
    cmake --build build/qemu --target qemu-cxx-smoke

The build invokes Cargo for KUMO with `KUMO_CONSOLE_LOGLEVEL=7`, builds the C
loader and C/C++ programs with Clang/LLD, and packages the kernel and user
program in KUMORD01 using the native C packer. QEMU starts the C loader
directly and waits for the program markers and a successful kernel post-exit
`USERLAND EL0` check.

`Cirrus::Runtime` is the CMake library target for the public C ABI and ring.
`cirrus_add_program(name sources...)` adds startup assembly and the user linker
script. Freestanding C++17 currently has no exceptions, RTTI, standard library
or global constructor support. This is a bootstrap runtime, not libSystem.

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
