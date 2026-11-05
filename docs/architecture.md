# Cirrus architecture and migration

Cirrus implements a native mobile system on KUMO, with C-family system code
and the open Darwin/iOS stack as its userspace reference. The language choice
applies to newly authored system code and native tools, not a rewrite of the
upstream Rust microkernel. Counting vendored source lines is not a measure of
porting progress.

The platform direction is a **Darwin personality on KUMO**: port the useful
open Apple stack, preserve a defined set of developer-facing interfaces, and
build original C-family replacements for the missing mobile layers. The
[Darwin ecosystem plan](darwin-personality.md) defines compatibility levels,
project boundaries, security requirements and acceptance gates. Its project
map is a plan, not an implementation claim.

## Boundaries

| Layer | Language / build | Current state |
| --- | --- | --- |
| KUMO kernel, HAL and kernel dependencies | Upstream Rust, Cargo invoked by CMake | Vendored; ARM64 bootable in QEMU |
| Native bootloader | Freestanding C11 and assembly, CMake | DTB/BootInfo/ELF handoff works in QEMU; phone validation pending |
| Trap ABI and IPC | C11 with C linkage, CMake | Runtime subset, channels and shared-VMO SPSC ring; full service transport pending |
| Native system tools | C11, CMake | Android boot-image tool and deterministic KUMORD01 packer |
| Driver and service domains | C11 / freestanding C++17 | Not implemented as a mobile service stack |
| Frameworks and native apps | Objective-C / Objective-C++ / C++ | Runtime and framework ports pending |
| Temporary UEFI adapter | Vendored upstream Rust, opt-in | Preserved for Orange Pi; excluded from default boot images |

Kernel-side `kumo-ipc` is a kernel dependency. Cirrus userspace uses the public
C ABI and C ring library; importing KUMO's Rust Sora service/runtime stack is
outside the architecture. The existing kernel bootstrap path `bin/sora`
currently names a C test executable and remains a wire-format convention.

CMake builds host tools natively and invokes a separate ARM64 CMake build for
freestanding code. The only Cargo invocation in the normal image build selects
the vendored kernel. Host tools and the standalone ARM64 C/C++ preset work
without Cargo. The Rust `xtask` and Cargo alias have been removed.

Public interfaces use C layouts and linkage. Drivers and services should cross
the kernel boundary through capability handles and VMOs rather than a direct
kernel implementation dependency. Higher-level POSIX, service and framework
semantics belong above those primitives.

## Open Darwin stack

These projects are pinned in `third_party/manifest.tsv`. Existing Apple source
is a porting base, not a working Cirrus SDK or proof of iOS app compatibility.

| Reference layer | Cirrus direction | Prerequisite / gap |
| --- | --- | --- |
| Libc and libSystem | Retarget the Apple/BSD C runtime behind a KUMO platform layer | File descriptors, filesystem/process services, allocator, errno, TLS and pthreads |
| dyld | Port the open C++ loader and its platform interfaces | Explicit Mach-O/relocation/linker support, VM and process launch interfaces |
| launchd and service IPC | C/C++ service bootstrap and launchd port over native capabilities | Service registry, process lifecycle, handle grants, restart and notification protocols |
| CoreFoundation | Port the open C framework | Working libc, allocation, threads, run-loop/event backend |
| libdispatch and Blocks runtime | Port the open C concurrency stack | pthreads, TLS, timers, event/workqueue backend and Blocks support |
| Objective-C runtime | Evaluate vendored objc4 and portable libobjc2 | Loader/runtime ABI, exceptions/unwinding decision, allocation and thread support |
| Foundation-style frameworks | C-family framework implementation or an appropriate open port | Objective-C runtime and CoreFoundation services; source/API coverage needs a separate decision |
| UI, compositor and drivers | Native C/C++/Objective-C mobile layers | Display, input, power and device services; no implementation yet |

The bootstrap currently loads static ELF images. Vendoring dyld does not add
Mach-O execution or Darwin binary compatibility. Keep ELF working while the
loader, object format and SDK plan is implemented explicitly. UIKit, Metal and
other proprietary Apple frameworks are not supplied by these source imports.

The initial compatibility target is recompilation of selected open libraries
against a Cirrus SDK. A later Mach-O profile is separate from external Darwin
binary compatibility. The Darwin personality never adds privileges to a
process or disables KUMO capability enforcement. C/C++ compatibility services
must themselves hold narrowly scoped authority.

Do not substitute a Rust libc, Rust service fabric or a Swift runtime for the
planned C-family foundation. Optional application languages and Android guest
work come after the native platform and are outside the bootstrap dependency
chain.

## Implementation order

1. **Build foundation (this change):** CMake/Ninja, host C image tools, ARM64
   toolchain, reusable C runtime target, C/C++ ABI checks and direct C-loader
   QEMU regression. Keep the old upstream UEFI adapter explicitly opt-in.
2. **C runtime/platform:** complete trap wrappers with contract tests; establish
   allocation, TLS/errno, thread and notification APIs. Add service bootstrap,
   process loading and capability transfer with independent process tests.
3. **POSIX and libSystem:** filesystem and descriptor services, pthreads and
   the Apple/BSD platform adapter. Build a small existing C utility against the
   resulting sysroot and execute it on KUMO before claiming a libc port.
4. **Loader and core services:** define the target object/SDK ABI, port dyld
   and launchd, then prove dynamic library loading and supervised services.
5. **C/Objective-C frameworks:** Blocks, libdispatch, CoreFoundation and the
   selected Objective-C runtime; prove an Objective-C application on KUMO.
6. **Mobile system:** isolated device drivers, storage, display, input, power,
   networking, compositor and application lifecycle. Phone hardware validation
   proceeds alongside the relevant platform work.

The source inventory and retained license notices remain in
`third_party/README.md` and `THIRD_PARTY_NOTICES.md`. This build change does not
enable or link the unported Darwin framework snapshots.
