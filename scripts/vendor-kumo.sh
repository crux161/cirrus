#!/bin/sh
# Re-import the CIRRUS-vendored KUMO kernel closure from upstream.
#
# Usage: scripts/vendor-kumo.sh [commit]
#
# Defaults to the pin recorded in UPSTREAM.md. Requires git, rsync, and access
# to the upstream repository. Replaces the contents of the imported directories
# under src/ (rsync --delete); review `git diff` afterwards before committing.
set -eu

REPO_URL="${KUMO_REPO_URL:-https://github.com/crux161/kumo}"
PIN="${1:-ce6d6285fe7e248549b6c9c2c86bf77fcc6427fa}"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP="${KUMO_VENDOR_TMP:-${TMPDIR:-/tmp}/cirrus-kumo-vendor}"

PATHS="kernel hal lib/kumo-abi lib/kumo-bsp lib/kumo-fdt lib/kumo-i2c-hid lib/kumo-ipc lib/kumo-pdc boot/niji-loader vendor/qrcodegen"
ROOT_FILES="rk3588-orangepi-5-plus.dtb sc8280xp-lenovo-thinkpad-x13s.dtb"

rm -rf "$TMP"
mkdir -p "$TMP"
git clone --filter=blob:none --no-checkout "$REPO_URL" "$TMP/kumo"
git -C "$TMP/kumo" checkout "$PIN" -- $PATHS $ROOT_FILES

for p in $PATHS; do
    mkdir -p "$ROOT/src/$(dirname "$p")"
    rsync -a --delete "$TMP/kumo/$p/" "$ROOT/src/$p/"
done
for f in $ROOT_FILES; do
    cp "$TMP/kumo/$f" "$ROOT/src/$f"
done

echo "Vendored $REPO_URL @ $PIN into $ROOT/src"
echo "Next: cd $ROOT && git status && git diff --stat"
