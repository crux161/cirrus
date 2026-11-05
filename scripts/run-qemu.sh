#!/bin/sh
# Build CIRRUS for QEMU and launch it with the serial console on stdio.
#
# The default is an incremental build of the current tree; every CIRRUS
# artifact (loader, kernel, initrd) is rebuilt from source. Pass --fresh to
# run `cargo clean` first for a from-scratch build.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

usage() {
    cat <<'HELP'
usage: scripts/run-qemu.sh [--fresh] [--no-run] [--timeout SECONDS] [--qemu PATH]

Builds the CIRRUS loader and the KUMO kernel from the current tree, stages
the QEMU virt DTB and KUMORD01 ramdisk, and launches Cirrus with the serial
console attached to stdio. Ctrl-C exits QEMU.

  --fresh     run `cargo clean` first for a from-scratch build
  --no-run    build and stage only; print the artifact paths
  --timeout   marker-mode timeout in seconds, used only without --stdio
  --qemu      qemu-system-aarch64 binary to use
HELP
}

FRESH=0
RUN=1
TIMEOUT=""
QEMU=""
while [ $# -gt 0 ]; do
    case "$1" in
        --fresh|--clean) FRESH=1; shift ;;
        --no-run) RUN=0; shift ;;
        --timeout) TIMEOUT=${2:?--timeout needs seconds}; shift 2 ;;
        --qemu) QEMU=${2:?--qemu needs a path}; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "run-qemu: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

if [ "$FRESH" = 1 ]; then
    echo "run-qemu: cargo clean (fresh build)"
    cargo clean
fi

if [ "$RUN" = 1 ]; then
    set -- --stdio
else
    set -- --no-run
fi
if [ -n "$TIMEOUT" ]; then
    set -- "$@" --timeout "$TIMEOUT"
fi
if [ -n "$QEMU" ]; then
    set -- "$@" --qemu "$QEMU"
fi
exec cargo xtask qemu-loader "$@"
