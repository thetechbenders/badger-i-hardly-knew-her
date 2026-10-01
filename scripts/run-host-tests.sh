#!/usr/bin/env bash
# Build and run all host-side tests (C++ unit tests + Python tool/render tests)
# and regenerate the public sample previews.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build/host"
# Tests must not pick up private local content.
cmake -S "$root/host" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DBADGER_PROFILE="$root/config/sample-profile.json" \
  -DBADGER_PORTRAIT="$root/assets/sample/portrait_placeholder.png"
cmake --build "$build"
"$build/badger_tests"
python3 "$root/tools/fontgen.py" --fetch --fonts-dir "$root/build/fonts/dejavu" --check --out "$root/firmware/generated/fonts.cpp"
python3 "$root/tools/iconsgen.py" --out "$root/firmware/generated/icons.cpp" --check
BADGER_PREVIEW="$build/badger_preview" python3 -m unittest discover -s "$root/tests_py" -v
python3 "$root/tools/render_previews.py" --preview "$build/badger_preview" --out "$root/build/previews/sample" \
  --profile "$root/config/sample-profile.json"
python3 "$root/tools/render_previews.py" --preview "$build/badger_preview" --out "$root/build/previews/sample-qr" \
  --profile "$root/config/sample-profile.json" --qr "https://example.com/dan" --screens card,qr
# Host diagnostic samples (labelled on screen): worst-case lengths with every
# status/fault state, and the fault-state diagnostics screens.
python3 "$root/tools/render_previews.py" --preview "$build/badger_preview" --out "$root/build/previews/diagnostic-max" \
  --diagnostic-max --preview-arg=--faults --preview-arg=--battery --preview-arg=low --preview-arg=--gesture --preview-arg=fault
