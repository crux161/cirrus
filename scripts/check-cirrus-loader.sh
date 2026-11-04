#!/bin/sh
# Build and run the cirrus-boot loader entry test under QEMU virt.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CC:-clang}
LD=${LD:-ld.lld}
QEMU=${QEMU:-qemu-system-aarch64}
TIMEOUT=${TIMEOUT:-timeout}

TMP=$(mktemp -d "${TMPDIR:-/tmp}/cirrus-loader.XXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

FLAGS="--target=aarch64-none-elf -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mgeneral-regs-only -O2 -DCIRRUS_LOADER_TEST"
for src in \
    "$ROOT/boot/cirrus-boot/src/entry.S" \
    "$ROOT/boot/cirrus-boot/src/stub.S" \
    "$ROOT/boot/cirrus-boot/src/loader.c" \
    "$ROOT/boot/cirrus-boot/src/boot_image.c" \
    "$ROOT/boot/cirrus-boot/src/boot_plan.c" \
    "$ROOT/boot/cirrus-boot/src/bootinfo.c"; do
    obj="$TMP/$(basename "$src").o"
    $CC $FLAGS -I"$ROOT/boot/cirrus-boot/include" -c "$src" -o "$obj"
done

$LD -T "$ROOT/boot/cirrus-boot/loader.ld" -static -nostdlib -o "$TMP/loader.elf" \
    "$TMP/entry.S.o" "$TMP/stub.S.o" "$TMP/loader.c.o" \
    "$TMP/boot_image.c.o" "$TMP/boot_plan.c.o" "$TMP/bootinfo.c.o"

DTB="$TMP/qemu.dtb"
$TIMEOUT 5 "$QEMU" -M virt,dumpdtb="$DTB" -cpu cortex-a72 -m 1024 \
    -display none -monitor none -serial none >/dev/null 2>&1 || true
if [ ! -s "$DTB" ]; then
    echo "could not generate QEMU virt DTB"
    exit 1
fi

LOG="$TMP/serial.log"
$TIMEOUT 10 "$QEMU" -M virt -cpu cortex-a72 -m 1024 -display none \
    -monitor none -no-reboot -serial "file:$LOG" -kernel "$TMP/loader.elf" \
    -device "loader,file=$DTB,addr=0x47000000,force-raw=on" \
    >/dev/null 2>&1 || true

if ! grep -a "CIRRUS loader entry OK" "$LOG" >/dev/null 2>&1; then
    echo "loader entry test failed; serial:"
    cat "$LOG" 2>/dev/null || true
    exit 1
fi
cat "$LOG"
echo "cirrus-loader: entry test passed"
