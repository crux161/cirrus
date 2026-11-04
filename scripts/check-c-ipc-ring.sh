#!/bin/sh
# Host-side check for the CIPC SPSC ring implementation.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CC:-clang}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/cirrus-ipc-ring.XXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

"$CC" -std=c11 -Wall -Wextra -Werror -I"$ROOT/include" \
    "$ROOT/tests/c_ipc_ring_host.c" "$ROOT/userland/ipc_ring.c" \
    -o "$TMP/c_ipc_ring_host"
"$TMP/c_ipc_ring_host"
