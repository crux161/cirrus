#!/bin/sh
# Host checks for the CIRRUS boot-plan library.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CC:-clang}
DTC=${DTC:-dtc}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/cirrus-boot.XXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

"$CC" -std=c11 -Wall -Wextra -Werror \
    -I"$ROOT/boot/cirrus-boot/include" \
    "$ROOT/boot/cirrus-boot/src/boot_image.c" \
    "$ROOT/boot/cirrus-boot/src/boot_plan.c" \
    "$ROOT/tests/cirrus_boot_host.c" \
    -o "$TMP/cirrus_boot_host"

"$DTC" -I dts -O dtb -o "$TMP/sdm845-test.dtb" \
    "$ROOT/boot/cirrus-boot/tests/sdm845_test.dts"
"$TMP/cirrus_boot_host" "$TMP/sdm845-test.dtb" synthetic

REAL="$ROOT/resources/linux/dtb/sdm845-oneplus-enchilada.dtb"
if [ -f "$REAL" ]; then
    "$TMP/cirrus_boot_host" "$REAL" real
fi
