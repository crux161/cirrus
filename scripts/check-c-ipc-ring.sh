#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
cmake --preset host
cmake --build --preset host --target c_ipc_ring_host
ctest --preset host -R '^ipc-ring$'
