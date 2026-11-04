# Vendored upstream: KUMO / MUREX

CIRRUS vendors only the kernel-side closure of the KUMO repository, as a
snapshot drop. Everything else in KUMO (its userspace including Sora, the
Nijigumo bootloader, imager, xtask, graphics libs, toolchain) is intentionally
not vendored; see SEED.md for what CIRRUS replaces with C-lineage components.

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

- None yet.

## Licensing

Upstream declares `MIT OR Apache-2.0` in its workspace manifest. No root
LICENSE file is published at the pinned commit; add upstream license texts to
`THIRD_PARTY_NOTICES` when CIRRUS ships artifacts.
