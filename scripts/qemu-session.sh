#!/bin/sh
# Run a CMake-built image. Does not compile or depend on a Rust task runner.
set -eu
BUILD=${1:?usage: qemu-session.sh BUILD_DIR [--check|--stdio] [--timeout N] [--qemu PATH] [--cxx]}
shift
MODE=check
TIMEOUT=90
QEMU_TOOL=${QEMU:-qemu-system-aarch64}
FDTPUT_TOOL=${FDTPUT:-fdtput}
INITRD="$BUILD/initrd.img"
SMOKE_MARKER='CIRRUS C smoke: svc #0 trapped and returned'
while [ $# -gt 0 ]; do
    case "$1" in
        --check) MODE=check; shift ;;
        --stdio) MODE=stdio; shift ;;
        --timeout) TIMEOUT=${2:?--timeout needs seconds}; shift 2 ;;
        --qemu) QEMU_TOOL=${2:?--qemu needs a path}; shift 2 ;;
        --cxx) INITRD="$BUILD/initrd-cxx.img"; SMOKE_MARKER='CIRRUS C++ smoke: raw trap returned'; shift ;;
        *) echo "Unknown QEMU session option: $1" >&2; exit 2 ;;
    esac
done
case "$TIMEOUT" in ''|*[!0-9]*|0) echo 'Timeout must be a positive integer' >&2; exit 2 ;; esac
DTB="$BUILD/qemu.dtb"
"$QEMU_TOOL" -M "virt,gic-version=3,dumpdtb=$DTB" -cpu cortex-a72 -m 1024 \
    -display none -monitor none -serial none
"$FDTPUT_TOOL" -c -p "$DTB" /chosen
SIZE=$(wc -c < "$INITRD" | tr -d ' ')
END=$(printf '%x' "$((0x50000000 + SIZE))")
"$FDTPUT_TOOL" -t x -p "$DTB" /chosen linux,initrd-start 50000000
"$FDTPUT_TOOL" -t x -p "$DTB" /chosen linux,initrd-end "$END"
set -- -M virt,gic-version=3 -cpu cortex-a72 -m 1024 -display none -monitor none \
    -no-reboot -kernel "$BUILD/aarch64/loader.elf" \
    -device "loader,file=$DTB,addr=0x47000000,force-raw=on" \
    -device "loader,file=$INITRD,addr=0x50000000,force-raw=on"
if [ "$MODE" = stdio ]; then
    exec "$QEMU_TOOL" "$@" -serial stdio
fi
LOG="$BUILD/serial.log"
: > "$LOG"
"$QEMU_TOOL" "$@" -serial "file:$LOG" </dev/null &
QEMU_PID=$!
cleanup() { kill "$QEMU_PID" 2>/dev/null || true; wait "$QEMU_PID" 2>/dev/null || true; }
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
DEADLINE=$(($(date +%s) + TIMEOUT))
while :; do
    if grep -Eq 'CIRRUS loader: FAIL|CIRRUS C.*FAIL|USERLAND  EL0.*FAIL|handoff      : INVALID|returning to firmware|Kernel panic|panicked at|TOWER' "$LOG"; then
        cat "$LOG"; echo 'QEMU smoke failed' >&2; exit 1
    fi
    if grep -Fq 'CIRRUS loader: entry' "$LOG" &&
       grep -Fq 'CIRRUS loader: entering high half' "$LOG" &&
       grep -Fq "$SMOKE_MARKER" "$LOG" &&
       grep -Eq 'USERLAND  EL0.*exited +OK' "$LOG"; then
        if [ "$INITRD" = "$BUILD/initrd-cxx.img" ] ||
           { grep -Fq 'CIRRUS C IPC ring: spsc ok' "$LOG" &&
             grep -Fq 'CIRRUS C VMO IPC: cross-process ok' "$LOG"; }; then
            cat "$LOG"; echo 'QEMU smoke passed'; exit 0
        fi
    fi
    if ! kill -0 "$QEMU_PID" 2>/dev/null || [ "$(date +%s)" -ge "$DEADLINE" ]; then
        cat "$LOG"; echo "QEMU exited early or timed out after ${TIMEOUT}s" >&2; exit 1
    fi
    sleep 0.1
done
