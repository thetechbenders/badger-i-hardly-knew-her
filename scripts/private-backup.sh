#!/usr/bin/env bash
# Private backup of everything needed to rebuild the personalised badge.
#
#   scripts/private-backup.sh create            -> local/backups/badger-private-<commit>.tar.gz
#   scripts/private-backup.sh verify <archive>  restore into a temp dir, check, rebuild, compare
#
# The archive holds private data (portrait, contact details) and is written
# under local/, which Git ignores. It contains:
#   local/         the private inputs (profile, portrait, originals, settings), minus backups/
#   artifacts/     the personalised firmware built from the recorded commit
#   source.bundle  git bundle of that commit (restores the exact source)
#   BUILDINFO      commit, toolchain and dependency versions
#   RESTORE.md     how to restore and reflash
#   MANIFEST.sha256
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
py="${PYTHON:-python3}"
files=(badger_badge.uf2 badger_badge-assets.uf2 badger_badge.bin badger_badge.elf badger_badge.elf.map
       memory-report.txt SHA256SUMS)

build_private() {  # src_dir build_dir
  mkdir -p "$(dirname "$2")"
  BUILD_DIR="$2" "$1/scripts/build-firmware.sh" -DPython3_EXECUTABLE="$(command -v "$py")" \
    -DBADGER_PROFILE="$1/local/profile.json" -DBADGER_PORTRAIT="$1/local/portrait.png" >"$2.log" 2>&1 ||
    { tail -30 "$2.log" >&2; echo "error: private build failed (log: $2.log)" >&2; exit 1; }
}

create() {
  cd "$root"
  local missing=()
  for f in local/profile.json local/portrait.png; do [ -f "$f" ] || missing+=("$f"); done
  if [ ${#missing[@]} -gt 0 ]; then
    echo "error: private inputs missing: ${missing[*]} (see README.md, Private content)" >&2
    exit 1
  fi
  if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "error: commit or stash tracked changes first; the backup records one exact commit" >&2
    exit 1
  fi
  "$py" -c "import sys, pathlib; sys.path.insert(0, 'tools'); import badge_profile as p; p.load(pathlib.Path('local/profile.json'))"
  local commit short build stage name
  commit="$(git rev-parse HEAD)"
  short="$(git rev-parse --short=12 HEAD)"
  build="$root/build/fw-private"
  rm -rf "$build"
  build_private "$root" "$build"
  "$py" scripts/verify_artifacts.py "$build" --require-clean >"$build.verify" ||
    { cat "$build.verify" >&2; exit 1; }

  name="badger-private-$short"
  stage="$(mktemp -d)/$name"
  mkdir -p "$stage/artifacts"
  rsync -a --exclude /backups/ local/ "$stage/local/"
  for f in "${files[@]}"; do cp "$build/$f" "$stage/artifacts/"; done
  git bundle create -q "$stage/source.bundle" HEAD "$(git rev-parse --abbrev-ref HEAD)"
  {
    echo "commit $commit"
    echo "describe $(git describe --always --dirty --tags --abbrev=12)"
    echo "branch $(git rev-parse --abbrev-ref HEAD)"
    echo "created $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "toolchain $(arm-none-eabi-gcc --version | head -1)"
    echo "python $("$py" --version 2>&1) pillow $("$py" -c 'import PIL; print(PIL.__version__)')"
    grep -E '^[A-Z_]+=' deps.lock
  } >"$stage/BUILDINFO"
  cat >"$stage/RESTORE.md" <<'EOF'
# Restoring the personalised badge

This archive is private: it contains the portrait and contact details.

1. Check integrity: `sha256sum -c MANIFEST.sha256` (inside the extracted folder).
2. Flash as is: hold BOOT/USR, tap RST, copy `artifacts/badger_badge.uf2` to
   `RPI-RP2`. The portrait is built in; `artifacts/badger_badge-assets.uf2` is
   the same portrait as a separate asset-region UF2 (optional).
3. Settings on the badge (anything committed over USB after flashing) are
   not in this archive unless `local/settings-backup.txt` exists; restore it
   with `python3 tools/badgerctl.py restore local/settings-backup.txt`.
4. Rebuild from source:
       git clone source.bundle badger && cd badger && git checkout <commit from BUILDINFO>
       cp -r ../local ./local
       scripts/fetch-deps.sh && scripts/build-firmware.sh
   With the toolchain and dependency versions in BUILDINFO, the UF2/BIN/ELF
   are byte-identical to `artifacts/` (`scripts/private-backup.sh verify` does
   exactly this).
EOF
  (cd "$stage" && find . -type f ! -name MANIFEST.sha256 -print0 | sort -z | xargs -0 sha256sum >MANIFEST.sha256)
  mkdir -p local/backups
  tar -C "$(dirname "$stage")" -czf "local/backups/$name.tar.gz" "$name"
  (cd local/backups && sha256sum "$name.tar.gz" >"$name.tar.gz.sha256")
  rm -rf "$(dirname "$stage")"
  echo "backup: $root/local/backups/$name.tar.gz"
  cat "local/backups/$name.tar.gz.sha256"
}

verify() {
  local archive tmp dir commit
  archive="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
  if [ -f "$archive.sha256" ]; then
    (cd "$(dirname "$archive")" && sha256sum -c --quiet "$(basename "$archive").sha256") ||
      { echo "FAIL  archive checksum"; return 1; }
    echo "ok    archive checksum"
  fi
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' RETURN
  tar -C "$tmp" -xzf "$archive"
  dir="$(find "$tmp" -mindepth 1 -maxdepth 1 -type d)"
  # Explicit failures: a failing `a && b` list does not trip `set -e`.
  (cd "$dir" && sha256sum -c --quiet MANIFEST.sha256) || { echo "FAIL  manifest: archive modified or damaged"; return 1; }
  echo "ok    manifest: every file intact"
  (cd "$dir/artifacts" && sha256sum -c --quiet SHA256SUMS) || { echo "FAIL  artifact SHA256SUMS"; return 1; }
  echo "ok    artifact SHA256SUMS"
  commit="$(sed -n 's/^commit //p' "$dir/BUILDINFO")"
  git clone -q "$dir/source.bundle" "$tmp/src"
  git -C "$tmp/src" checkout -q "$commit"
  echo "ok    source bundle restores commit $commit"
  cp -r "$dir/local" "$tmp/src/local"
  if [ -d "$root/deps/pico-sdk" ]; then ln -s "$root/deps" "$tmp/src/deps"; else (cd "$tmp/src" && scripts/fetch-deps.sh); fi
  # Same relative build directory as `create`: it is recorded in the ELF's
  # debug info (the UF2/BIN would match regardless).
  build_private "$tmp/src" "$tmp/src/build/fw-private"
  local same=1
  for f in badger_badge.uf2 badger_badge.bin badger_badge.elf badger_badge-assets.uf2; do
    if cmp -s "$dir/artifacts/$f" "$tmp/src/build/fw-private/$f"; then echo "ok    rebuilt $f is byte-identical"
    else echo "FAIL  rebuilt $f differs"; same=0; fi
  done
  if [ "$same" != 1 ]; then echo "reproducibility check FAILED"; return 1; fi
  echo "restore and reproducibility verified"
}

case "${1:-}" in
  create) create ;;
  verify) [ $# -eq 2 ] || { echo "usage: $0 verify <archive.tar.gz>" >&2; exit 2; }; verify "$2" ;;
  *) echo "usage: $0 create | verify <archive.tar.gz>" >&2; exit 2 ;;
esac
