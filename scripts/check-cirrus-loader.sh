#!/bin/sh
# Build the CIRRUS loader plus the real KUMO kernel and boot the pair under
# QEMU virt (xtask owns the build/QEMU orchestration).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
exec cargo xtask qemu-loader "$@"
