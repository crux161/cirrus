#!/bin/sh
# Stage a UEFI ESP tree for the Orange Pi 5 Plus.
#
# The board boots through its EDK2/UEFI firmware. QEMU cannot emulate RK3588,
# so this is a real-hardware staging path, not a QEMU machine.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
exec cargo xtask stage-esp --board orange-pi-5-plus "$@"
