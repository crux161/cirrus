#!/bin/sh
# New Rust belongs in the explicitly vendored KUMO closure only.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
FILES=$(git ls-files --cached --others --exclude-standard -- '*.rs' 'Cargo.toml' |
    while IFS= read -r path; do
        if [ -f "$path" ]; then printf '%s\n' "$path"; fi
    done)
BAD=$(printf '%s\n' "$FILES" | LC_ALL=C grep -Ev \
    '^(Cargo.toml|src/(kernel|hal/(kumo-hal|kumo-hal-aarch64|kumo-hal-x86_64)|lib/(kumo-abi|kumo-bsp|kumo-fdt|kumo-i2c-hid|kumo-ipc|kumo-pdc)|boot/(niji-loader|niji-uefi)|vendor/qrcodegen)/.*|third_party/.*)$' || true)
if [ -n "$BAD" ]; then
    printf 'Rust outside the vendored KUMO boundary:\n%s\n' "$BAD" >&2
    exit 1
fi
echo 'CIRRUS language boundary: passed'
