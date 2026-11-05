# Cirrus engineering constraints

- Cirrus is a mobile OS built on the KUMO/MUREX microkernel. Preserve that
  kernel boundary; do not replace KUMO with XNU or Linux.
- Write Cirrus-owned bootloaders, IPC, drivers, services, runtimes, frameworks
  and native build tools in C, C++, Objective-C or Objective-C++. Use assembly
  for architecture entry points. Shell and CMake may orchestrate builds.
- Do not add Rust crates or a Rust system/runtime dependency outside the
  vendored KUMO closure. Existing kernel HAL/ABI libraries are part of that
  closure. `src/boot/niji-loader` is a kernel dependency; `niji-uefi` is an
  opt-in temporary firmware adapter, not the native mobile bootloader.
- CMake owns the build graph. Cargo is an adapter for upstream KUMO, not a
  Cirrus task runner. Keep host tools runnable without a Rust toolchain.
- Follow the open Darwin/iOS stack direction in `docs/architecture.md`:
  Libc/libSystem, dyld, launchd, CoreFoundation, libdispatch and Objective-C.
  Keep upstream source snapshots and Cirrus port adapters separate. Never
  describe a vendored component as ported until it builds and runs on KUMO.
- The Darwin ecosystem direction and project gates are in
  `docs/darwin-personality.md`. A personality never grants extra authority or
  bypasses KUMO checks. Keep compatibility services scoped and distinguish
  source/API ports from Mach-O and external binary compatibility. Original
  mobile framework replacements must have explicit API profiles and tests.
- Maintain public C ABI headers usable from C11 and C++17. Freestanding C++
  initially has no exceptions, RTTI, standard library or constructor runtime.
- Keep upstream imports identifiable. Record kernel changes in `UPSTREAM.md`
  and third-party changes against `third_party/manifest.tsv`.
- For changes to the build/runtime, run the relevant CMake/CTest checks.
  Boot-path changes also require the QEMU smoke. Do not change or commit the
  ignored local research, internal plans, device resources or firmware.
