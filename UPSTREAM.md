# Vendored upstream: KUMO / MUREX

CIRRUS vendors the kernel-side dependency closure of KUMO as a snapshot drop,
plus the temporary UEFI adapter described below. KUMO's userspace (including
Rust Sora), imager, xtask, graphics libraries and toolchain are not imported.
Cirrus supplies its own C-family boot/runtime and CMake build; see
`docs/architecture.md` for the boundaries and porting order.

- Upstream: https://github.com/crux161/kumo
- Pinned commit: `ce6d6285fe7e248549b6c9c2c86bf77fcc6427fa`
- Commit date/subject: 2026-11-04 12:47:52 -0800, "REENTRY checkpoint"
- Branch the pin was HEAD of: `stable/hid-keyboard-shell`
- Imported by: `scripts/vendor-kumo.sh` (snapshot copy, no upstream history)

## Paths imported

| Upstream path | CIRRUS path |
| --- | --- |
| `kernel/` | `src/kernel/` |
| `hal/` | `src/hal/` |
| `lib/kumo-abi/` | `src/lib/kumo-abi/` |
| `lib/kumo-bsp/` | `src/lib/kumo-bsp/` |
| `lib/kumo-fdt/` | `src/lib/kumo-fdt/` |
| `lib/kumo-i2c-hid/` | `src/lib/kumo-i2c-hid/` |
| `lib/kumo-ipc/` | `src/lib/kumo-ipc/` |
| `lib/kumo-pdc/` | `src/lib/kumo-pdc/` |
| `boot/niji-loader/` | `src/boot/niji-loader/` |
| `boot/niji-uefi/` | `src/boot/niji-uefi/` |
| `vendor/qrcodegen/` | `src/vendor/qrcodegen/` |
| `rk3588-orangepi-5-plus.dtb` | `src/rk3588-orangepi-5-plus.dtb` |
| `sc8280xp-lenovo-thinkpad-x13s.dtb` | `src/sc8280xp-lenovo-thinkpad-x13s.dtb` |

The two root DTBs are `include_bytes!` inputs for host tests in the HAL and in
`kernel/src/syscall.rs`; their `../../../` / `../../` paths resolve to `src/`
under this layout.

## Why this closure

`kernel/Cargo.toml` inherits workspace fields and workspace dependencies and
path-depends on `../boot/niji-loader`. `kumo-hal` selects its arch backend by
feature; `kumo-hal-aarch64` in turn path-depends on `kumo-fdt`, `kumo-pdc`,
`kumo-i2c-hid` and `vendor/qrcodegen`. The only crates.io dependency in the
closure is `linked_list_allocator` (used by the kernel); CIRRUS resolves Rust
dependencies from crates.io rather than upstream's `vendor/cargo` mirror.

## Updating

    scripts/vendor-kumo.sh [commit]

The script re-clones upstream, checks out the imported paths, and rsyncs them
into `src/` (with `--delete` inside imported directories). Review `git status`
and `git diff`, then commit. This is a snapshot drop, not a merge: upstream
history is not imported, and local CIRRUS modifications appear as ordinary
diffs that must be carried deliberately when re-vendoring.

## Local divergence (keep current)

- `hal/kumo-hal-aarch64`: a third Stage-A UART sink for the SDM845 GENI/QUP
  debug UART (`console_set_geni_base`, polled TX command sequence, bounded
  waits, and polled RX with LSB-first FIFO-word unpacking and last-word
  valid-byte handling). It follows the PL011/DW-8250 injection discipline and
  names no board address. Proposed upstream.
- `kernel`: resolves the board BSP's GENI console base before the first
  Stage-A log and includes the GENI sink in the serial-floor gate, mirroring
  the PL011 and DW-APB paths. Proposed upstream.
- Re-vendoring note: `hal/` and `kernel/` are rsynced with `--delete`; carry
  these diffs deliberately when the pin moves.

## Boot boundary and temporary firmware adapter

The default mobile/QEMU path uses `boot/cirrus-boot`, the freestanding C
loader built by CMake. Cirrus-owned build tasks and KUMORD01 packaging no
longer use Rust `xtask`.

`src/boot/niji-loader` remains in the kernel's dependency closure; its name
does not make it the active Cirrus firmware entry point. `src/boot/niji-uefi`
is retained solely as a temporary upstream firmware adapter for existing UEFI
hardware (Orange Pi). CMake exposes it only with `CIRRUS_LEGACY_UEFI=ON` and
the explicit `legacy-uefi-esp` target. A native C UEFI replacement is pending;
normal Cirrus builds do not depend on this adapter.

## Licensing

Upstream declares `MIT OR Apache-2.0` in its workspace manifest. No root
LICENSE file is published at the pinned commit; add upstream license texts to
`THIRD_PARTY_NOTICES` when CIRRUS ships artifacts.
