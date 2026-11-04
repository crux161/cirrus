# Apple component licensing

License texts and terms vary per project and per file. The per-file header
controls.

- `APPLE_LICENSE` is the Apple Public Source License 2.0. It covers the APSL
  projects: Libc, Libsystem, dyld, CF, objc4, xnu, cctools, and APSL-marked
  files in other projects.
- `APACHE-2.0` is the Apache License 2.0 text. launchd and libdispatch are
  Apache-2.0; `libdispatch/LICENSE` is the upstream copy.
- The POSIX utility projects, shell_cmds, file_cmds, text_cmds, basic_cmds, and
  adv_cmds, are predominantly BSD-3-Clause with some APSL files. Utility source
  files retain their copyright and permission notices.
- Libc contains substantial BSD-3-Clause code alongside APSL code, and xnu
  headers include BSD-licensed material. Preserve all file-level notices.
