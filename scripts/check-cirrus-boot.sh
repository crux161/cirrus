#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
cmake --preset host
cmake --build --preset host
ctest --preset host -R '^(boot-|bootimg-tool|initrd-tool)'
