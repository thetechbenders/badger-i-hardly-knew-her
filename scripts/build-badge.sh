#!/usr/bin/env bash
# Build a personalised badge from a fill-in form (see config/badge-form.toml).
#
#   scripts/build-badge.sh --new                    copy the template to local/badge.toml
#   scripts/build-badge.sh --check   local/badge.toml   validate the form and its files only
#   scripts/build-badge.sh --preview local/badge.toml   + portrait, profile and screen previews
#   scripts/build-badge.sh           local/badge.toml   + RP2040 firmware (local/out/badge/fw/)
#
# Everything is validated before the cross-compile; errors name the form
# field to fix. Outputs go to local/out/<form name>/ (git-ignored); pass
# --out DIR after the form to choose another directory.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
py="${PYTHON:-python3}"
mode=build
case "${1:-}" in
  --new) shift; exec "$py" "$root/tools/badge_form.py" new "$@" ;;
  --check) mode=check; shift ;;
  --preview) mode=preview; shift ;;
  ""|-h|--help) sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
if [ "$mode" = build ] && [ ! -d "$root/deps/pico-sdk" ]; then
  "$root/scripts/fetch-deps.sh"
fi
exec "$py" "$root/tools/badge_form.py" "$mode" "$@"
