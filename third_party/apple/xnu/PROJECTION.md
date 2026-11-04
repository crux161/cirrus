# xnu header projection

This directory is not the full xnu tree. It is a projection of
`apple-oss-distributions/xnu` at `xnu-12377.121.6`
(`ac9718fb1af618d5ce8678d0dc6e8a58f252216f`) for the Darwin libc/dyld port:

- `EXTERNAL_HEADERS/`
- `bsd/sys/`, `bsd/net/`, `bsd/netinet/`, `bsd/netinet6/`, `bsd/arm/`,
  `bsd/vm/`, `bsd/pthread/`, `bsd/uuid/`, `bsd/security/` (headers only)
- `osfmk/mach/`, `osfmk/mach_debug/`, `osfmk/kern/`, `osfmk/arm64/`,
  `osfmk/machine/`, `osfmk/vm/` (headers only)
- `libkern/libkern/`, `libkern/os/` (headers only)
- `libsyscall/` (full, including syscall stub sources)

Kernel implementation sources outside these paths are excluded. Rebuild the
projection with `scripts/vendor-third-party.sh sync xnu`.
