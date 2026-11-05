#!/bin/sh
# Host checks for the CIRRUS boot-plan library and the boot-image tool.
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
    "$ROOT/boot/cirrus-boot/src/bootinfo.c" \
    "$ROOT/boot/cirrus-boot/src/initrd.c" \
    "$ROOT/boot/cirrus-boot/src/elf.c" \
    "$ROOT/boot/cirrus-boot/src/console.c" \
    "$ROOT/tests/cirrus_boot_host.c" \
    -o "$TMP/cirrus_boot_host"

"$DTC" -I dts -O dtb -o "$TMP/sdm845-test.dtb" \
    "$ROOT/boot/cirrus-boot/tests/sdm845_test.dts"
"$TMP/cirrus_boot_host" "$TMP/sdm845-test.dtb" synthetic
"$DTC" -I dts -O dtb -o "$TMP/sdm845-test-reversed.dtb" \
    "$ROOT/boot/cirrus-boot/tests/sdm845_test_reversed.dts"
"$TMP/cirrus_boot_host" "$TMP/sdm845-test-reversed.dtb" synthetic

REAL="$ROOT/resources/linux/dtb/sdm845-oneplus-enchilada.dtb"
if [ -f "$REAL" ]; then
    "$TMP/cirrus_boot_host" "$REAL" real
fi

ORANGEPI="$ROOT/src/rk3588-orangepi-5-plus.dtb"
if [ -f "$ORANGEPI" ]; then
    "$TMP/cirrus_boot_host" "$ORANGEPI" orangepi
fi

# The host boot-image tool is the same C packer the build uses.
"$CC" -std=c11 -Wall -Wextra -Werror \
    -I"$ROOT/boot/cirrus-boot/include" \
    "$ROOT/boot/cirrus-boot/src/boot_image.c" \
    "$ROOT/boot/cirrus-boot/tools/bootimg.c" \
    -o "$TMP/bootimg"
printf 'kernel-bytes' > "$TMP/kernel"
printf 'ramdisk-bytes' > "$TMP/ramdisk"
"$TMP/bootimg" pack "$TMP/boot.img" --page-size 2048 --header-version 2 \
    --base 0x80000000 --kernel-offset 0x8000 \
    --ramdisk-offset 0x02000000 --tags-offset 0x01e00000 \
    --cmdline console=ttyMSM0,115200n8 \
    "$TMP/kernel" "$TMP/ramdisk" "$TMP/sdm845-test.dtb" >/dev/null
"$TMP/bootimg" info "$TMP/boot.img" | tee "$TMP/info.txt"
grep -q 'header_version 2' "$TMP/info.txt"
grep -q 'kernel_addr 0x80008000' "$TMP/info.txt"
grep -q 'ramdisk_addr 0x82000000' "$TMP/info.txt"
grep -q 'tags_addr 0x81e00000' "$TMP/info.txt"
grep -q 'dtb_addr 0x0000000080001800' "$TMP/info.txt"
grep -q 'cmdline console=ttyMSM0,115200n8' "$TMP/info.txt"
echo "cirrus-boot: host checks passed"
