#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
cmake --preset host
cmake --build --preset host --target c_abi_check cxx_abi_check
cmake --preset aarch64
cmake --build --preset aarch64 --target c_smoke cxx_smoke
