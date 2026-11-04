#!/bin/sh
# Compile-check the CIRRUS C ABI headers and the raw trap entry.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CC:-clang}
CXX=${CXX:-clang++}
SRC="$ROOT/tests/c_abi/kumo_trap_static_asserts.c"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/cirrus-c-abi.XXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

# Host C and C++ codegen. On an aarch64 host the C path emits the inline svc.
"$CC" -std=c11 -ffreestanding -Wall -Wextra -Werror -I"$ROOT/include" -c "$SRC" -o "$TMP/host-c.o"
"$CXX" -std=c++17 -ffreestanding -Wall -Wextra -Werror -I"$ROOT/include" -x c++ -c "$SRC" -o "$TMP/host-cxx.o"

# Bare aarch64 C codegen exercises the inline svc on every host. C++ for a bare
# target needs a cross libc++ installation and is checked on the host instead.
"$CC" -target aarch64-none-elf -std=c11 -ffreestanding -Wall -Wextra -Werror -I"$ROOT/include" -c "$SRC" -o "$TMP/aarch64-c.o"

echo "kumo_trap.h: host C/C++ and bare aarch64 C ABI checks passed"
