# AOSP

`manifest/` is a snapshot of `platform/manifest` at `android-16.0.0_r4`
(commit `15128c9e27cfa599c48d294babd39286ee8f1426`), Apache-2.0. AOSP is a
`repo` meta-repository, so only its manifest is committed.

Materialize the tree outside this repository or under `_sources/`:

    repo init -u https://android.googlesource.com/platform/manifest -b android-16.0.0_r4
    repo sync -c -j4

The minimal CIRRUS micro-guest project set has not been defined yet. Do not
treat the default manifest as the final guest configuration.
