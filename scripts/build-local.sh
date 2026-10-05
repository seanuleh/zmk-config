#!/usr/bin/env bash
# Build all sofled firmware locally via the ZMK build container.
# Run from the repo root. Outputs UF2s under build/{left,right}/zephyr/.
#
# Usage:
#   scripts/build-local.sh            # build both halves
#   scripts/build-local.sh left       # build only left
#   scripts/build-local.sh right
#
# First run does `west update` (slow, downloads modules). Subsequent runs
# reuse the workspace and finish in ~15-30s each.

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
IMG="zmkfirmware/zmk-build-arm:stable"

run_in_container() {
  docker run --rm -v "$REPO:/zmk-config" -w /zmk-config "$IMG" bash -c "$1"
}

if [ ! -d "$REPO/.west" ]; then
  echo "Initializing west workspace (first run only)..."
  run_in_container "west init -l /zmk-config/config && west update && west zephyr-export"
fi

build_target() {
  local name="$1" board="$2" shield="$3" extra="${4:-}"
  mkdir -p "$REPO/build/$name"
  echo "=== building $name ==="
  run_in_container "source /zmk-config/zephyr/zephyr-env.sh && west zephyr-export 2>/dev/null && west build -s zmk/app -d build/$name -b \"$board\" -- -DSHIELD=\"$shield\" -DZMK_CONFIG=/zmk-config/config -DBOARD_ROOT=/zmk-config $extra"
  echo "→ build/$name/zephyr/zmk.uf2"
}

target="${1:-all}"
# George's board: no dongle, no displays, no LEDs. Left is central.
LEFT=(left "nice_nano/nrf52840/zmk" "sofled_left" "-DCONFIG_ZMK_SPLIT_ROLE_CENTRAL=y")
RIGHT=(right "nice_nano/nrf52840/zmk" "sofled_right")
case "$target" in
  left)  build_target "${LEFT[@]}" ;;
  right) build_target "${RIGHT[@]}" ;;
  all)
    build_target "${LEFT[@]}"
    build_target "${RIGHT[@]}"
    ;;
  *) echo "usage: $0 [left|right|all]" >&2; exit 1 ;;
esac
