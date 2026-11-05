# Cirrus

Cirrus is an ARM64 mobile operating system built around the KUMO/MUREX
microkernel. Cirrus-owned boot code, IPC, drivers, services and frameworks use
**C, freestanding C++, Objective-C and Objective-C++**. CMake is the primary
build system; Cargo builds the vendored KUMO kernel closure only.

The userspace direction follows the open parts of the Darwin/iOS stack:
Libc/libSystem, dyld, launchd, CoreFoundation, libdispatch and an Objective-C
runtime. Their source snapshots are present, but these libraries are **not yet
ported to KUMO**. See [architecture and porting order](docs/architecture.md).

The intended transplant is a [Darwin personality on KUMO](docs/darwin-personality.md):
reuse the open system stack, implement the missing mobile frameworks in
C-family languages, and preserve KUMO's capability model across both. The plan
separates source ports, framework APIs, Mach-O support and external binary
compatibility, with runnable acceptance criteria for each stage.

## Build and check

Host prerequisites: CMake 3.24+, Ninja, a C11/C++17 compiler, Git, and `dtc`.
ARM64 builds additionally need Clang, LLD and `llvm-objcopy`. Boot-image
packaging uses `gzip`; QEMU testing uses `qemu-system-aarch64` and `fdtput`.

Host libraries, image tools and tests need no Rust toolchain:

```sh
cmake --preset host
cmake --build --preset host
ctest --preset host
```

Build just the freestanding C/C++ runtime, C bootloader and smoke programs:

```sh
cmake --preset aarch64
cmake --build --preset aarch64
```

For a complete image, install the Rust toolchain described by
`rust-toolchain.toml` for KUMO, then:

```sh
cmake --preset qemu
cmake --build --preset qemu
cmake --build build/qemu --target qemu-smoke
cmake --build build/qemu --target qemu-cxx-smoke
```

`qemu-smoke` boots the **C loader** directly, runs the C trap/channel/shared-VMO
IPC checks at EL0, and requires a successful kernel post-exit check.
`qemu-cxx-smoke` exercises the external C syscall ABI from freestanding C++.
`cmake --build build/qemu --target qemu` attaches an interactive serial console.
The convenience wrapper `scripts/run-qemu.sh` also supports `--check`,
`--no-run`, `--fresh`, `--qemu PATH` and `--timeout SECONDS`.

Build outputs and compilation databases live under `build/<preset>/`.
`build/qemu/initrd.img` contains the kernel ELF and a C program at `bin/sora`;
that filename is KUMO's bootstrap convention, not the Rust Sora implementation.

## Mobile boot image

The initial phone targets are OnePlus 6/6T (SDM845). Supply a device DTB:

```sh
cmake --preset oneplus6 -DCIRRUS_DTB=/absolute/path/to/device.dtb
cmake --build build/oneplus6 --target boot-image
```

Use `-DCIRRUS_BOARD=oneplus-fajita` for the 6T. Cache settings
`CIRRUS_BOOT_SLOT`, `CIRRUS_BOOT_HEADER_VERSION`, `CIRRUS_BOOT_PAGE_SIZE` and
`CIRRUS_BOOT_CMDLINE` control packaging. The slot setting labels the output;
the build does not flash a device. Hardware boot remains unverified.

Orange Pi's existing UEFI staging path is retained as an explicit temporary
upstream Rust exception: `scripts/stage-orangepi5.sh [--out DIR]`. It requires
the Rust `aarch64-unknown-uefi` target. Native Cirrus/QEMU builds do not use it.

## Source layout

| Path | Role |
| --- | --- |
| `boot/cirrus-boot/` | Freestanding C mobile loader, BootInfo/DTB/ELF parsing, C boot-image tool |
| `include/`, `userland/` | Public C ABI, C/C++ runtime and shared-memory IPC |
| `tools/` | Native C build tools, including the KUMORD01 packer |
| `cmake/`, `scripts/` | Toolchains, kernel adapter, image staging and test runners |
| `tests/` | Host validation and ARM64 C/C++ EL0 programs |
| `src/` | Vendored KUMO kernel and its dependency closure; see [UPSTREAM.md](UPSTREAM.md) |
| `third_party/` | Pinned upstream sources; see [third-party inventory](third_party/README.md) |

New system code follows [AGENTS.md](AGENTS.md). CI builds the C/C++ targets,
runs CTest, checks the language boundary, and boots both smoke images in QEMU.
