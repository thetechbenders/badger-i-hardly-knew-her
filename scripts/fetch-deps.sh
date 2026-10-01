#!/usr/bin/env bash
# Fetch pinned build dependencies into deps/ (git-ignored) and verify commits.
#   scripts/fetch-deps.sh            pico-sdk (+tinyusb), pimoroni-pico, picotool
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
source <(grep -E '^[A-Z_]+=' "$root/deps.lock")
deps="$root/deps"
mkdir -p "$deps"

fetch() {  # name url tag expected_commit
  local dir="$deps/$1"
  if [ ! -d "$dir/.git" ]; then
    git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$3" "$2" "$dir"
  fi
  local got
  got="$(git -C "$dir" rev-parse HEAD)"
  if [ -n "${4:-}" ] && [ "$got" != "$4" ]; then
    echo "error: $1 is at $got, expected $4 (tag $3 moved or local changes)" >&2
    exit 1
  fi
  echo "$1 $3 $got"
}

fetch pico-sdk "$PICO_SDK_URL" "$PICO_SDK_TAG" "$PICO_SDK_COMMIT"
git -C "$deps/pico-sdk" submodule update --init --depth 1 lib/tinyusb >/dev/null
tu="$(git -C "$deps/pico-sdk/lib/tinyusb" rev-parse HEAD)"
[ "$tu" = "$TINYUSB_COMMIT" ] || { echo "error: tinyusb at $tu, expected $TINYUSB_COMMIT" >&2; exit 1; }
echo "tinyusb $tu"
fetch pimoroni-pico "$PIMORONI_PICO_URL" "$PIMORONI_PICO_TAG" "$PIMORONI_PICO_COMMIT"
fetch picotool "$PICOTOOL_URL" "$PICOTOOL_TAG" "$PICOTOOL_COMMIT"

if ! command -v arm-none-eabi-gcc >/dev/null; then
  echo "warning: arm-none-eabi-gcc not found (see docs/BUILD.md)" >&2
else
  v="$(arm-none-eabi-gcc -dumpversion)"
  [ "$v" = "$ARM_GCC_VERSION" ] || echo "warning: arm-none-eabi-gcc $v, CI uses $ARM_GCC_VERSION" >&2
fi

# Build picotool (host tool used by the SDK to produce UF2 files). USB device
# features need libusb-1.0-dev; UF2 generation works without it.
if [ ! -x "$deps/picotool-install/picotool/picotool" ]; then
  cmake -S "$deps/picotool" -B "$deps/picotool-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPICO_SDK_PATH="$deps/pico-sdk" -DCMAKE_INSTALL_PREFIX="$deps/picotool-install" \
    -DPICOTOOL_FLAT_INSTALL=1 >/dev/null
  cmake --build "$deps/picotool-build" >/dev/null
  cmake --install "$deps/picotool-build" >/dev/null
fi
echo "picotool $("$deps/picotool-install/picotool/picotool" version | head -1)"
