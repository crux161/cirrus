# Third-party components

CIRRUS vendors the components listed in SEED.md section 3 in two modes:

- **vendored**: a source snapshot is committed under this directory and pinned
  in `manifest.tsv`.
- **fetched**: the upstream revision is pinned in `manifest.tsv`, but the source
  is not committed. `scripts/vendor-third-party.sh fetch NAME` materializes it
  under `_sources/`, which is gitignored.

## Layout

| Path | Contents | Mode |
| --- | --- | --- |
| `apple/` | Libc, Libsystem, dyld, launchd, libdispatch, CF, objc4, xnu header projection, cctools, POSIX utilities | vendored |
| `gnustep/libobjc2/` | portable Objective-C runtime | vendored |
| `halium/libhybris/` | Halium libhybris | vendored |
| `aosp/manifest/` | AOSP platform manifest at `android-16.0.0_r4` | vendored |
| `_sources/` | fetched components: GNUstep Foundation projects, Darling, Mesa, Linux BSP reference, AOSP tree, Android apps | fetched, gitignored |

`manifest.tsv` is the machine-readable pin list. Columns are: name, tier,
upstream, ref, commit, license, disposition, path, notes.

## Reproduce and update

```sh
scripts/vendor-third-party.sh list
scripts/vendor-third-party.sh check            # verify pinned commits exist upstream
scripts/vendor-third-party.sh sync xnu         # refresh a vendored component
scripts/vendor-third-party.sh sync all
scripts/vendor-third-party.sh fetch mesa       # materialize a fetched component
scripts/vendor-third-party.sh fetch all
```

Update the pin in both `manifest.tsv` and the table inside
`scripts/vendor-third-party.sh`, then run the matching `sync` or `fetch`
command, review `git diff`, and commit.

The AOSP tree is a `repo` meta-repository, not a single git repository. Only
its manifest is committed. `fetch aosp-tree` prints the `repo init` and
`repo sync` commands; the minimal CIRRUS project set for the micro-guest is
still to be defined.

## Licensing

The component licenses vary and several are **not permissive**:

- Apple projects are APSL-2.0, Apache-2.0 (launchd, libdispatch), or
  BSD-3-Clause, with mixed per-file headers. APSL is OSI-approved but carries
  source-availability, patent, and termination terms.
- GNUstep `libs-base` and `libs-corebase` use LGPL for library code and GPL
  for tools/tests; consult their pinned per-file notices. They are fetched
  only and not yet linked into CIRRUS.
- GNUstep `tools-make`, Darling, Aurora Store and the F-Droid client are
  listed under GPL in the inventory. They remain fetched only. Source
  selection must distinguish library, tool and component license terms.
- The Linux BSP extraction is GPL-2.0-only reference material. Any translation
  into CIRRUS C modules requires a derivative-work review.
- Mesa and libobjc2 are MIT. libhybris, AOSP, and microG are Apache-2.0.

SEED.md describes this set as permissive. That is inaccurate for the APSL and
GPL components. Per-component license texts are retained in each vendored
directory; Apple projects without a local file are covered by the central
`apple/APPLE_LICENSE`. See `../THIRD_PARTY_NOTICES.md` for the distribution
summary.
