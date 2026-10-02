#!/usr/bin/env bash
# Cross-build the firmware. The hardware target comes from -DBHIHKH_TARGET=...
# or -DBHIHKH_TARGET:STRING=... (or the BHIHKH_TARGET environment variable);
# the default is badger2040, the original Badger 2040. Extra args go to CMake,
# e.g.
#   scripts/build-firmware.sh -DBADGER_PROFILE=local/profile.json -DBADGER_PORTRAIT=local/portrait.png
#   scripts/build-firmware.sh -DBHIHKH_TARGET=badger2350
# Output: build/fw/ for badger2040, build/fw-<target>/ for other targets
# (BUILD_DIR overrides), so the boards' artifacts never share a directory.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
target="${BHIHKH_TARGET:-badger2040}"
for a in "$@"; do
  case "$a" in -DBHIHKH_TARGET=* | -DBHIHKH_TARGET:*=*) target="${a#*=}" ;; esac
done
case "$target" in
  badger2040) default_build="$root/build/fw"; prefix=badger_badge ;;
  badger2350) default_build="$root/build/fw-badger2350"; prefix=badger2350_badge ;;
  *) echo "error: unknown BHIHKH_TARGET '$target' (badger2040, badger2350)" >&2; exit 2 ;;
esac
build="${BUILD_DIR:-$default_build}"
[ -d "$root/deps/pico-sdk" ] || "$root/scripts/fetch-deps.sh"
# The checked target goes last: CMake keeps the last -D for a variable, so it
# builds exactly the board whose directory and artifact names are used here.
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}" "$@" -DBHIHKH_TARGET="$target"
cmake --build "$build"
echo
echo "Artifacts for $target in $build:"
ls -1 "$build/$prefix.uf2" "$build/$prefix-assets.uf2" "$build/$prefix.elf" \
      "$build/$prefix.elf.map" "$build"/memory-report.txt "$build"/SHA256SUMS
