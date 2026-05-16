#!/usr/bin/env bash
# Local ZMK build using the same Docker image as GitHub Actions.
# Usage: ./build-local.sh [left|right|dongle]  (default: all three)
set -euo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
IMAGE="zmkfirmware/zmk-build-arm:stable"
TARGET="${1:-all}"

docker pull "$IMAGE"

docker run --rm \
    -e TARGET="$TARGET" \
    -v "$REPO:/zmk-config" \
    "$IMAGE" \
    bash -lc "$(cat <<'SCRIPT'
set -euo pipefail
TARGET="${TARGET:-all}"

ZMK_CONFIG_SRC=/zmk-config/config
ZMK_EXTRA_MODULES=/zmk-config
BASE=/tmp/zmk-ws

mkdir -p "$BASE/config"
cp -R "$ZMK_CONFIG_SRC/." "$BASE/config/"

cd "$BASE"
west init -l "$BASE/config"
west update --fetch-opt=--filter=tree:0
west zephyr-export

_build() {
    local board="$1" shield="$2" out="$3"
    local extra=()
    [[ -n "$shield" ]] && extra+=("-DSHIELD=$shield")
    echo ""
    echo "=== Building $out (board=$board${shield:+ shield=$shield}) ==="
    west build -s zmk/app -d "$BASE/build/$out" -b "$board" -- \
        "${extra[@]}" \
        -DZMK_CONFIG="$BASE/config" \
        -DZMK_EXTRA_MODULES="$ZMK_EXTRA_MODULES"
    cp "$BASE/build/$out/zephyr/zmk.uf2" "/zmk-config/$out.uf2"
    echo "  → /zmk-config/$out.uf2"
}

case "${TARGET:-all}" in
    left)   _build glove80_lh     ""             glove80_left   ;;
    right)  _build glove80_rh     ""             glove80_right  ;;
    dongle) _build nice_nano//zmk glove80_dongle glove80_dongle ;;
    reset)
        _build glove80_lh ""             glove80_left_reset
        _build glove80_rh ""             glove80_right_reset
        ;;
    all)
        _build glove80_lh     ""             glove80_left
        _build glove80_rh     ""             glove80_right
        _build nice_nano//zmk glove80_dongle glove80_dongle
        _build glove80_lh     settings_reset glove80_left_reset
        _build glove80_rh     settings_reset glove80_right_reset
        ;;
    *) echo "Unknown target: ${TARGET}. Use: left|right|dongle|reset|all" >&2; exit 1 ;;
esac

if [[ "${TARGET:-all}" == "all" ]]; then
    echo ""
    echo "=== Bundling ==="
    cat /zmk-config/glove80_left.uf2 /zmk-config/glove80_right.uf2 /zmk-config/glove80_dongle.uf2 \
        > /zmk-config/glove80_bundle.uf2
    echo "  → /zmk-config/glove80_bundle.uf2"
    cat /zmk-config/glove80_left_reset.uf2 /zmk-config/glove80_right_reset.uf2 \
        > /zmk-config/glove80_reset_bundle.uf2
    echo "  → /zmk-config/glove80_reset_bundle.uf2"
fi

echo ""
echo "Done."
SCRIPT
)"
