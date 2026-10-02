#!/usr/bin/env bash
# Build and run all host-side tests (C++ unit tests + Python tool/render tests)
# and regenerate the public sample previews, for every hardware target:
# build/host + build/previews/ for the Badger 2040 (badger2040),
# build/host-badger2350 + build/previews/badger2350/ for the Badger 2350.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"

host_build() {  # target build-dir portrait
  # Tests must not pick up private local content.
  cmake -S "$root/host" -B "$2" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBHIHKH_TARGET="$1" \
    -DBADGER_PROFILE="$root/config/sample-profile.json" -DBADGER_PORTRAIT="$3"
  cmake --build "$2"
  "$2/badger_tests"
}

previews() {  # preview-binary out-dir target
  local P=(python3 "$root/tools/render_previews.py" --preview "$1")
  "${P[@]}" --out "$2/sample" --profile "$root/config/sample-profile.json"
  "${P[@]}" --out "$2/sample-qr" --profile "$root/config/sample-profile.json" --qr "https://example.com/alex" \
    --screens card,qr
  # Project index at its maximum size: the sample portfolio padded to 12 entries
  # with labelled placeholders (its last entry stays last).
  "${P[@]}" --out "$2/index-12" --profile "$root/config/sample-profile.json" --example-projects 12 \
    --screens index,projects
  # Host diagnostic samples (labelled on screen): worst-case lengths with every
  # status/fault state, and the fault-state diagnostics screens.
  "${P[@]}" --out "$2/diagnostic-max" --diagnostic-max --preview-arg=--faults --preview-arg=--battery \
    --preview-arg=low --preview-arg=--gesture --preview-arg=fault
  # The public fill-in form template, through the same pipeline a user runs
  # (validation, portrait, fit and QR gates, previews); generic content only.
  python3 "$root/tools/badge_form.py" preview "$root/config/badge-form.toml" --target "$3" --out "$2/form-template"
}

host_build badger2040 "$root/build/host" "$root/assets/sample/portrait_placeholder.png"
host_build badger2350 "$root/build/host-badger2350" "$root/assets/sample/portrait_placeholder_badger2350.png"
python3 "$root/tools/fontgen.py" --fetch --fonts-dir "$root/build/fonts/dejavu" --check --out "$root/firmware/generated/fonts.cpp"
python3 "$root/tools/iconsgen.py" --out "$root/firmware/generated/icons.cpp" --check
BADGER_PREVIEW="$root/build/host/badger_preview" BADGER_PREVIEW_BADGER2350="$root/build/host-badger2350/badger_preview" \
  python3 -m unittest discover -s "$root/tests_py" -v
previews "$root/build/host/badger_preview" "$root/build/previews" badger2040
previews "$root/build/host-badger2350/badger_preview" "$root/build/previews/badger2350" badger2350
