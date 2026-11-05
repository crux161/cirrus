# Third-party notices

CIRRUS includes or pins the following third-party components. Vendor pins and
the full path map are in `third_party/manifest.tsv`; per-component license
texts are retained in the corresponding `third_party` directories.

| Component group | Pins | License | Distribution |
| --- | --- | --- | --- |
| Apple Darwin core: Libc, Libsystem, dyld, CF, objc4, xnu headers | project tags, 2021-2026 | APSL-2.0, mixed BSD-3-Clause | vendored under `third_party/apple/` |
| Apple launchd, libdispatch | launchd-842.92.1, libdispatch-1542.100.32 | Apache-2.0 | vendored |
| Apple POSIX utilities and cctools | project tags | BSD-3-Clause and APSL-2.0 | vendored |
| GNUstep libobjc2 | v2.3 | MIT | vendored |
| GNUstep tools-make | master commit | GPL-3.0 | fetched only, not linked |
| GNUstep libs-base, libs-corebase | master commits | LGPL library code; GPL tools/tests; per-file notices apply | fetched only, not linked |
| Darling | v0.1.20260608 | GPL-3.0 | fetched only, not linked |
| Mesa, including freedreno and panfrost | mesa-26.2.6 | MIT | fetched only |
| Linux BSP reference extraction | v6.18.60 | GPL-2.0-only | fetched only, not linked |
| Halium libhybris | master commit | Apache-2.0 | vendored |
| AOSP manifest and tree | android-16.0.0_r4 | Apache-2.0 | manifest vendored, tree fetched via repo |
| microG GmsCore | v0.3.17.252432 | Apache-2.0 | fetched only |
| Aurora Store | 4.8.4 | GPL-3.0 | fetched only |
| F-Droid client | v0.104 | GPL-3.0 | fetched only |

## Obligations

- APSL-2.0 is not a permissive license. Distributing CIRRUS binaries built from
  Apple sources requires preserving notices and complying with APSL source and
  patent terms.
- GPL components impose source-availability and copyleft obligations if
  distributed or linked. This tree keeps them as fetched reference material and
  does not build or link them.
- Linux BSP files are GPL-2.0-only reference sources. Translation into CIRRUS C
  modules may create derivative works; obtain legal review before shipping.
- Apache-2.0 components require retaining NOTICE and license texts.
- MIT components require retaining copyright and permission notices.

This file is engineering documentation, not legal advice.
