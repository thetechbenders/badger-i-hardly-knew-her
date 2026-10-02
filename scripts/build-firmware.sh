#!/usr/bin/env bash
# Cross-build the firmware (default BHIHKH_TARGET=badger2040, the RP2040 Badger 2040).
# Extra args go to CMake, e.g.
#   scripts/build-firmware.sh -DBADGER_PROFILE=local/profile.json -DBADGER_PORTRAIT=local/portrait.png
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build/fw}"
[ -d "$root/deps/pico-sdk" ] || "$root/scripts/fetch-deps.sh"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}" "$@"
cmake --build "$build"
echo
echo "Artifacts in $build:"
ls -1 "$build"/badger_badge.uf2 "$build"/badger_badge-assets.uf2 "$build"/badger_badge.elf \
      "$build"/badger_badge.elf.map "$build"/memory-report.txt "$build"/SHA256SUMS
