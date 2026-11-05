#!/bin/sh
# Temporary upstream UEFI adapter; the native Cirrus bootloader is C.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/build/orangepi/esp/orange-pi-5-plus"
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT=${2:?--out needs a path}; shift 2 ;;
        *) echo 'usage: scripts/stage-orangepi5.sh [--out DIR]' >&2; exit 2 ;;
    esac
done
cmake -S "$ROOT" -B "$ROOT/build/orangepi" -G Ninja \
    -DCIRRUS_BUILD_SYSTEM=ON -DCIRRUS_LEGACY_UEFI=ON -DCIRRUS_BOARD=qemu-virt-aarch64 \
    "-DCIRRUS_LEGACY_ESP_DIR=$OUT"
cmake --build "$ROOT/build/orangepi" --target legacy-uefi-esp
