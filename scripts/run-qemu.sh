#!/bin/sh
# Configure/build with CMake, then attach the QEMU serial console.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
FRESH=0
RUN=1
MODE=--stdio
TIMEOUT=90
QEMU_TOOL=${QEMU:-qemu-system-aarch64}
while [ $# -gt 0 ]; do
    case "$1" in
        --fresh|--clean) FRESH=1; shift ;;
        --no-run) RUN=0; shift ;;
        --check) MODE=--check; shift ;;
        --timeout) TIMEOUT=${2:?--timeout needs seconds}; shift 2 ;;
        --qemu) QEMU_TOOL=${2:?--qemu needs a path}; shift 2 ;;
        -h|--help)
            echo 'usage: scripts/run-qemu.sh [--fresh] [--no-run] [--check] [--timeout SECONDS] [--qemu PATH]'
            exit 0 ;;
        *) echo "run-qemu: unknown option: $1" >&2; exit 2 ;;
    esac
done
if [ "$FRESH" = 1 ]; then
    cmake --preset qemu --fresh
    if [ -f "$ROOT/build/qemu/aarch64/CMakeCache.txt" ]; then
        cmake --build "$ROOT/build/qemu/aarch64" --target clean
    fi
    cmake --build --preset qemu --target clean
    # Cargo clean is deliberately limited to this preset's kernel output.
    cargo clean --target-dir "$ROOT/build/qemu/kumo"
else
    cmake --preset qemu
fi
cmake --build --preset qemu
if [ "$RUN" = 0 ]; then
    printf 'Loader: %s\nKernel: %s\nInitrd: %s\n' \
        "$ROOT/build/qemu/aarch64/loader.elf" \
        "$ROOT/build/qemu/kumo/aarch64-unknown-none/release/kumo-kernel" \
        "$ROOT/build/qemu/initrd.img"
    exit 0
fi
exec "$ROOT/scripts/qemu-session.sh" "$ROOT/build/qemu" "$MODE" --timeout "$TIMEOUT" --qemu "$QEMU_TOOL"
