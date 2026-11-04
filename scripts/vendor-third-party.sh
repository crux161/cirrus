#!/bin/sh
# Reproducible third-party vendoring for CIRRUS.
#
# Modes:
#   scripts/vendor-third-party.sh list
#   scripts/vendor-third-party.sh check [name|all]
#   scripts/vendor-third-party.sh sync <name|all>    # refresh vendored sources
#   scripts/vendor-third-party.sh fetch <name|all>   # materialize fetched pins under _sources/
#
# Pins mirror third_party/manifest.tsv. Update both together.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TP="$ROOT/third_party"
SOURCES="$TP/_sources"
TMP="${TP_TMP:-${TMPDIR:-/tmp}/cirrus-third-party}"

COMPONENTS="Libc Libsystem dyld launchd libdispatch CF objc4 xnu shell_cmds file_cmds text_cmds basic_cmds adv_cmds cctools libobjc2 tools-make libs-base libs-corebase darling mesa linux-bsp libhybris aosp-manifest aosp-tree microg aurora-store fdroid-client"
LINUX_SPARSE="arch/arm64/boot/dts/qcom arch/arm64/boot/dts/rockchip drivers/clk/qcom drivers/clk/rockchip drivers/pinctrl/qcom drivers/regulator include/dt-bindings"

info() {
    NAME=$1
    SPARSE=""
    case "$NAME" in
    Libc) URL="https://github.com/apple-oss-distributions/Libc"; REF="Libc-1752.120.2"; COMMIT="4e34d0559e3a1b081afeb8604d9e204a1f31321d"; MODE="vendored"; DEST="apple/Libc" ;;
    Libsystem) URL="https://github.com/apple-oss-distributions/Libsystem"; REF="Libsystem-1356"; COMMIT="c23b51183e46e613375dc7639fb4bf11fbf59ba8"; MODE="vendored"; DEST="apple/Libsystem" ;;
    dyld) URL="https://github.com/apple-oss-distributions/dyld"; REF="dyld-1378"; COMMIT="fd8d0c4d52320ebf64db34f3cb280310d905c5ae"; MODE="vendored"; DEST="apple/dyld" ;;
    launchd) URL="https://github.com/apple-oss-distributions/launchd"; REF="launchd-842.92.1"; COMMIT="49a9c79b2949192dfca697b1054245cdfb645788"; MODE="vendored"; DEST="apple/launchd" ;;
    libdispatch) URL="https://github.com/apple-oss-distributions/libdispatch"; REF="libdispatch-1542.100.32"; COMMIT="701f4d1a24ae9c6863901bbbb22624b7d1b87321"; MODE="vendored"; DEST="apple/libdispatch" ;;
    CF) URL="https://github.com/apple-oss-distributions/CF"; REF="CF-1153.18"; COMMIT="dc54c6bb1c1e5e0b9486c1d26dd5bef110b20bf3"; MODE="vendored"; DEST="apple/CF" ;;
    objc4) URL="https://github.com/apple-oss-distributions/objc4"; REF="objc4-951.7"; COMMIT="ebfe77e64331034c867285a95d3ac205203291d5"; MODE="vendored"; DEST="apple/objc4" ;;
    xnu) URL="https://github.com/apple-oss-distributions/xnu"; REF="xnu-12377.121.6"; COMMIT="ac9718fb1af618d5ce8678d0dc6e8a58f252216f"; MODE="vendored"; DEST="apple/xnu"; SPARSE="EXTERNAL_HEADERS bsd/sys bsd/net bsd/netinet bsd/netinet6 bsd/arm bsd/vm bsd/pthread bsd/uuid bsd/security osfmk/mach osfmk/mach_debug osfmk/kern osfmk/arm64 osfmk/machine osfmk/vm libkern/libkern libkern/os libsyscall" ;;
    shell_cmds) URL="https://github.com/apple-oss-distributions/shell_cmds"; REF="shell_cmds-329"; COMMIT="298787009e5432c5e4c378a077f98267077e3495"; MODE="vendored"; DEST="apple/shell_cmds" ;;
    file_cmds) URL="https://github.com/apple-oss-distributions/file_cmds"; REF="file_cmds-479"; COMMIT="659a8a301e2acf0343f8b8673a154a2ca4d07084"; MODE="vendored"; DEST="apple/file_cmds" ;;
    text_cmds) URL="https://github.com/apple-oss-distributions/text_cmds"; REF="text_cmds-199"; COMMIT="592aaf8a50aa5810ee8183df20f0ba48bb23aa7e"; MODE="vendored"; DEST="apple/text_cmds" ;;
    basic_cmds) URL="https://github.com/apple-oss-distributions/basic_cmds"; REF="basic_cmds-70"; COMMIT="c8ca3d0842a5f1376579af96c233e4ad9d58f023"; MODE="vendored"; DEST="apple/basic_cmds" ;;
    adv_cmds) URL="https://github.com/apple-oss-distributions/adv_cmds"; REF="adv_cmds-237"; COMMIT="6bed8737a34dbb54782a18f47dccf933a9967a12"; MODE="vendored"; DEST="apple/adv_cmds" ;;
    cctools) URL="https://github.com/apple-oss-distributions/cctools"; REF="cctools-1035.1.102"; COMMIT="e0d56624eca2a76c2ace4c21850df9e666de4ca5"; MODE="vendored"; DEST="apple/cctools" ;;
    libobjc2) URL="https://github.com/gnustep/libobjc2"; REF="v2.3"; COMMIT="e877e782fb965c4e870d2ecf6aff58dddc6290ae"; MODE="vendored"; DEST="gnustep/libobjc2" ;;
    tools-make) URL="https://github.com/gnustep/tools-make"; REF="master"; COMMIT="cafd2c4a73b568b2f0022f15a149963f39c564f3"; MODE="fetched"; DEST="gnustep/tools-make" ;;
    libs-base) URL="https://github.com/gnustep/libs-base"; REF="master"; COMMIT="818b4b8ddd1b97dd7269136425cbd63da9e73622"; MODE="fetched"; DEST="gnustep/libs-base" ;;
    libs-corebase) URL="https://github.com/gnustep/libs-corebase"; REF="master"; COMMIT="67a6f90601e7b143f173d1403f9a381a6d968fc2"; MODE="fetched"; DEST="gnustep/libs-corebase" ;;
    darling) URL="https://github.com/darlinghq/darling"; REF="v0.1.20260608"; COMMIT="e947f0d5a3c6bba27e3631d9bf0da2a9840e6894"; MODE="fetched"; DEST="darling" ;;
    mesa) URL="https://gitlab.freedesktop.org/mesa/mesa"; REF="mesa-26.2.6"; COMMIT="8e87a503ebd91408c88596d1b9eeb5f4c0b2088d"; MODE="fetched"; DEST="mesa-26.2.6" ;;
    linux-bsp) URL="https://github.com/gregkh/linux"; REF="v6.18.60"; COMMIT="f45ed76d6791cf72ff1265ee93511175bbbc2f18"; MODE="fetched"; DEST="linux-6.18.60" ;;
    libhybris) URL="https://github.com/halium/libhybris"; REF="master"; COMMIT="f0a608f96dd301f497b657c94b73ab76645de59c"; MODE="vendored"; DEST="halium/libhybris" ;;
    aosp-manifest) URL="https://android.googlesource.com/platform/manifest"; REF="android-16.0.0_r4"; COMMIT="15128c9e27cfa599c48d294babd39286ee8f1426"; MODE="vendored"; DEST="aosp/manifest" ;;
    aosp-tree) URL="https://android.googlesource.com/platform/manifest"; REF="android-16.0.0_r4"; COMMIT="15128c9e27cfa599c48d294babd39286ee8f1426"; MODE="fetched"; DEST="aosp/tree" ;;
    microg) URL="https://github.com/microg/GmsCore"; REF="v0.3.17.252432"; COMMIT="32bc8954ff872d0e1a05ddfae81561b6dbecef69"; MODE="fetched"; DEST="android/GmsCore" ;;
    aurora-store) URL="https://github.com/AuroraOSS/AuroraStore"; REF="4.8.4"; COMMIT="ad3d349edce82e119ba239f3a07b3afe028abd19"; MODE="fetched"; DEST="android/AuroraStore" ;;
    fdroid-client) URL="https://gitlab.com/fdroid/fdroidclient"; REF="v0.104"; COMMIT="eabec87a4ce7a08af67447a8aed58212a417b472"; MODE="fetched"; DEST="android/fdroidclient" ;;
    *) echo "unknown component: $NAME" >&2; return 1 ;;
    esac
}

clone_full() {
    url=$1; ref=$2; commit=$3; dest=$4
    rm -rf "$dest"
    mkdir -p "$(dirname "$dest")"
    if git clone -q --depth 1 --branch "$ref" "$url" "$dest" 2>/dev/null; then
        if [ "$(git -C "$dest" rev-parse HEAD)" != "$commit" ]; then
            git -C "$dest" fetch -q --depth 1 origin "$commit" 2>/dev/null || true
            git -C "$dest" checkout -q "$commit" 2>/dev/null || \
                echo "warning: $dest at $(git -C "$dest" rev-parse HEAD), expected $commit" >&2
        fi
    else
        git clone -q --filter=blob:none --no-checkout "$url" "$dest"
        git -C "$dest" checkout -q "$commit"
    fi
}

clone_sparse() {
    url=$1; ref=$2; commit=$3; dest=$4; paths=$5
    rm -rf "$dest"
    mkdir -p "$(dirname "$dest")"
    git clone -q --depth 1 --branch "$ref" --filter=blob:none --sparse "$url" "$dest"
    git -C "$dest" sparse-checkout set $paths
    actual=$(git -C "$dest" rev-parse HEAD)
    [ "$actual" = "$commit" ] || echo "warning: $dest at $actual, expected $commit" >&2
}

sync_xnu() {
    tmp="$TMP/sync/xnu"
    clone_sparse "$URL" "$REF" "$COMMIT" "$tmp" "$SPARSE"
    proj="$TMP/sync/xnu-proj"
    rm -rf "$proj"
    mkdir -p "$proj"
    rsync -a --prune-empty-dirs --include='*/' --include='*.h' --exclude='*' \
        "$tmp/bsd" "$tmp/osfmk" "$tmp/EXTERNAL_HEADERS" "$tmp/libkern" "$proj/"
    rsync -a "$tmp/libsyscall/" "$proj/libsyscall/"
    cp "$tmp/APPLE_LICENSE" "$tmp/README.md" "$proj/"
    mkdir -p "$TP/$DEST"
    rsync -a --delete --exclude='PROJECTION.md' "$proj/" "$TP/$DEST/"
}

sync_one() {
    info "$1"
    if [ "$MODE" != "vendored" ]; then
        echo "$NAME is fetched; use fetch" >&2
        return 1
    fi
    if [ "$NAME" = "xnu" ]; then
        sync_xnu
    else
        tmp="$TMP/sync/$NAME"
        clone_full "$URL" "$REF" "$COMMIT" "$tmp"
        mkdir -p "$TP/$DEST"
        rsync -a --delete --exclude='.git' "$tmp/" "$TP/$DEST/"
    fi
    echo "synced $NAME -> third_party/$DEST"
}

fetch_one() {
    info "$1"
    if [ "$MODE" != "fetched" ]; then
        echo "$NAME is vendored; use sync" >&2
        return 1
    fi
    dest="$SOURCES/$DEST"
    case "$NAME" in
    mesa)
        rm -rf "$dest"
        mkdir -p "$dest" "$TMP"
        tarball="$TMP/$REF.tar.xz"
        curl -fL --retry 3 -o "$tarball" "https://archive.mesa3d.org/$REF.tar.xz"
        tar -xf "$tarball" -C "$dest" --strip-components=1
        ;;
    linux-bsp)
        clone_sparse "$URL" "$REF" "$COMMIT" "$dest" "$LINUX_SPARSE"
        ;;
    aosp-tree)
        echo "AOSP tree is repo-managed; run:"
        echo "  mkdir -p $dest && cd $dest"
        echo "  repo init -u $URL -b $REF"
        echo "  repo sync -c -j4"
        return 0
        ;;
    *)
        clone_full "$URL" "$REF" "$COMMIT" "$dest"
        ;;
    esac
    echo "fetched $NAME -> third_party/_sources/$DEST"
}

check_one() {
    info "$1"
    if git ls-remote "$URL" 2>/dev/null | grep -q "^$COMMIT"; then
        echo "ok   $NAME"
    else
        echo "MISS $NAME ($COMMIT)" >&2
        return 1
    fi
}

case "${1:-list}" in
list)
    printf '%-14s %-9s %-24s %s\n' NAME MODE REF UPSTREAM
    for c in $COMPONENTS; do
        info "$c"
        printf '%-14s %-9s %-24s %s\n' "$NAME" "$MODE" "$REF" "$URL"
    done
    ;;
check)
    target="${2:-all}"
    fail=0
    if [ "$target" = "all" ]; then
        for c in $COMPONENTS; do check_one "$c" || fail=1; done
    else
        check_one "$target" || fail=1
    fi
    exit "$fail"
    ;;
sync)
    target="${2:-}"
    [ -n "$target" ] || { echo "usage: $0 sync <name|all>" >&2; exit 2; }
    if [ "$target" = "all" ]; then
        for c in $COMPONENTS; do
            info "$c"
            if [ "$MODE" = "vendored" ]; then sync_one "$c"; fi
        done
    else
        sync_one "$target"
    fi
    ;;
fetch)
    target="${2:-}"
    [ -n "$target" ] || { echo "usage: $0 fetch <name|all>" >&2; exit 2; }
    if [ "$target" = "all" ]; then
        for c in $COMPONENTS; do
            info "$c"
            if [ "$MODE" = "fetched" ]; then fetch_one "$c"; fi
        done
    else
        fetch_one "$target"
    fi
    ;;
*)
    echo "usage: $0 {list|check|sync|fetch} [name|all]" >&2
    exit 2
    ;;
esac
